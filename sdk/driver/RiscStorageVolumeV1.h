#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_VOLUME_API_V1 1u
#define RISC_STORAGE_VOLUME_NAME_MAX 128u
typedef uint32_t risc_storage_dir_t;
typedef uint32_t risc_storage_file_t;
#define RISC_STORAGE_DIR_INVALID 0u
#define RISC_STORAGE_FILE_INVALID 0u
typedef struct {
    char name[RISC_STORAGE_VOLUME_NAME_MAX];
    uint64_t size;
    uint8_t is_directory;
} risc_storage_dirent_v1;
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    bool (*refresh)(void *);
    bool (*ready)(void *);
    bool (*label)(void *,char *,size_t);
    bool (*stat)(void *,const char *,uint64_t *,bool *);
    risc_storage_dir_t (*dir_open)(void *,const char *);
    bool (*dir_next)(void *,risc_storage_dir_t,risc_storage_dirent_v1 *);
    void (*dir_close)(void *,risc_storage_dir_t);
    risc_storage_file_t (*file_open_read)(void *,const char *,uint64_t *);
    size_t (*file_read)(void *,risc_storage_file_t,void *,size_t);
    risc_storage_file_t (*file_open_write)(void *,const char *);
    size_t (*file_write)(void *,risc_storage_file_t,const void *,size_t);
    bool (*file_close)(void *,risc_storage_file_t,bool);
    bool (*remove)(void *,const char *);
    bool (*last_error)(void *,char *,size_t);
} risc_storage_volume_api_v1;
#ifdef __cplusplus
}
#endif
