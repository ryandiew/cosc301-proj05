#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


int* refs; // sets reference array as pointer 

void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}


void write_dirent(struct direntry *dirent, char *filename, 
		  uint16_t start_cluster, uint32_t size) // identical to func from dos_cp 
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) 
    {
	if (p2[i] == '/' || p2[i] == '\\') 
	{
	    uppername = p2+i+1;
	}
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) {
        uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
        fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else {
        *p = '\0';
        p++;
        len = strlen(p);
        if (len > 3) len = 3;
            memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) 
    {
	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* could also set time and date here if we really
       cared... */
}

void create_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size,
		   uint8_t *image_buf, struct bpb33* bpb) // identical to func from dos_cp
{
    
    
    while (1) 
    {
	if (dirent->deName[0] == SLOT_EMPTY) 
	{
	    /* we found an empty slot at the end of the directory */
	    write_dirent(dirent, filename, start_cluster, size);
	    dirent++;

	    /* make sure the next dirent is set to be empty, just in
	       case it wasn't before */
	    memset((uint8_t*)dirent, 0, sizeof(struct direntry));
	    dirent->deName[0] = SLOT_EMPTY;
	    return;
	}

	if (dirent->deName[0] == SLOT_DELETED) 
	{
	    /* we found a deleted entry - we can just overwrite it */
	    write_dirent(dirent, filename, start_cluster, size);
	    return;
	}
	dirent++;
    }
}

uint16_t find_dirent(struct direntry *dirent, char *buffer) {
    uint16_t followclust = 0;
    memset(buffer, 0, MAXFILENAME);

    int i;
    char name[9];
    char extension[4];
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY){
      return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED){ 
      return followclust;
    }

    // dot entry ("." or "..")
    // skip it
    if (((uint8_t)name[0]) == 0x2E){ 
      return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) {
        if (name[i] == ' ') name[i] = '\0';
        else break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) {
        if (extension[i] == ' ') extension[i] = '\0';
        else break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN) {
        // ignore any long file name extension entries
        //
        // printf("Win95 long-filename entry seq 0x%0x\n", 
        //          dirent->deName[0]);
    } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
        if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN) {
            strcpy(buffer, name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    } else {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
        strcpy(buffer, name);
        if (strlen(extension))  {
            strcat(buffer, ".");
            strcat(buffer, extension);
        }
    }

    return followclust;
}

void print_indent(int indent) // identical to func in dos_ls
{
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}


uint16_t print_dirent(struct direntry *dirent, int indent, struct bpb33 *bpb, uint8_t *image_buf) // modified from dos_ls -- able to detect size differences
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
	printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
	    print_indent(indent);
    	    printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
	int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
	int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
	int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
	int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;

	size = getulong(dirent->deFileSize);
	print_indent(indent);
	printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
	       name, extension, size, getushort(dirent->deStartCluster),
	       ro?'r':' ', 
               hidden?'h':' ', 
               sys?'s':' ', 
               arch?'a':' ');
      
       // let the modifications begin :) -- checking size 
       
       int num_clust = 0;
       uint16_t prev_clust;
       
       uint16_t cluster = getushort(dirent->deStartCluster);
       while(is_valid_cluster(cluster, bpb)) {
         refs[cluster]++;
         if(refs[cluster] > 1){
         dirent->deName[0] = SLOT_DELETED;
         refs[cluster]--;
         printf("scan error: multiple references to same cluster");
         }
         
         prev_clust = cluster;
         cluster = get_fat_entry(cluster, image_buf, bpb);
         
         if(prev_clust == cluster) { // checks if pointing to self
           printf("points to itself\n");
           set_fat_entry(cluster, FAT12_MASK& CLUST_EOFS, image_buf, bpb);
           num_clust++;
           break;
         }

         if(cluster == (FAT12_MASK & CLUST_BAD)){
           printf("Bad Cluster\n");
           set_fat_entry(cluster, FAT12_MASK & CLUST_FREE, image_buf, bpb);
           set_fat_entry(prev_clust, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
           num_clust++;
           break; 
         }
         num_clust++;
      }
     
      int max = num_clust * 512;
      int min = max - 512;

      if(size > max){
        printf("Size greater than cluster num");
        putulong(dirent->deFileSize, max);
      }
      
      else if(size <= min){
        printf("Size less than cluster num");
        putulong(dirent->deFileSize,max);
      }
    }

    return followclust;
}

void follow_dir(uint16_t cluster, int indent,
		uint8_t *image_buf, struct bpb33* bpb) // from dos_ls
{
    while (is_valid_cluster(cluster, bpb)) {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
	for ( ; i < numDirEntries; i++)
	{
            
            uint16_t followclust = print_dirent(dirent, indent,bpb,image_buf);
            if (followclust)
                follow_dir(followclust, indent+1, image_buf, bpb);
            dirent++;
	}

	cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}

void traverse_root(uint8_t *image_buf, struct bpb33* bpb) {
    uint16_t cluster = 0;

    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++) {
        uint16_t followclust = print_dirent(dirent, 0,bpb,image_buf);
        
        
        if (is_valid_cluster(followclust, bpb)) {   //update reference count
            refs[followclust]++;
            follow_dir(followclust, 1, image_buf, bpb);
        }

        dirent++;
    }
}

void update_orphans(uint8_t *image_buf, struct bpb33* bpb) {
    char name[128];
    char num[16];
    
    uint16_t next_cluster;
    struct direntry *dirent = (struct direntry*)cluster_to_addr(0, image_buf, bpb);
    
    int orphan_count = 0;
    int clusters = 0;
    
    for (int i = 2; i < bpb->bpbSectors; i++) {
        next_cluster = get_fat_entry(i, image_buf, bpb);        
    
        if (refs[i] == 0 && next_cluster != (FAT12_MASK&CLUST_FREE)) {
            if (next_cluster == (FAT12_MASK & CLUST_BAD)) {
                printf("Found bad orph in cluster %d.\n", i);
                set_fat_entry(next_cluster, CLUST_FREE, image_buf, bpb);
                continue;
            }
            
            orphan_count++;
            printf("Found Orphan in cluster %d.\n", i);
    
            // convert from %d -> str
            sprintf(num, "%d", orphan_count);
            strcpy(name, "found");
            strcat(name, num);
            strcat(name, ".dat");
            
            create_dirent(dirent, name, i, clusters * 512, image_buf, bpb);
            set_fat_entry(i, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
            printf("Orphan is fostered\n");
        }
    }
}


int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
        usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    
    refs = calloc(bpb->bpbSectors-33, sizeof(int));

    traverse_root(image_buf, bpb);
    update_orphans(image_buf, bpb);
    unmmap_file(image_buf, &fd);
    return 0;
}





    unmmap_file(image_buf, &fd);
    return 0;
}
