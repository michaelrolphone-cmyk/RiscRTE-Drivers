/* Included by the shared volume owner, under its existing serialized guard. */
#include <RiscStorageVolumeFsV1.h>
static bool fs_dir_tell(void* c,uint32_t handle,uint64_t* position){
 (void)c;if(!position||!enter_ready())return false;
 dir_slot* slot=dir_for(handle);bool ok=slot&&!slot->error;
 if(ok)*position=slot->object.dptr;
 return leave()&&ok;
}
static bool fs_dir_seek(void* c,uint32_t handle,uint64_t position){
 (void)c;if(position>UINT32_MAX||!enter_ready())return false;
 dir_slot* slot=dir_for(handle);bool ok=false;
 if(slot&&!slot->error){FRESULT rc=f_seekdir(&slot->object,(DWORD)position);ok=result_ok(rc);if(!ok)slot->error=rc;}
 return leave()&&ok;
}
static uint64_t fs_modified(WORD date,WORD time){
 unsigned y=1980+(date>>9),m=(date>>5)&15,d=date&31;
 if(!m||m>12||!d||d>31)return 0;
 y-=m<=2;int era=y/400;unsigned yo=y-era*400,dy=(153*(m+(m>2?-3:9))+2)/5+d-1;
 int64_t days=(int64_t)era*146097+yo*365+yo/4-yo/100+dy-719468;
 return (uint64_t)(days*86400+(time>>11)*3600+((time>>5)&63)*60+(time&31)*2);
}
static int32_t fs_metadata(void* c,const char* path,risc_storage_metadata_v1* out){
 (void)c;if(!out||out->struct_size<sizeof(*out)||!valid_path(path))return RISC_STORAGE_FS_INVALID;
 if(!enter_ready())return RISC_STORAGE_FS_IO;
 FILINFO info;memset(&info,0,sizeof(info));FRESULT rc=FR_OK;
 if(equal(path,"/"))info.fattrib=AM_DIR;else rc=f_stat(path,&info);
 int32_t result=RISC_STORAGE_FS_IO;
 if(rc==FR_NO_FILE||rc==FR_NO_PATH)result=RISC_STORAGE_FS_NOT_FOUND;
 else if(result_ok(rc)){
  *out=(risc_storage_metadata_v1){.struct_size=sizeof(*out),.flags=info.fattrib&AM_DIR?RISC_STORAGE_DIRECTORY:0,.size=info.fsize,.modified_seconds=fs_modified(info.fdate,info.ftime)};
  if(out->modified_seconds)out->flags|=RISC_STORAGE_MODIFIED_VALID|RISC_STORAGE_MODIFIED_LOCAL;
  result=RISC_STORAGE_FS_OK;
 }
 return leave()?result:RISC_STORAGE_FS_IO;
}
static bool fs_truncate(void* c,uint32_t handle,uint64_t length){
 (void)c;if(length>UINT32_MAX||!enter_ready())return false;
 file_slot* slot=file_for(handle);bool ok=false;
 if(slot&&!slot->error&&(slot->flags&RISC_STORAGE_OPEN_WRITE)&&length<=f_size(&slot->object)){
  FSIZE_t previous=f_tell(&slot->object);FRESULT rc=f_lseek(&slot->object,(FSIZE_t)length);
  if(rc==FR_OK)rc=f_truncate(&slot->object);
  if(rc==FR_OK)rc=f_sync(&slot->object);
  if(rc==FR_OK&&previous<length)rc=f_lseek(&slot->object,previous);
  ok=result_ok(rc);if(!ok)slot->error=rc;
 }return leave()&&ok;
}
static bool fs_same_path(const char* a,const char* b){
 while(*a&&*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}return !*a&&!*b;
}
static bool fs_reserved(const char* p){size_t n=strlen(p);return n>=sizeof(".risc-replace")-1&&fs_same_path(p+n-(sizeof(".risc-replace")-1),".risc-replace");}
static bool fs_backup_name(const char* destination,char* out){
 static const char suffix[]=".risc-replace";size_t n=strlen(destination);
 if(n+sizeof(suffix)>RISC_STORAGE_VOLUME_PATH_MAX)return false;
 memcpy(out,destination,n);memcpy(out+n,suffix,sizeof(suffix));return true;
}
static bool fs_operand_free(const char* p){
 /* Reject aliases of open files through FatFs sharing checks too (FF_FS_LOCK). */
 for(unsigned i=0;i<FILE_SLOTS;i++)if(files[i].handle&&equal(files[i].path,p))return false;
 return !directory_in_use(p);
}
static bool fs_recover_locked(const char* destination,const char* backup){
 FILINFO old,live;FRESULT prior=f_stat(backup,&old);
 if(prior==FR_NO_FILE||prior==FR_NO_PATH)return true;
 if(!result_ok(prior)||old.fattrib&AM_DIR||!fs_operand_free(backup)||!fs_operand_free(destination))return false;
 FRESULT exists=f_stat(destination,&live);
 if(exists==FR_NO_FILE||exists==FR_NO_PATH)return result_ok(f_rename(backup,destination));
 if(!result_ok(exists)||live.fattrib&AM_DIR)return false;
 return result_ok(f_unlink(backup));
}
static bool fs_recover_replace(void* c,const char* destination){
 (void)c;char backup[RISC_STORAGE_VOLUME_PATH_MAX];
 if(!valid_path(destination)||fs_reserved(destination)||equal(destination,"/")||!fs_backup_name(destination,backup)||!enter_ready())return false;
 bool ok=fs_recover_locked(destination,backup);return leave()&&ok;
}
static bool fs_replace_file(void* c,const char* source,const char* destination){
 (void)c;char backup[RISC_STORAGE_VOLUME_PATH_MAX];
 if(!valid_path(source)||!valid_path(destination)||fs_reserved(source)||fs_reserved(destination)||fs_same_path(source,destination)||!fs_backup_name(destination,backup))return false;
 const char* a=strrchr(source,'/');const char* b=strrchr(destination,'/');
 if(!a||!b||a-source!=b-destination||memcmp(source,destination,(size_t)(a-source))||!enter_ready())return false;
 bool ok=false;FILINFO info;
 if(fs_operand_free(source)&&fs_operand_free(destination)&&fs_recover_locked(destination,backup)&&result_ok(f_stat(source,&info))&&!(info.fattrib&AM_DIR)){
  FRESULT before=f_stat(destination,&info);bool prior=before==FR_OK;
  if((!prior&&(before==FR_NO_FILE||before==FR_NO_PATH))||(prior&&!(info.fattrib&AM_DIR)&&result_ok(f_rename(destination,backup)))){
   FRESULT install=f_rename(source,destination);ok=result_ok(install);
   if(!ok&&prior&&!io_failed)(void)result_ok(f_rename(backup,destination));
   if(ok&&prior)ok=result_ok(f_unlink(backup));
  }
 }
 return leave()&&ok;
}
