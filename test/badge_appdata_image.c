/* Offline provisioning/readback using the exact Runtime LittleFS C sources.
 * This formats only a caller-owned host image; never mounts or opens a device. */
#include "lfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned char image[0x100000];static bool readonly;static unsigned writes;
static int rd(const struct lfs_config*c,lfs_block_t b,lfs_off_t o,void*p,lfs_size_t n){assert(b<c->block_count&&o<=c->block_size&&n<=c->block_size-o);memcpy(p,image+b*c->block_size+o,n);return 0;}
static int wr(const struct lfs_config*c,lfs_block_t b,lfs_off_t o,const void*p,lfs_size_t n){++writes;if(readonly)return LFS_ERR_IO;assert(b<c->block_count&&o<=c->block_size&&n<=c->block_size-o);const unsigned char*v=p;for(unsigned i=0;i<n;i++){unsigned char*d=&image[b*c->block_size+o+i];assert((*d&v[i])==v[i]);*d=v[i];}return 0;}
static int erase(const struct lfs_config*c,lfs_block_t b){++writes;if(readonly)return LFS_ERR_IO;assert(b<c->block_count);memset(image+b*c->block_size,255,c->block_size);return 0;}
static int sync_(const struct lfs_config*c){(void)c;return 0;}
int main(int argc,char**argv){assert(argc==3);bool create=!strcmp(argv[1],"create");assert(create||!strcmp(argv[1],"verify"));
 struct lfs_config c={.read=rd,.prog=wr,.erase=erase,.sync=sync_,.read_size=128,.prog_size=128,.block_size=4096,.block_count=256,.block_cycles=512,.cache_size=512,.lookahead_size=128,.name_max=64};lfs_t fs;
 if(create){memset(image,255,sizeof(image));assert(!lfs_format(&fs,&c));FILE*f=fopen(argv[2],"wb");assert(f&&fwrite(image,1,sizeof(image),f)==sizeof(image)&&!fclose(f));}
 else{FILE*f=fopen(argv[2],"rb");assert(f&&fread(image,1,sizeof(image),f)==sizeof(image)&&fgetc(f)==EOF&&!fclose(f));}
 readonly=true;writes=0;assert(!lfs_mount(&fs,&c));assert(lfs_fs_size(&fs)>0);lfs_dir_t d;assert(!lfs_dir_open(&fs,&d,"/"));struct lfs_info i;unsigned count=0;int n;
 while((n=lfs_dir_read(&fs,&d,&i))>0){assert(!strcmp(i.name,".")||!strcmp(i.name,".."));count++;}assert(!n&&count==2);assert(!lfs_dir_close(&fs,&d));assert(!lfs_unmount(&fs)&&!writes);
 puts("PASS exact LittleFS: 1MiB, empty root, readonly replay, zero replay writes; no device operations");
}
