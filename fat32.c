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

  //Start with a default, arbitrary size. Will be deleted soon.
  buffer = (unsigned char*)malloc(512);
  device_read_sector(buffer, 512, 1, 0);
  memcpy(&bpb, buffer+BPB_OFFSET, sizeof(struct bios_param_block));

  free(buffer);
  buffer = (unsigned char*)malloc(bpb.bytes_per_sector);

  print_bpb();

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
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_nlink = 1;
    stbuf->st_size = 1024;
  }

  return 0;
}

int fat32_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
  printf("[READDIR]\n");

  filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);
  return 0;
}

void print_bpb()
{
  printf("Bytes per Sector: %" PRId16 "\n",         bpb.bytes_per_sector);
  printf("Sectors per Cluster: %" PRId8 "\n",       bpb.sectors_per_cluster);
  printf("Reserved Logical Sectors: %" PRId16 "\n", bpb.reserved_logical_sectors);
  printf("File Allocation Tables #: %" PRId8 "\n",  bpb.file_allocation_tables);
  printf("Max Root Dir Entries: %" PRId16 "\n",     bpb.max_root_directory_entries);
  printf("Total Logical Sectors: %" PRId16 "\n",    bpb.total_logical_sectors);
  printf("Media Descriptor: %u\n",                  bpb.media_descriptor);
  printf("Logical Sectors per FAT: %" PRId16 "\n",  bpb.logical_sectors_per_fat);
  printf("Sectors per Track: %" PRId16 "\n",        bpb.sectors_per_track);
  printf("Number of Heads: %" PRId16 "\n",          bpb.number_of_heads);
  printf("Hidden Sectors: %" PRId64 "\n",           bpb.hidden_sectors);
  printf("Large Sectors: %" PRId64 "\n",            bpb.large_sectors);
  printf("Sectors per FAT: %" PRId64 "\n",          bpb.sectors_per_fat);
  printf("File System Version: %" PRId16 "\n",      bpb.file_system_version);
  printf("Root Cluser Number: %" PRId64 "\n",       bpb.root_cluster_number);
  printf("FS Info Sector Number: %" PRId16 "\n",    bpb.file_system_information_sector_number);
  printf("Backup Boot Sector: %" PRId16 "\n",       bpb.backup_boot_sector);
}

int main(int argc, char *argv[])
{
  if(!device_open(realpath("/dev/sdb1", NULL)))
  {
    printf("Cannot open device file %s\n", argv[1]);
    return -1;
  }

  return fuse_main(argc, argv, &fuse_ops, NULL);
}
