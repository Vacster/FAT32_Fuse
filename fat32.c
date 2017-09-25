#include "device.h"
#include "fat32.h"

#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

void *fat32_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
  printf("[INIT]\n");
  bpb = (struct bios_param_block*)malloc(sizeof(struct bios_param_block));

  device_read_sector((char*)bpb, sizeof(struct bios_param_block), 1, BPB_OFFSET);
  print_bpb();

  fat_offset = bpb->reserved_sectors * bpb->bytes_sector;
  clusters_offset = fat_offset + (bpb->fat_amount * bpb->sectors_per_fat * bpb->bytes_sector);
  device_read_sector((char*)&end_of_chain, 4, 1, fat_offset + 4);
  return NULL;
}

int fat32_getattr (const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
  printf("[GETATTR] %s\n", path);

  // GNU's definitions of the attributes (http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html):
  // 		st_uid: 	The user ID of the file’s owner.
  //		st_gid: 	The group ID of the file.
  //		st_atime: 	This is the last access time for the file.
  //		st_mtime: 	This is the time of the last modification to the contents of the file.
  //		st_mode: 	Specifies the mode of the file. This includes file type information (see Testing File Type) and the file permission bits (see Permission Bits).
  //		st_nlink: 	The number of hard links to the file. This count keeps track of how many directories have entries for this file. If the count is ever decremented to zero, then the file itself is discarded as soon
  //						as no process still holds it open. Symbolic links are not counted in the total.
  //		st_size:	This specifies the size of a regular file in bytes. For files that are really devices this field isn’t usually meaningful. For symbolic links this specifies the length of the file name the link refers to.

  stbuf->st_uid = getuid(); // 1The owner of the file/directory is the user who mounted the filesystem
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
  printf("[READDIR] %s\n", path);

  struct directory_entry *dir_entry = resolve(path);
  int cluster = ((dir_entry->First_Cluster_High<<16)|dir_entry->First_Cluster_Low);
  int dir_entries_per_cluster = (bpb->sectors_cluster*bpb->bytes_sector) / sizeof(struct directory_entry);
  char *clust_buffer = (char*)malloc(bpb->sectors_cluster*bpb->bytes_sector);
  device_read_sector(clust_buffer, bpb->bytes_sector, bpb->sectors_cluster, clusters_offset + ((bpb->sectors_cluster*bpb->bytes_sector)*(cluster - 2)));
  for(int x = 0; x < dir_entries_per_cluster; x++)
  {
    memcpy(dir_entry, clust_buffer + (sizeof(struct directory_entry) * x), sizeof(struct directory_entry));

    if(is_dir_entry_empty(dir_entry))//Once one is empty the rest are too
      break;

    if(*((uint8_t*)dir_entry) != 0xE5 && !(dir_entry->Attributes & (1<<3)) && !(dir_entry->Attributes & 1))
    {
      printf("--\n");
      print_dir_entry(dir_entry);
      printf("--\n");
      char *name = get_long_filename(cluster, x);
      if(filler(buf, name, NULL, 0, FUSE_FILL_DIR_PLUS))
        return 0;
      free(name);
    }
  }
  free(clust_buffer);
  free(dir_entry);
  return 0;
}

//Found online, simple helper for truncate
void replaceLast(char * str, char oldChar, char newChar, int *lastIndex)
{
    int i;

    i = 0;

    while(str[i] != '\0')
    {
        if(str[i] == oldChar)
            *lastIndex = i;
        i++;
    }

    if(*lastIndex != -1)
        str[*lastIndex] = newChar;
}

int fat32_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
  printf("[TRUNCATE] %s %jd\n", path, size);
  struct directory_entry *dir_entry = resolve(path);
  if(dir_entry == NULL)
  {
    printf("Path is nonexistant");
    free(dir_entry);
    return -ENOENT;
  }
  dir_entry->Filesize = size;

  char *path_cpy = strdup(path);
  int last_index = -1;
  replaceLast(path_cpy, '/', '\0', &last_index);

  int64_t parent_cluster;
  if(strlen(path_cpy))   //If not root
  {
    struct directory_entry *parent_entry = resolve(path_cpy);
    if(parent_entry == NULL)
    {
      printf("Parent is nonexistant");
      free(parent_entry);
      return -ENOENT;
    }
    parent_cluster = ((parent_entry->First_Cluster_High<<16)|parent_entry->First_Cluster_Low);
  }else
    parent_cluster = bpb->root_cluster_number;

  for(int x = 0; x < (bpb->sectors_cluster * bpb->bytes_sector)/sizeof(struct directory_entry); x++)
  {
    char *lfn = get_long_filename(parent_cluster, x);
    if(!strcmp(lfn, path_cpy + last_index + 1))
    {
      device_write_sector((char*)dir_entry, sizeof(dir_entry), 1, clusters_offset + ((parent_cluster - 2) * (bpb->sectors_cluster * bpb->bytes_sector)) + (x * sizeof(dir_entry)));
      free(dir_entry);
      free(lfn);
      return 0;
    }
    free(lfn);
  }
  return 0;
}

