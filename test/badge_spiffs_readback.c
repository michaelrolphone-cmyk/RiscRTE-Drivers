/* Offline read-only image mount using the pinned ESP-IDF SPIFFS implementation. */
#include "spiffs.h"
#include <inttypes.h>
static unsigned char *image;
static size_t image_size;
static unsigned writes;
void spiffs_api_lock(struct spiffs_t *fs) {(void)fs;}
void spiffs_api_unlock(struct spiffs_t *fs) {(void)fs;}
static s32_t read_image(spiffs *fs,u32_t addr,u32_t size,u8_t *dst) {
    (void)fs;
    if (addr > image_size || size > image_size - addr) return -1;
    memcpy(dst,image+addr,size); return SPIFFS_OK;
}
static s32_t write_image(spiffs *fs,u32_t addr,u32_t size,u8_t *src) {
    (void)fs;(void)addr;(void)size;(void)src;writes++;return -1;
}
static s32_t erase_image(spiffs *fs,u32_t addr,u32_t size) {
    (void)fs;(void)addr;(void)size;writes++;return -1;
}
int main(int argc,char **argv) {
    if (argc!=3 && argc!=5) return 2;
    const unsigned long expected_size=argc==5?strtoul(argv[3],NULL,0):0x1cf0000;
    const unsigned long expected_count=argc==5?strtoul(argv[4],NULL,0):12;
    if(!expected_count || expected_count>1024 || !expected_size || expected_size>0x2000000 || expected_size%4096)return 2;
    FILE *f=fopen(argv[1],"rb"); if(!f)return 3;
    if(fseek(f,0,SEEK_END))return 4;
    long length=ftell(f);if(length!=(long)expected_size)return 5;
    image_size=(size_t)length;rewind(f);image=malloc(image_size);
    if(!image || fread(image,1,image_size,f)!=image_size)return 6;
    fclose(f);
    spiffs fs={0};spiffs_config cfg={0};
    cfg.phys_size=image_size;cfg.phys_addr=0;cfg.phys_erase_block=4096;
    cfg.log_block_size=4096;cfg.log_page_size=512;
    cfg.hal_read_f=read_image;cfg.hal_write_f=write_image;cfg.hal_erase_f=erase_image;
    u8_t work[1024],fds[4096],cache[4096];
    s32_t result=SPIFFS_mount(&fs,&cfg,work,fds,sizeof fds,cache,sizeof cache,NULL);
    if(result!=SPIFFS_OK){fprintf(stderr,"mount failed: %d\n",(int)result);return 7;}
    spiffs_DIR dir;struct spiffs_dirent e;unsigned count=0;
    if(!SPIFFS_opendir(&fs,"/",&dir))return 8;
    while(SPIFFS_readdir(&dir,&e)) {
        const char *name=(const char *)e.name;
        if(name[0]!='/' || strstr(name,".."))return 9;
        char path[4096];if(snprintf(path,sizeof path,"%s%s",argv[2],name)>=(int)sizeof path)return 10;
        FILE *expected=fopen(path,"rb");if(!expected)return 11;
        spiffs_file fd=SPIFFS_open(&fs,name,SPIFFS_RDONLY,0);if(fd<0)return 12;
        unsigned total=0;u8_t actual[4096],wanted[4096];
        while(1){s32_t got=SPIFFS_read(&fs,fd,actual,sizeof actual);if(got<0)return 13;
            size_t n=fread(wanted,1,sizeof wanted,expected);
            if((size_t)got!=n || memcmp(actual,wanted,n))return 14;
            total+=n;if(!n)break;}
        if(total!=e.size)return 15;
        SPIFFS_close(&fs,fd);fclose(expected);count++;
        printf("verified %s %u bytes\n",name,total);
    }
    SPIFFS_closedir(&dir);SPIFFS_unmount(&fs);free(image);
    if(count!=expected_count || writes)return 16;
    printf("readback verified: %u files, zero writes, zero device operations\n",count);
    return 0;
}
