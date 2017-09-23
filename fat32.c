#include "device.h"
#include "fat32.h"

#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

void *fat32_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
  printf("[INIT]\n");
  bpb = (struct bios_param_block*)malloc(sizeof(struct bios_param_block));

  device_read_sector((char*)bpb, sizeof(struct bios_param_block), 1, BPB_OFFSET);
  print_bpb();

  buffer          =  (char*)malloc(bpb->bytes_sector);
  cluster_buffer  =  (char*)malloc(bpb->bytes_sector * bpb->sectors_cluster);

  fat_offset = bpb->reserved_sectors * bpb->bytes_sector;
  clusters_offset = fat_offset + (bpb->fat_amount * bpb->sectors_per_fat * bpb->bytes_sector);
  device_read_sector((char*)&end_of_chain, 4, 1, fat_offset + 4);
  // printf("%d\n",clusters_offset);
  //Printing info of third(second?) file. Change multiplier to print another one.
  // device_read_sector((char*)dir_entry, sizeof(struct directory_entry), 1, clusters_offset+(32*2));
  // print_dir_entry();
  //
  // unsigned int next;
  // device_read_sector((char*)&next, sizeof(int), 1, fat_offset+(((dir_entry->First_Cluster_High<<16)|dir_entry->First_Cluster_Low) * 4));
  struct directory_entry *dir_entry = resolve("/FOLDER/NEW_IMG");
  if(dir_entry != NULL){
    print_dir_entry(dir_entry);
    printf("LFN: %s\n", get_long_filename(dir_entry));
  }else{
    printf("Not Found\n");
  }
  // printf("remaining_clusters: %u\n", remaining_clusters(3));
  return NULL;
}

int fat32_getattr (const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
  printf("[GETATTR]\n");
  printf( "\tAttributes of %s requested\n", path );

  // GNU's definitions of the attributes (http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html):
  // 		st_uid: 	The user ID of the file’s owner.
  //		st_gid: 	The group ID of the file.
  //		st_atime: 	This is the last access time for the file.
  //		st_mtime: 	This is the time of the last modification to the contents of the file.
  //		st_mode: 	Specifies the mode of the file. This includes file type information (see Testing File Type) and the file permission bits (see Permission Bits).
  //		st_nlink: 	The number of hard links to the file. This count keeps track of how many directories have entries for this file. If the count is ever decremented to zero, then the file itself is discarded as soon
  //						as no process still holds it open. Symbolic links are not counted in the total.
  //		st_size:	This specifies the size of a regular file in bytes. For files that are really devices this field isn’t usually meaningful. For symbolic links this specifies the length of the file name the link refers to.

  stbuf->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
  stbuf->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted the filesystem
  stbuf->st_atime = time( NULL ); // The last "a"ccess of the file/directory is right now
  stbuf->st_mtime = time( NULL ); // The last "m"odification of the file/directory is right now
  if ( strcmp( path, "/" ) == 0 )
  {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536
  }
  else
  {
    struct directory_entry *dir_entry = resolve(path);
    if(dir_entry != NULL){
      if(dir_entry->Attributes & (1<<4))
        stbuf->st_mode = S_IFDIR;
      else
        stbuf->st_mode = S_IFREG | 0644;

      stbuf->st_nlink = 1;  //TODO: ?
      stbuf->st_size = dir_entry->Filesize;
    }
  }

  return 0;
}

int fat32_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
  printf("[READDIR]\n");

  struct directory_entry *dir_entry = resolve(path);
  int cluster = ((dir_entry->First_Cluster_High<<16)|dir_entry->First_Cluster_Low);
  int dir_entries_per_cluster = (bpb->sectors_cluster*bpb->bytes_sector) / sizeof(struct directory_entry);
  device_read_sector(cluster_buffer, bpb->bytes_sector, bpb->sectors_cluster, clusters_offset + ((bpb->sectors_cluster*bpb->bytes_sector)*(cluster - 2)));
  for(int x = 0; x < dir_entries_per_cluster; x ++)
  {
    memcpy(dir_entry, cluster_buffer + (sizeof(struct directory_entry) * x), sizeof(struct directory_entry));

    if(is_dir_entry_empty(dir_entry))
      break;

    if(*((uint8_t*)dir_entry) != 0x41 && *((uint8_t*)dir_entry) != 0xE5 && !(dir_entry->Attributes & (1<<3)))
    {
      //Hack
      char name[9];
      name[8] = '\0';
      strncpy(name, dir_entry->Short_Filename, 8);
      filler(buf, name, NULL, 0, FUSE_FILL_DIR_PLUS);
    }
  }

  return 0;
}

struct directory_entry* resolve(const char *path)
{
  printf("[RESOLVE] %s\n", path);
  int current_cluster = bpb->root_cluster_number;

  char *path_copy = strdup(path);
  char *token = strtok(path_copy, "/");
  int dir_entries_per_cluster = (bpb->sectors_cluster*bpb->bytes_sector) / sizeof(struct directory_entry);
  struct directory_entry *dir_entry = (struct directory_entry*) malloc(sizeof(struct directory_entry));

