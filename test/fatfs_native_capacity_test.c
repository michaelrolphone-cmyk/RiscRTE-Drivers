/* Real pinned FatFs, deterministic RAM sectors only; no formatting API. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
static uint8_t*media;static unsigned sectors,reads,writes,checkpoints,limit;
int risc_fatfs_checkpoint(void){return ++checkpoints<=limit;}
DSTATUS disk_initialize(BYTE p){return p?STA_NOINIT:0;}
DSTATUS disk_status(BYTE p){return disk_initialize(p);}
DRESULT disk_read(BYTE p,BYTE*b,LBA_t at,UINT n){if(p||at+n>sectors)return RES_PARERR;++reads;memcpy(b,media+(size_t)at*512,n*512);return RES_OK;}
DRESULT disk_write(BYTE p,const BYTE*b,LBA_t at,UINT n){(void)p;(void)b;(void)at;(void)n;++writes;return RES_ERROR;}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void*b){(void)p;(void)b;return cmd==CTRL_SYNC?RES_OK:RES_PARERR;}
DWORD get_fattime(void){return 0;}
static void put16(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;}
static void setfat(FATFS*fs,unsigned entry,uint32_t value){
 for(unsigned f=0;f<fs->n_fats;++f){uint8_t*p=media+(size_t)(fs->fatbase+f*fs->fsize)*512;
  if(fs->fs_type==FS_FAT16)put16(p+entry*2,value);
  else{unsigned i=entry+entry/2;if(entry&1){p[i]=(p[i]&15)|(value<<4);p[i+1]=value>>4;}else{p[i]=value;p[i+1]=(p[i+1]&240)|((value>>8)&15);}}
}
}
static void run(unsigned type){
 sectors=type==12?2048:8192;media=calloc(sectors,512);assert(media);uint8_t*b=media;
 b[0]=0xeb;b[1]=0x3c;b[2]=0x90;memcpy(b+3,"MSDOS5.0",8);put16(b+11,512);b[13]=1;put16(b+14,1);b[16]=2;put16(b+17,512);put16(b+19,sectors);b[21]=0xf8;put16(b+22,type==12?6:32);put16(b+24,32);put16(b+26,64);b[38]=0x29;memcpy(b+54,type==12?"FAT12   ":"FAT16   ",8);b[510]=0x55;b[511]=0xaa;
 for(unsigned f=0;f<2;++f){uint8_t*p=media+(size_t)(1+f*(type==12?6:32))*512;p[0]=0xf8;p[1]=p[2]=0xff;if(type==16)p[3]=0xff;}
 FATFS fs,*got=NULL;memset(&fs,0,sizeof fs);limit=100000;checkpoints=reads=writes=0;assert(f_mount(&fs,"",1)==FR_OK&&fs.fs_type==(type==12?FS_FAT12:FS_FAT16));
 fs.free_clst=0;unsigned before=reads;DWORD count=0;
 assert(f_getfree_bound("",12,1,&count,&got)==FR_OK&&got==&fs&&count==12&&reads>before&&!writes&&fs.free_clst==0);
 for(unsigned i=2;i<fs.n_fatent;++i)setfat(&fs,i,i<13?0:0xffff);
 fs.free_clst=fs.n_fatent-2;before=reads;checkpoints=0;
 assert(f_getfree_bound("",12,1,&count,&got)==FR_OK&&count==11&&reads>before&&checkpoints>=fs.n_fatent-2&&!writes);
 /* A fixed root with no vacant slot cannot grow into free data clusters. */
 for(unsigned i=0;i<fs.n_rootdir;++i)media[(size_t)fs.dirbase*512+i*32]='A';
 assert(f_getfree_bound("",1,1,&count,&got)==FR_OK&&count==0&&!writes);
 assert(f_getfree_bound("",1,0,&count,&got)==FR_OK&&count==1&&!writes);
 media[(size_t)fs.dirbase*512+7*32]=0xe5;
 assert(f_getfree_bound("",1,1,&count,&got)==FR_OK&&count==1&&!writes);
 fs.wflag=1;before=reads;assert(f_getfree_bound("",1,0,&count,&got)==FR_LOCKED&&!count&&reads==before&&!writes);fs.wflag=0;
 limit=1;checkpoints=0;assert(f_getfree_bound("",12,0,&count,&got)==FR_DISK_ERR&&!count&&!writes);
 assert(f_mount(NULL,"",0)==FR_OK);free(media);printf("FAT%u fresh/optimistic-pessimistic cache/root slots/dirty refusal/checkpoint budget PASS\n",type);
}
int main(void){run(12);run(16);}
