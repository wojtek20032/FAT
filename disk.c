//
// Created by macha on 03.12.2023.
//

#include <errno.h>
#include <stdlib.h>
#include "file_reader.h"


struct disk_t *disk_open_from_file(const char *volume_file_name) {
    if (!volume_file_name) {
        errno = EFAULT;
        return NULL;
    }
    struct disk_t *res = malloc(sizeof(struct disk_t));
    if (res == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    res->disk = fopen(volume_file_name, "rb");
    if (!res->disk) {
        free(res);
        errno = ENOENT;
        return NULL;
    }
    uint8_t buffer[SEC_SIZE];
    uint32_t count = 0;
    for (;;) {
        if (fread(buffer, 1, 1, res->disk)) {
            count++;
        } else {
            break;
        }
    }
    res->num_sectors = (int) ((float) count / 512);

    return res;
}

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read) {
    if (!pdisk || !buffer) {
        errno = EFAULT;
        return -1;
    }
    if (first_sector < 0 || first_sector > pdisk->num_sectors || sectors_to_read + first_sector > pdisk->num_sectors) {
        errno = ERANGE;
        return -1;
    }
    fseek(pdisk->disk, first_sector * SEC_SIZE, SEEK_SET);
    fread(buffer, SEC_SIZE, sectors_to_read, pdisk->disk);
    return sectors_to_read;
}

int disk_close(struct disk_t *pdisk) {
    if (!pdisk) {
        errno = EFAULT;
        return -1;
    }
    fclose(pdisk->disk);
    free(pdisk);
    return 0;
}

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector) {
    if (!pdisk) {
        errno = EFAULT;
        return NULL;
    }
    struct volume_t *res = malloc(sizeof(struct volume_t));
    if (!res) {
        free(res);
        errno = ENOMEM;
        return NULL;
    }
    if (disk_read(pdisk, (int) first_sector, &res->info, 1) == -1) {
        free(res);
        return NULL;
    }
    if (res->info.Signature_value != 0xaa55) {
        free(res);
        errno = EINVAL;
        return NULL;
    }
    if (res->info.Extended_boot != 0x28 && res->info.Extended_boot != 0x29) {
        free(res);
        errno = EINVAL;
        return NULL;
    }
    if (res->info.Number_of_fats != 1 && res->info.Number_of_fats != 2) {
        free(res);
        errno = EINVAL;
        return NULL;
    }
    res->FAT_1 = malloc(res->info.bytes_per_sec * res->info.size_of_each_fat);
    if (!res->FAT_1) {
        free(res);
        errno = ENOMEM;
        return NULL;
    }

    res->root_dir = malloc(sizeof(struct SFN) * res->info.Max_numbers_of_files);
    if (!res->root_dir) {
        free(res->FAT_1);
        free(res);
        errno = ENOMEM;
        return NULL;
    }
    void *checker = malloc(res->info.bytes_per_sec * res->info.size_of_each_fat);
    if (!checker) {
        free(res->FAT_1);
        free(res);
        errno = ENOMEM;
        return NULL;
    }
    res->str_disk = pdisk;

    disk_read(pdisk, res->info.size_of_reserved, res->FAT_1, res->info.size_of_each_fat);
    disk_read(pdisk, res->info.size_of_reserved + res->info.size_of_each_fat, checker, res->info.size_of_each_fat);
    size_t check = 0;
    for (size_t i = 0; i < res->info.bytes_per_sec * res->info.size_of_each_fat; i++) {
        if (*((char *) res->FAT_1 + i) == *((char *) checker + i)) {
            continue;
        }
        check = 1;
    }
    if (check != 0) {
        free(res->FAT_1);
        free(checker);
        free(res->root_dir);
        free(res);
        errno = EINVAL;
        return NULL;
    }
    disk_read(pdisk, res->info.size_of_reserved + 2 * res->info.size_of_each_fat, res->root_dir,
              (int) sizeof(struct SFN) * res->info.Max_numbers_of_files / res->info.bytes_per_sec);
    free(checker);
    return res;
}

int fat_close(struct volume_t *pvolume) {
    if (!pvolume) {
        errno = EFAULT;
        return -1;
    }
    free(pvolume->FAT_1);
    free(pvolume->root_dir);
    free(pvolume);
    return 0;
}

uint16_t glue_two_bytes(uint8_t left, uint8_t right, int odd) {
    uint16_t res = 0;
    if (odd == 1) {
        res += left >> 4;
        res += right << 4;
    } else {
        right = right << 4;
        right = right >> 4;
        res += left;
        res += right << 8;
    }
    return res;
}