  device_read_sector(cluster_buffer, bpb->bytes_sector, bpb->sectors_cluster, clusters_offset + ((bpb->sectors_cluster*bpb->bytes_sector)*(current_cluster - 2)));
  if(!strcmp(path_copy, "/"))
  {
    //This is a hack
    dir_entry->First_Cluster_High = 0xFF00 & bpb->root_cluster_number;
    dir_entry->First_Cluster_Low = 0x00FF & bpb->root_cluster_number;
    return dir_entry;
  }
  while(token != NULL){
    //TODO: Create function that returns amount of clusters left in chain and use a while (hasn't reached last cluster+1) it continues and reads next cluster
    for(int x = 0; x < dir_entries_per_cluster; x++)
    {
      memcpy(dir_entry, cluster_buffer + (sizeof(struct directory_entry) * x), sizeof(struct directory_entry));
      if(*((uint8_t*)dir_entry) == 0)
        return NULL;

      if(!strncmp(token, dir_entry->Short_Filename, strlen(token)))
      {
        //If the file found is a Directory
        if(dir_entry->Attributes & 0b00010000)
        {
          int cluster = ((dir_entry->First_Cluster_High<<16)|dir_entry->First_Cluster_Low);
          device_read_sector(cluster_buffer, bpb->bytes_sector, bpb->sectors_cluster, clusters_offset + ((bpb->sectors_cluster*bpb->bytes_sector)*(cluster - 2)));
          token = strtok(NULL, "/");
          if(token == NULL)
            return dir_entry;
          x = 0;
          break;
        }else{
          //This way we prevent errors when looking for folder has same name as a file
          token = strtok(NULL, "/");
          free(path_copy);
          return token != NULL ? NULL : dir_entry;
        }
      }
    }
  }
  free(path_copy);
  return NULL;
}

//Function that returns amount of clusters left in chain
int remaining_clusters(int starting_cluster)
{
  int current_cluster = starting_cluster;
  int remaining_clusters = 0;

  do
  {
    //Reading one int at a time for convenience. Not efficient but prevents errors
    get_next_cluster(&current_cluster);
    remaining_clusters++;
  } while(current_cluster != end_of_chain);
  return remaining_clusters;
}

void get_next_cluster(int *current_cluster)
{
  device_read_sector((char*)current_cluster, sizeof(int), 1, fat_offset + (*current_cluster * 4));
}

int is_dir_entry_empty(struct directory_entry *dir_entry)
{
  for(int x = 0; x < sizeof(struct directory_entry); x++)
  {
    if(*((int8_t*)dir_entry) != 0)
      return 0;
  }
  return 1;
}

//Should remember to free after use
char *get_long_filename(struct directory_entry *dir_entry)
{
  struct long_filename_entry *lfn_entry = (struct long_filename_entry*)dir_entry;

  //Look for 1st LFN entry
  while((lfn_entry->sequence_number & 0xF0) != 1){
    printf("Hello\n");
    lfn_entry -= sizeof(struct long_filename_entry);
  }
  printf("Fin\n");

  char *name = (char*)malloc(sizeof(char) * 255); //Max name size (should be 260 but fuck life)
   //Max Dir entries concatenated
  for(int x = 0, y = 0; x < 20 && lfn_entry->attribute == 0x0F; x++, lfn_entry -= sizeof(struct long_filename_entry))
  {
    int z;
    for(z = 0; z < 5; z++)
      name[y++] = lfn_entry->name_1[z] & 0x0F;
    for(z = 0; z < 6; z++)
      name[y++] = lfn_entry->name_2[z] & 0x0F;
    for(z = 0; z < 2; z++)
      name[y++] = lfn_entry->name_3[z] & 0x0F;
  }
  return name;
}
//Printing only the relevant data
void print_bpb()
{
  printf("\n-- BIOS Parameter Block -- \n");
  printf("Bytes per Sector: %" PRId16 "\n",         bpb->bytes_sector);
  printf("Sectors per Cluster: %" PRId8 "\n",       bpb->sectors_cluster);
  printf("Reserved Logical Sectors: %" PRId16 "\n", bpb->reserved_sectors);
  printf("File Allocation Tables #: %" PRId8 "\n",  bpb->fat_amount);
  printf("Logical Sectors per FAT: %" PRId32 "\n",  bpb->sectors_per_fat);
  printf("Root Cluster Number: %" PRId32 "\n",      bpb->root_cluster_number);
}

void print_dir_entry(struct directory_entry *dir_entry)
{
  int32_t first_cluster = (dir_entry->First_Cluster_High << 16) | dir_entry->First_Cluster_Low;
  printf("\n-- Directory Entry -- \n");
  printf("Filename: %s\n",                    dir_entry->Short_Filename);
  printf("Attributes: %X\n",                  dir_entry->Attributes);
  printf("First Cluster: %" PRId16 "\n",      first_cluster);
  printf("File Size: %" PRId32 "\n",          dir_entry->Filesize);
}

int main(int argc, char *argv[])
{
  if(!device_open(realpath("/dev/disk/by-label/fuse", NULL)))
  {
    printf("Cannot open device file %s\n", "/dev/disk/by-label/fuse");
    return -1;
  }

  return fuse_main(argc, argv, &fuse_ops, NULL);
}