int fat32_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info * fi)
{
  printf("[WRITE] %s\tSize: %lu\tOffset: %lu \n", path, size, offset);
  int cluster_size = (bpb->sectors_cluster*bpb->bytes_sector);
  struct directory_entry *dir_entry = resolve(path);
  if(dir_entry == NULL)
  {
    printf("Path is nonexistant");
    free(dir_entry);
    return -ENOENT;
  }
  uint32_t next = (uint32_t)fi->fh;
  // uint32_t last;
  size_t write_count;
  char *cluster_buffer = (char*)malloc(cluster_size);

  // device_write_sector(buffer, size, 1, clusters_offset + (cluster_size * (next - 2)));
  //Write until eof, if not done continue in next for-loop
  for(write_count = 0; write_count < ceil((size*1.0)/cluster_size) && next != end_of_chain; write_count++, get_next_cluster(&next))
  {
    printf("Writing on cluster %d\n", next);
    memcpy(cluster_buffer, buffer + write_count, cluster_size);
    device_write_sector(cluster_buffer, cluster_size, 1, clusters_offset + (cluster_size * (next - 2)));
    // last = next;
  }
  // for(; write_count < size/cluster_size; write_cou1nt += cluster_size)
  // {
  //   next = get_free_fat();
  //   device_write_sector((char*)&next, sizeof(next), 1, fat_offset + (last * 4));
  //
  //   memcpy(cluster_buffer, buffer + write_count, cluster_size);
  //
  //   device_write_sector(cluster_buffer, cluster_size, 1, clusters_offset + (cluster_size * (next - 2)));
  //   last = next;
  //   device_write_sector((char*)&end_of_chain, sizeof(end_of_chain), 1, fat_offset + (last * 4));
  // }

  free(cluster_buffer);
  return size;
}

int fat32_open(const char* path, struct fuse_file_info* fi)
{
  printf("[OPEN] %s\n", path);
  struct directory_entry *dir_entry = resolve(path);
  if(dir_entry == NULL)
  {
    printf("Path is nonexistant");
    free(dir_entry);
    return -ENOENT;
  }

  //File handler in this case is simply the cluster in which it begins
  fi->fh = ((dir_entry->First_Cluster_High<<16)|dir_entry->First_Cluster_Low);
  printf("File Opened Succesfully!\nCluster: %"PRId64"\n", fi->fh);
  free(dir_entry);
  return 0;
}

int fat32_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
  pthread_mutex_lock(&read_mutex);
  printf("[READ] %s, %lu, %lu\n", path, size, offset);
  struct directory_entry *dir_entry = resolve(path);
  if(dir_entry == NULL || !fi->fh)
  {
    printf("Path is nonexistant");
    free(dir_entry);
    pthread_mutex_unlock(&read_mutex);
    return -ENOENT;
  }

  printf("Cluster: %" PRId64 "\n", fi->fh);
  int cluster_size = (bpb->sectors_cluster*bpb->bytes_sector);

  int cluster_offsets = floor((offset*1.0)/cluster_size);
  uint32_t current_cluster = (uint32_t)fi->fh;
  offset -= cluster_offsets * cluster_size;

  for(int x = 0; x < cluster_offsets; x++, get_next_cluster(&current_cluster));

  char *cluster_buffer = (char*)malloc(size);
  for(int x = 0; x < ceil((size*1.0)/cluster_size); x++){
    printf("CO: %d\t\tOff: %lu\t\tCC: %"PRId32"\t\tCount: %d\n", cluster_offsets, offset, current_cluster, x);

    //On the first read (!x) we take in count the remaining offset that is not divisible by cluster_size
    device_read_sector(cluster_buffer + (x * cluster_size), cluster_size - (!x ? offset : 0), 1, (!x ? offset : 0) + clusters_offset + (cluster_size * (current_cluster - 2)));

    if(!remaining_clusters(current_cluster))
      break;
    get_next_cluster(&current_cluster);
  }
  memcpy(buf, cluster_buffer, size);
  free(dir_entry);
  free(cluster_buffer);
  pthread_mutex_unlock(&read_mutex);
  return size;
}

struct directory_entry *resolve(const char *path)
{
  printf("[RESOLVE] %s\n", path);
  int current_cluster = bpb->root_cluster_number;

  char *path_copy = strdup(path);
  char *token = strtok(path_copy, "/");
  int dir_entries_per_cluster = (bpb->sectors_cluster*bpb->bytes_sector) / sizeof(struct directory_entry);
  struct directory_entry *dir_entry = (struct directory_entry*) malloc(sizeof(struct directory_entry));
  char *cluster_buffer = (char*)malloc(bpb->sectors_cluster*bpb->bytes_sector);
  device_read_sector(cluster_buffer, bpb->bytes_sector, bpb->sectors_cluster, clusters_offset + ((bpb->sectors_cluster*bpb->bytes_sector)*(current_cluster - 2)));