struct clusters_chain_t *get_chain_fat12(const void *const buffer, size_t size, uint16_t first_cluster) {
    if (!buffer || size <= 0) {
        return NULL;
    }
    uint16_t num_clusters = (size * 2) / 3;
    if (first_cluster / 3 * 2 > num_clusters) {
        return NULL;
    }
    struct clusters_chain_t *chain = malloc(1 * sizeof(struct clusters_chain_t));
    if (!chain) {
        return NULL;
    }
    uint8_t left, right;
    uint16_t res;
    left = *((uint8_t *) buffer + first_cluster * 3 / 2);
    right = *((uint8_t *) buffer + (first_cluster * 3 / 2) + 1);
    res = glue_two_bytes(left, right, (first_cluster % 2));
    chain->size = 1;
    for (; res != 0xff8 && res != 0xfff;) {
        if (res >= num_clusters || chain->size > num_clusters) {
            errno = 1;
            break;
        }
        left = *((uint8_t *) buffer + res * 3 / 2);
        right = *((uint8_t *) buffer + (res * 3 / 2) + 1);
        res = glue_two_bytes(left, right, res % 2);
        chain->size++;
    }
    chain->clusters = malloc(chain->size * sizeof(uint16_t));
    if (!chain->clusters) {
        free(chain);
        return NULL;
    }
    chain->clusters[0] = first_cluster;
    left = *((uint8_t *) buffer + first_cluster * 3 / 2);
    right = *((uint8_t *) buffer + (first_cluster * 3 / 2) + 1);
    res = glue_two_bytes(left, right, (first_cluster % 2));
    for (size_t i = 1; i < chain->size; i++) {
        chain->clusters[i] = res;
        left = *((uint8_t *) buffer + res * 3 / 2);
        right = *((uint8_t *) buffer + (res * 3 / 2) + 1);
        res = glue_two_bytes(left, right, res % 2);
    }
    return chain;
}

struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {
    if (!pvolume || !file_name) {
        errno = EFAULT;
        return NULL;
    }
    struct SFN *root = pvolume->root_dir;
    struct file_t *File = malloc(sizeof(struct file_t));
    if (!File) {
        errno = ENOMEM;
        return NULL;
    }
    size_t count_root = 0;
    char Name[11] = "           ";
    int counter = 0;
    int arg = 0;
    for (size_t i = 0; file_name[i] != '.'; i++) {
        if (file_name[i] == '\0') {
            arg = 1;
            break;
        }
        Name[i] = file_name[i];
        counter++;
    }
    int z = 0;
    if (arg == 0) {
        for (int i = 8; i < 11; i++, z++) {
            if (file_name[counter + 1 + z] == '\0') {
                break;
            }
            Name[i] = file_name[counter + 1 + z];
        }
    }
    int flag = 0;
    for (unsigned short i = 0; i < pvolume->info.Max_numbers_of_files; ++i) {
        int same = 1;
        for (int j = 0; j < 11; ++j) {
            if (root->file_name[j] != Name[j]) {
                same = 0;
                break;
            }
        }
        if (same == 1) {
            if ((root->file_attri & 0x10) >> 4 == 1) {
                free(File);
                errno = EISDIR;
                return NULL;
            }
            flag = 1;
            File->root_num = count_root;
            break;
        } else {
            root++;
            count_root++;
        }
    }
    if (flag == 0) {
        free(File);
        errno = ENOENT;
        return NULL;
    }

    File->cursor = 0;
    File->vol = pvolume;
    File->data_start = 0 + pvolume->info.size_of_reserved;
    File->data_start += pvolume->info.size_of_each_fat * pvolume->info.Number_of_fats;
    File->data_start += pvolume->info.Max_numbers_of_files * (int) sizeof(struct SFN) / pvolume->info.bytes_per_sec;
    File->chain = get_chain_fat12(pvolume->FAT_1, pvolume->info.size_of_each_fat * pvolume->info.bytes_per_sec,
                                  root->low_order_first_cluster);
    if (!File->chain) {
        free(File);
        errno = ENOMEM;
        return NULL;
    }

    return File;
}

int file_close(struct file_t *file) {
    if (!file) {
        errno = EFAULT;
        return -1;
    }
    free(file->chain->clusters);
    free(file->chain);
    free(file);
    return 0;
}

int32_t file_seek(struct file_t *stream, int32_t offset, int whence) {
    if (!stream) {
        errno = EFAULT;
        return -1;
    }
    struct SFN *temp = stream->vol->root_dir;
    for (size_t i = 0; i < stream->root_num; ++i) {
        temp++;
    }
    switch (whence) {
        case SEEK_SET:
            stream->cursor = offset;
            break;
        case SEEK_CUR:
            stream->cursor += offset;
            break;
        case SEEK_END:
            stream->cursor = temp->file_size + offset;
        default:
            errno = EINVAL;
            return -1;
    }
    return 1;
}

int get_sector_for_cluster(struct file_t *temp, int num_of_cluster) {
    if (!temp) {
        return -999;
    }
    int sector = 0;
    sector += temp->data_start;
    sector += (temp->chain->clusters[num_of_cluster] - 2) * temp->vol->info.sector_per_cluster;
    return sector;
}

