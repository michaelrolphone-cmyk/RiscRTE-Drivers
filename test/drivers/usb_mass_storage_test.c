#include "RiscStorageVolumeV1.h"
#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define SECTORS 8192u
#define SECTOR_SIZE 512u
static uint8_t disk[SECTORS][SECTOR_SIZE];
static bool attached = true;
static bool claimed;
static uint32_t active_tag;
static uint32_t active_transfer;
static uint8_t active_flags;
static uint8_t active_cdb[16];
static uint8_t active_cdb_len;
static int stage; /* 0=cbw,1=data,2=csw */

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static void put_le16(uint8_t *p, uint16_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static void put_le32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put_be32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

static const uint8_t config[] = {
    9,2,32,0,1,1,0,0x80,50,
    9,4,0,0,2,0x08,0x06,0x50,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0,
};
static bool configuration(void *ctx, uint64_t device, uint8_t *bytes, size_t *length,
                          uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (!attached || device != 42 || !bytes || !length || *length < sizeof(config)) return false;
    memcpy(bytes, config, sizeof(config));
    *length = sizeof(config); *vid=0x0781; *pid=0x5567;
    return true;
}
static bool claim_interface(void *ctx, uint64_t device, uint8_t iface,
                            uint8_t alt, uint64_t *claim) {
    (void)ctx;
    if (!attached || claimed || device != 42 || iface != 0 || alt != 0 || !claim) return false;
    claimed = true; *claim = 77;
    return true;
}
static void release_interface(void *ctx, uint64_t token) {
    (void)ctx; assert(token==77); claimed=false; stage=0;
}
static int32_t control(void *ctx, uint64_t device, uint8_t request_type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *payload, uint16_t length,
                       uint32_t timeout) {
    (void)ctx; (void)value; (void)timeout;
    if (!claimed || device != 42 || request_type != 0xa1 || request != 0xfe ||
        index != 0 || !payload || length != 1) return -1;
    payload[0]=0;
    return 1;
}
static int32_t bulk_write(void *ctx, uint64_t claim, uint8_t endpoint,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx; (void)timeout;
    assert(claim == 77 && endpoint == 0x02 && src);
    if (stage == 0) {
        assert(length == 31 && le32(src) == 0x43425355u);
        active_tag = le32(src+4);
        active_transfer=le32(src+8);
        active_flags=src[12];
        active_cdb_len=src[14];
        assert(active_cdb_len && active_cdb_len<=16);
        memcpy(active_cdb, src+15, active_cdb_len);
        stage = active_transfer ? 1 : 2;
        return (int32_t)length;
    }
    assert(stage == 1 && !(active_flags & 0x80));
    assert(active_cdb[0] == 0x2a && length == 512 && active_transfer == 512);
    uint32_t lba = be32(active_cdb+2);
    assert(lba < SECTORS);
    memcpy(disk[lba], src, 512);
    stage=2;
    return (int32_t)length;
}
static int32_t bulk_read(void *ctx, uint64_t claim, uint8_t endpoint,
                         uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx; (void)timeout;
    assert(claim == 77 && endpoint == 0x81 && dst);
    if (stage == 1) {
        assert(active_flags & 0x80);
        if (active_cdb[0] == 0x25) {
            assert(capacity == 8 && active_transfer == 8);
            put_be32(dst, SECTORS-1);
            put_be32(dst+4, 512);
            stage=2;
            return 8;
        }
        assert(active_cdb[0] == 0x28 && capacity == 512 && active_transfer == 512);
        uint32_t lba=be32(active_cdb+2);
        assert(lba<SECTORS);
        memcpy(dst,disk[lba],512);
        stage=2;
        return 512;
    }
    assert(stage == 2 && capacity == 13);
    memset(dst,0,13);
    put_le32(dst,0x53425355u);
    put_le32(dst+4,active_tag);
    stage=0;
    return 13;
}
static bool poll_devices(void *ctx, size_t max_events, size_t *processed) {
    (void)ctx;
    assert(max_events && processed);
    *processed=0;
    return true;
}
static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx;
    assert(count);
    size_t required = attached ? 1u : 0u;
    if (*count < required || (required && !out)) {
        *count=required;
        return false;
    }
    if (required) out[0]=42;
    *count=required;
    return true;
}