  if(!strcmp(path_copy, "/"))
  {
    //This is a hack
    dir_entry->First_Cluster_High = 0xFF00 & bpb->root_cluster_number;
    dir_entry->First_Cluster_Low = 0x00FF & bpb->root_cluster_number;
    dir_entry->Attributes = 1; //Hidden
    return dir_entry;
  }

  while(token != NULL){
    for(int x = 0; x < dir_entries_per_cluster; x++)
    {
      memcpy(dir_entry, cluster_buffer + (sizeof(struct directory_entry) * x), sizeof(struct directory_entry));
      if(*((uint8_t*)dir_entry) == 0){
        free(cluster_buffer);
        free(dir_entry);
        return NULL;
      }

      char *lfn = get_long_filename(current_cluster, x);
      // printf("Comparing %s and %s\n", token, lfn);
      if(!strncmp(token, lfn, strlen(lfn)+1))
      {
        free(lfn);
        //If the file found is a Directory
        if(dir_entry->Attributes & 0b00010000)
        {
          current_cluster = ((dir_entry->First_Cluster_High<<16)|dir_entry->First_Cluster_Low);
          device_read_sector(cluster_buffer, bpb->bytes_sector, bpb->sectors_cluster, clusters_offset + ((bpb->sectors_cluster*bpb->bytes_sector)*(current_cluster - 2)));
          token = strtok(NULL, "/");
          if(token == NULL){
            free(cluster_buffer);
            return dir_entry;
          }
          x = 0;
          break;
        }else{
          //This way we prevent errors when looking for folder has same name as a file
          token = strtok(NULL, "/");
          free(path_copy);
          free(cluster_buffer);
          return token != NULL ? NULL : dir_entry;
        }
      }else
        free(lfn);
    }
  }
  free(path_copy);
  free(dir_entry);
  return NULL;
}

int fat32_create (const char *path, mode_t mode, struct fuse_file_info *fi)
{

}

//Function that returns amount of clusters left in chain
uint32_t remaining_clusters(uint32_t starting_cluster)
{
  uint32_t current_cluster = starting_cluster;
  uint32_t remaining_clusters = 0;

  do
  {
    //Reading one int at a time for convenience. Not efficient but prevents errors
    get_next_cluster(&current_cluster);
    remaining_clusters++;
  } while(current_cluster != end_of_chain);
  return remaining_clusters - 1;
}

void get_next_cluster(uint32_t *current_cluster)
{
  device_read_sector((char*)current_cluster, sizeof(uint32_t), 1, fat_offset + (*current_cluster * 4));
}

int is_dir_entry_empty(struct directory_entry *dir_entry)
{
  return !*((int8_t*)dir_entry);
}

//Should remember to free after use
char *get_long_filename(int cluster, int entry)
{
  // printf("[GET_LONG_FILENAME]\n");
  char *tmp_cluster_buffer = (char*)malloc(bpb->bytes_sector * bpb->sectors_cluster);
  device_read_sector(tmp_cluster_buffer, bpb->bytes_sector, bpb->sectors_cluster, clusters_offset + ((bpb->sectors_cluster*bpb->bytes_sector)*(cluster - 2)));
  struct directory_entry special_dir;
  struct long_filename_entry lfn_entry;

  char *name = (char*)malloc(sizeof(char) * 255); //Max name size (should be 260 but fuck life)

  //Special cases are obligatory
  memcpy(&special_dir, tmp_cluster_buffer + (sizeof(struct long_filename_entry)*(entry)), sizeof(struct long_filename_entry));
  if(!strncmp("..", special_dir.Short_Filename, 2)){
    strncpy(name, "..", 3);
  }else if(!strncmp(".", special_dir.Short_Filename, 1)){
    strncpy(name, ".", 2);
  }else{
    // Look for 1st LFN entry. 20 is max number of lfn dirs

    memcpy(&lfn_entry, tmp_cluster_buffer + (sizeof(struct long_filename_entry)*(entry-1)), sizeof(struct long_filename_entry));
    for(int x = 1, y = 0; x < 20 && lfn_entry.attribute == 15; x++)
    {
      int z;
      for(z = 0; z < 5; z++)
        name[y++] = lfn_entry.name_1[z*2];
      for(z = 0; z < 6; z++)
        name[y++] = lfn_entry.name_2[z*2];
      for(z = 0; z < 2; z++)
        name[y++] = lfn_entry.name_3[z*2];
      memcpy(&lfn_entry, tmp_cluster_buffer + (sizeof(struct long_filename_entry)*(entry-x-1)), sizeof(struct long_filename_entry));
    }
  }
  free(tmp_cluster_buffer);
  return name;
}

int get_free_fat()
{
  int buffer[bpb->bytes_sector * bpb->sectors_per_fat]; //Just to improve the hassle of freeing
  device_read_sector((char*)buffer, bpb->bytes_sector, bpb->sectors_per_fat, fat_offset);

  for (int x = 0; x < (bpb->bytes_sector * bpb->sectors_per_fat)/sizeof(int32_t); x++)
    if(!buffer[x])
      return x;
  return -1;
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
    printf("Cannot open device file\n");
    return -1;
  }

  return fuse_main(argc, argv, &fuse_ops, NULL);
}
