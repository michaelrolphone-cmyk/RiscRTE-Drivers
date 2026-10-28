#!/usr/bin/env python3
"""Small real mapper tests; no full Runtime build."""
import argparse,ast,json,os,subprocess,tempfile
from pathlib import Path
from badge_host_mapper import stage_runtime_mapper
p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();results=[]
with tempfile.TemporaryDirectory(prefix='badge-mapper-proof-') as d:
 out=Path(d);stage=stage_runtime_mapper(a.runtime,out/'stage');lib=out/'tiny.so';src=out/'tiny.c';src.write_text('int mapped_value(void){return 42;}\n');subprocess.run(['cc','-fPIC','-shared',str(src),'-o',str(lib)],check=True)
 test=out/'proof.cpp';test.write_text(r'''
#include <cstdio>
#include <cerrno>
static bool fail_copy=false;
static size_t badge_write(const void*p,size_t s,size_t n,FILE*f){if(fail_copy){errno=ENOSPC;return 0;}return fwrite(p,s,n,f);}
#define fwrite badge_write
extern "C" {
#include "esp_dlfcn.h"
}
#undef fwrite
#include <cassert>
#include <sys/stat.h>
static int reader(void*,const char*path,elf_file_t*out){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);out->size=ftell(f);rewind(f);out->payload=(uint8_t*)malloc(out->size);assert(out->payload);assert(fread(out->payload,1,out->size,f)==out->size);fclose(f);return 0;}
int main(int argc,char**argv){assert(argc==3);assert(!setenv("TMPDIR",argv[2],1));
 std::string a,b;int one=badge_mapper_temp(a,"proof-");int two=badge_mapper_temp(b,"proof-");assert(one>=0&&two>=0&&a!=b&&a.rfind(argv[2],0)==0);struct stat st{};assert(!fstat(one,&st)&&(st.st_mode&0777)==0600);close(one);close(two);assert(!unlink(a.c_str())&&!unlink(b.c_str()));
 void*h=esp_dlopen_instance(argv[1]);assert(h);auto value=(int(*)())dlsym(h,"mapped_value");assert(value&&value()==42);dlclose(h);
 h=esp_dlopen_reader_instance(argv[1],reader,nullptr);assert(h);value=(int(*)())dlsym(h,"mapped_value");assert(value&&value()==42);dlclose(h);
 fail_copy=true;assert(!esp_dlopen_instance(argv[1]));assert(!esp_dlopen_reader_instance(argv[1],reader,nullptr));fail_copy=false;
 assert(!setenv("TMPDIR","/proc",1));assert(badge_mapper_temp(a,"forbidden-")<0);assert(!esp_dlopen_instance(argv[1]));assert(!esp_dlopen_reader_instance(argv[1],reader,nullptr));
 assert(!setenv("TMPDIR",argv[1],1));assert(badge_mapper_temp(a,"not-dir-")<0);
 assert(!setenv("TMPDIR","/nonexistent/badge-proof-dir",1));assert(badge_mapper_temp(a,"missing-")<0);
 unsetenv("TMPDIR");one=badge_mapper_temp(a,"proof-");assert(one>=0&&a.rfind("/tmp/",0)==0);close(one);unlink(a.c_str());
 setenv("TMPDIR","",1);one=badge_mapper_temp(a,"proof-");assert(one>=0&&a.rfind("/tmp/",0)==0);close(one);unlink(a.c_str());
 puts("PASS secure TMPDIR/fallback unique0600, exact file+reader mapping, copy/create failure, cleanup");
}
''')
 for san in (False,True):
  dest=out/('san-dir' if san else 'normal-dir');dest.mkdir();exe=out/'proof';command=['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(stage),*(['-fsanitize=address,undefined','-no-pie'] if san else []),str(test),'-ldl','-o',str(exe)];subprocess.run(command,check=True);r=subprocess.run([str(exe),str(lib),str(dest)],check=True,text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});assert not list(dest.iterdir());assert 'operation=mkstemp' in r.stderr and 'operation=instance-copy' in r.stderr and 'operation=provider-copy' in r.stderr;results.append({'sanitized':san,'stdout':r.stdout,'stderr':r.stderr,'cleanup':True});print(r.stdout,end='')
 # Compile the real consumer too: it wraps esp_dlfcn.h in extern C.
 provider=a.runtime/'src/runtime/drivers/ProviderModuleV2.cpp'
 command=['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-DRISC_APP_REQUIREMENT_ROWS=24','-I'+str(stage),*['-I'+str(a.runtime/x) for x in ('src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test','test/drivers/stubs','test/realtime_shim','test/retained_wake_shim')],'-fsyntax-only',str(provider)]
 subprocess.run(command,check=True)
 receipt=json.loads((stage/'staging.json').read_text());receipt['tests']=results;receipt['actual_provider_consumer_syntax']=True
 if a.output:a.output.write_text(json.dumps(receipt,indent=2)+'\n')
root=Path(__file__).resolve().parents[1]
for name in ('prototype','hybrid','settings','picker','wifi_gap','springboard','timecard'):
 text=(root/f'scripts/test_badge_{name}_execution.py').read_text();ast.parse(text)
 assert "'-I'+str(host_mapper),*['-I'+str(N/x)" in text
 assert "'host_mapper_staging':host_mapper_receipt" in text
print('PASS all seven runner AST/include precedence/receipt selection')