static void format_fat16(void) {
    memset(disk,0,sizeof(disk));
    uint8_t *b=disk[0];
    b[0]=0xeb; b[1]=0x3c; b[2]=0x90;
    memcpy(b+3,"MSDOS5.0",8);
    put_le16(b+11,512);
    b[13]=1;
    put_le16(b+14,1);
    b[16]=1;
    put_le16(b+17,512);
    put_le16(b+19,SECTORS);
    b[21]=0xf8;
    put_le16(b+22,32);
    put_le16(b+24,32);
    put_le16(b+26,64);
    put_le32(b+28,0);
    put_le32(b+32,0);
    b[510]=0x55; b[511]=0xaa;
    uint8_t *fat=disk[1];
    put_le16(fat+0,0xfff8);
    put_le16(fat+2,0xffff);
    put_le16(fat+4,0xffff);
    const uint32_t root=33, data=65;
    uint8_t *e=disk[root];
    memcpy(e,"TEST    TXT",11);
    e[11]=0x20;
    put_le16(e+26,2);
    put_le32(e+28,5);
    memcpy(disk[data],"hello",5);
}

int main(int argc, char **argv) {
    assert(argc==2);
    format_fat16();
    void *elf=dlopen(argv[1],RTLD_NOW);
    assert(elf);
    risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(elf,"t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver=get(2);
    assert(driver && !strcmp(driver->driver_id,"usb-mass-storage") &&
           !strcmp(driver->capability_id,"storage.volume") &&
           driver->capability_api==1);
    const risc_storage_volume_api_v1 *volume=
        (const risc_storage_volume_api_v1*)driver->capability;
    assert(volume && volume->struct_size==sizeof(*volume));
    risc_usb_host_discovery_v1 h={
        {1,sizeof(h),NULL,configuration,claim_interface,release_interface,
         control,bulk_read,bulk_write},
        poll_devices,devices
    };
    risc_provider_dependency_v1 dep={"usb.host",1,&h.host};
    assert(!driver->start(NULL,0));
    assert(driver->start(&dep,1));
    assert(!driver->start(&dep,1));
    assert(volume->refresh(NULL));
    assert(volume->ready(NULL));
    assert(claimed);

    char label[32];
    assert(volume->label(NULL,label,sizeof(label)) && !strcmp(label,"USB Storage"));
    uint64_t size=0;
    bool is_dir=false;
    assert(volume->stat(NULL,"/TEST.TXT",&size,&is_dir) && size==5 && !is_dir);

    risc_storage_dir_t dir=volume->dir_open(NULL,"/");
    assert(dir);
    risc_storage_dirent_v1 ent;
    assert(volume->dir_next(NULL,dir,&ent));
    assert(!strcmp(ent.name,"TEST.TXT") && ent.size==5 && !ent.is_directory);
    volume->dir_close(NULL,dir);

    risc_storage_file_t f=volume->file_open_read(NULL,"/TEST.TXT",&size);
    assert(f && size==5);
    char small[8]={0};
    assert(volume->file_read(NULL,f,small,sizeof(small))==5 &&
           !memcmp(small,"hello",5));
    assert(volume->file_read(NULL,f,small,sizeof(small))==0);
    assert(volume->file_close(NULL,f,true));

    uint8_t payload[600];
    for (size_t i=0;i<sizeof(payload);++i) payload[i]=(uint8_t)(i*7u);
    f=volume->file_open_write(NULL,"/Copied File.bin");
    assert(f);
    assert(volume->file_write(NULL,f,payload,333)==333);
    assert(volume->file_write(NULL,f,payload+333,sizeof(payload)-333)==
           sizeof(payload)-333);
    assert(volume->file_close(NULL,f,true));
    assert(volume->stat(NULL,"/Copied File.bin",&size,&is_dir) &&
           size==600 && !is_dir);
    f=volume->file_open_read(NULL,"/Copied File.bin",&size);
    assert(f && size==600);
    uint8_t verify[600];
    size_t total=0, n;
    while ((n=volume->file_read(NULL,f,verify+total,sizeof(verify)-total))>0) total+=n;
    assert(total==600 && !memcmp(payload,verify,600));
    assert(volume->file_close(NULL,f,true));
    assert(volume->remove(NULL,"/Copied File.bin"));
    assert(!volume->stat(NULL,"/Copied File.bin",&size,&is_dir));

    f=volume->file_open_write(NULL,"/Abort.txt");
    assert(f);
    assert(volume->file_write(NULL,f,"discard",7)==7);
    assert(volume->file_close(NULL,f,false));
    assert(!volume->stat(NULL,"/Abort.txt",&size,&is_dir));

    attached=false;
    assert(volume->refresh(NULL));
    assert(!volume->ready(NULL));
    assert(!claimed);
    assert(driver->quiesce());
    driver->stop();
    assert(dlclose(elf)==0);
    puts("USB MSC BOT + FAT16 browse/read/write/delete/hotplug: PASS");
    return 0;
}