int return_cluster_info(struct file_t *stream, char *info) {
    if (strcmp("pos", info) == 0) {
        return stream->cursor % (stream->vol->info.bytes_per_sec * stream->vol->info.sector_per_cluster);
    } else if (strcmp("size", info) == 0) {
        return stream->vol->info.bytes_per_sec * stream->vol->info.sector_per_cluster;
    } else if (strcmp("num", info) == 0) {
        return stream->cursor / (stream->vol->info.bytes_per_sec * stream->vol->info.sector_per_cluster);
    }
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (!ptr || !stream) {
        errno = EFAULT;
        return -1;
    }

    size_t count = 0;
    int left = 0;
    int main_pointer = 0;
    char *buffor = malloc(sizeof(char) * return_cluster_info(stream, "size"));
    if (buffor == NULL) {
        errno = ENOMEM;
        return -1;
    }
    struct SFN *temp = stream->vol->root_dir;
    for (size_t i = 0; i < stream->root_num; ++i) {
        temp++;
    }
    int last = -1;
    while (count < nmemb) {
        if (stream->cursor > temp->file_size - 1) {
            break;
        }
        int num_of_cluster = return_cluster_info(stream, "num");
        int sec_of_num_cluster = get_sector_for_cluster(stream, num_of_cluster);
        int error = 0;
        if(last != sec_of_num_cluster) {
            error = disk_read(stream->vol->str_disk, sec_of_num_cluster, buffor,
                                  stream->vol->info.sector_per_cluster);
        }
        last = sec_of_num_cluster;
        if (error == -1) {
            errno = ERANGE;
            free(buffor);
            free(temp);
            return -1;
        }
        if (left) {
            int cluster_pos = return_cluster_info(stream, "pos");
            *((char *) ptr + main_pointer) = *(buffor + cluster_pos);
            main_pointer += 1;
            stream->cursor += 1;
            left--;
            if (left == 0) {
                count += 1;
            }
        } else {
            left = (int) size;
        }

    }
    free(buffor);
    return count;
}

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path) {
    if (!pvolume || !dir_path) {
        errno = EFAULT;
        return NULL;
    }
    struct dir_t *dir = malloc(sizeof(struct dir_t));
    if (!dir) {
        errno = ENOMEM;
        return NULL;
    }
    if (strcmp("\\", dir_path) == 0) {
        dir->cursor = 0;
        dir->number_of_files = pvolume->info.Max_numbers_of_files;
        dir->ptr = pvolume->root_dir;
        return dir;
    } else {
        free(dir);
        errno = ENOENT;
        return NULL;
    }

}

void convertSFNtoNormalString(const char *sfn, char *normal) {
    int i, j;
    for (i = 0; i < 8 && sfn[i] != ' '; ++i) {
        normal[i] = sfn[i];
    }
    if (sfn[8] != ' ') {
        normal[i++] = '.';

        for (j = 8; j < 11 && sfn[j] != ' '; ++i, ++j) {
            normal[i] = sfn[j];
        }
    }
    normal[i] = '\0';
}

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry) {
    if (!pdir || !pentry) {
        errno = EFAULT;
        return 0;
    }
    struct SFN *temp = pdir->ptr;
    temp += pdir->cursor;
    int flag = 0;
    unsigned int i = pdir->cursor;
    for (; i < pdir->number_of_files; ++i) {
        pdir->cursor++;
        if (*((uint8_t *) temp->file_name) != 0x00 && *((uint8_t *) temp->file_name) != 0xe5) {
                flag = 1;
                break;
        }
        temp++;
    }
    if (flag == 0) {
        return 1;
    }

    pentry->size = pdir->number_of_files;
    convertSFNtoNormalString(temp->file_name, pentry->name);
    if ((temp->file_attri & 0x20) >> 5 == 1) {
        pentry->is_archived = 1;
    } else {
        pentry->is_archived = 0;
    }

    if ((temp->file_attri & 0x10) >> 4 == 1) {
        pentry->is_directory = 1;
    } else {
        pentry->is_directory = 0;
    }

    if ((temp->file_attri & 2) >> 1 == 1) {
        pentry->is_hidden = 1;
    } else {
        pentry->is_hidden = 0;
    }

    if ((temp->file_attri & 1) >> 0 == 1) {
        pentry->is_readonly = 1;
    } else {
        pentry->is_readonly = 0;
    }

    if ((temp->file_attri & 4) >> 2 == 1) {
        pentry->is_system = 1;
    } else {
        pentry->is_system = 0;
    }

    return 0;
}

int dir_close(struct dir_t *pdir) {
    if (!pdir) {
        errno = EFAULT;
        return 0;
    }
    free(pdir);
    return 0;
}