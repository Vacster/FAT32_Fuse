#ifndef _FAT32_H_
#define _FAT32_H_

#define BPB_OFFSET 0xB

#include <fuse.h>
#include <stdint.h>

//Prevents Padding
#pragma pack(push,1)
struct bios_param_block {
  int16_t   bytes_per_sector;
  int8_t    sectors_per_cluster;
  int16_t   reserved_logical_sectors;
  int8_t    file_allocation_tables;
  int16_t   max_root_directory_entries; //Should be 0
  int16_t   total_logical_sectors;
  uint8_t   media_descriptor;
  int16_t   logical_sectors_per_fat;    //Should be 0
  int16_t   sectors_per_track;
  int16_t   number_of_heads;
  int64_t   hidden_sectors;
  int64_t   large_sectors;
  int64_t   sectors_per_fat;
  int16_t   something;
  int16_t   file_system_version;
  int64_t   root_cluster_number;
  int16_t   file_system_information_sector_number;
  int16_t   backup_boot_sector;
  int8_t    reserved[12];
} bpb;
#pragma pack(pop)

void print_bpb();
void *fat32_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
int fat32_getattr (const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int fat32_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags);

static struct fuse_operations fuse_ops = {
  .init       =   fat32_init,
  .readdir    =   fat32_readdir,
  .getattr    =   fat32_getattr,
};

unsigned char *buffer;

#endif
