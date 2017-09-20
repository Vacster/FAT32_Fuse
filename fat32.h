#ifndef _FAT32_H_
#define _FAT32_H_

#define BPB_OFFSET 0xB

#include <fuse.h>
#include <stdint.h>

//Prevents Padding
#pragma pack(push,1)
//Lots of missing or incorrect data online prevents a relevant struct
struct bios_param_block {
  int16_t   bytes_sector;               //bytes_per_sector
  int8_t    sectors_cluster;            //sectors_per_cluster
  int16_t   reserved_sectors;           //reserved_logical_sectors
  int8_t    fat_amount;                 //file_allocation_tables_amount
  int16_t   max_root_directory_entries; //Should be 0
  int16_t   total_logical_sectors;
  uint8_t   media_descriptor;
  int16_t   logical_sectors_per_fat;    //Should be 0
  int16_t   sectors_per_track;
  int16_t   number_of_heads;
  int64_t   hidden_sectors;
  int64_t   sectors_per_fat;
} *bpb;

struct directory_entry {
  char    Short_Filename[8];
  char    Short_File_Extension[3];

  /*
    Attribute Bits:
    0:    read only
    1:    hidden      -   Shouldn't show in Dir listing
    2:    system      -   Belongs to system, shouldn't be moved
    3:    volume id   -   Filename is volume label
    4:    directory   -   Is a Subdirectory
    5:    archive     -   Has been changed since last backup, ignore
    6-7:  unused, should be 0
  */
  int8_t  Attributes;
  int8_t  Extended_Attributes;

  /*
    Bits 15-11:   Hours   (0-23)
    Bits 10-5:    Minutes (0-59)
    Bits 4-0:     Seconds (0-29) - Only recorded to a 2 second resolution
  */
  int8_t  Create_Time_Finer;
  int16_t Create_Time;

  /*
    Bits 15-9:    Year    (0 = 1980)
    Bits 8-5:     Month   (1-12)
    Bits 4-0:     Day     (1-31)
  */
  int16_t Create_Date;
  int16_t Last_Access_Date;   //Doubtful
  int16_t First_Cluster_High;
  int16_t Last_Modified_Time; //Doubtful
  int16_t Last_Modified_Date; //Doubtful
  int16_t First_Cluster_Low;
  int32_t Filesize;
} *dir_entry;
#pragma pack(pop)

void print_bpb();
void print_dir_entry();
void *fat32_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
int fat32_getattr (const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int fat32_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags);

static struct fuse_operations fuse_ops = {
  .init       =   fat32_init,
  .readdir    =   fat32_readdir,
  .getattr    =   fat32_getattr,
};

char *buffer, *cluster_buffer;
int fat_offset, clusters_offset;

#endif
