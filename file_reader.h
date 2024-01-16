//
// Created by macha on 07.01.2024.
//

#ifndef FAT_FILE_READER_H
#define FAT_FILE_READER_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define SEC_SIZE 512

struct SFN{
    char file_name[11];
    uint8_t file_attri;
    uint8_t  reserved;

    uint8_t file_creation_time_t_s;
    uint16_t file_creation_time_h;

    uint16_t creation_date;

    uint16_t access_date;
    uint16_t addres_of_first_cluster;

    uint16_t modified_time;
    uint16_t modified_date;

    uint16_t low_order_first_cluster;
    uint32_t file_size;
}__attribute__((__packed__));
struct boot_sector{
    char  Assembly_code[3];
    char OEM[8];
    uint16_t bytes_per_sec;
    uint8_t sector_per_cluster;
    uint16_t size_of_reserved;
    uint8_t Number_of_fats;
    uint16_t Max_numbers_of_files;
    uint16_t number_of_sectors_16;
    uint8_t media_type;
    uint16_t size_of_each_fat;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t number_of_sectors_b_partition;
    uint32_t number_of_sectors_32;
    uint8_t  BIOS_INT;
    uint8_t not_used;
    uint8_t Extended_boot;
    uint32_t volume_serial_number;
    char Volume_label[11];
    char File_system_type[8];
    uint8_t not_used_1[448];

    uint16_t Signature_value;
}__attribute__((__packed__));

struct disk_t{
    FILE *disk;
    int num_sectors;
};
struct volume_t{
    struct disk_t *str_disk;
    struct boot_sector info;
    void *FAT_1;
    void* root_dir;
};
struct clusters_chain_t {
    uint16_t *clusters;
    size_t size;
};
struct file_t{
    uint16_t cursor;
    size_t root_num;
    int data_start;
    struct clusters_chain_t *chain;
    struct volume_t *vol;
};
uint16_t glue_two_bytes(uint8_t left, uint8_t right, int odd);
char* real_file(const char* file_name);
struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster);
struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);
struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);
int file_close(struct file_t *file);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int get_sector_for_cluster(struct file_t *temp, int num_of_cluster);
int return_cluster_info(struct file_t *stream, char*info);
struct dir_t{
    uint32_t number_of_files;
    uint32_t cursor;
    void *ptr;
};

struct dir_entry_t{
    char name[13];
    uint32_t is_readonly;
    uint32_t is_hidden;
    size_t size;
    uint32_t is_system;
    uint32_t is_directory;
    uint32_t is_archived;
};
struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);
#endif //FAT_FILE_READER_H
