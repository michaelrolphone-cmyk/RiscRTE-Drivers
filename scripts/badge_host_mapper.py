"""Stage the selected Runtime host mapper with TMPDIR-aware secure copies only."""
import hashlib
import json
from pathlib import Path

_HELPER = r'''
// ProviderModuleV2 includes this shim inside extern C; STL stays C++ linkage.
extern "C++" {
#include <string>
#include <cerrno>
inline void badge_mapper_error(const char* operation) {
  const int saved=errno;
  std::fprintf(stderr,"BADGE_HOST_MAPPER operation=%s errno=%d detail=%s\n",operation,saved,std::strerror(saved));
  errno=saved;
}
inline int badge_mapper_temp(std::string& path,const char* stem) {
  const char* directory=std::getenv("TMPDIR");
  if(!directory||!*directory)directory="/tmp";
  path=directory;if(path.back()!='/')path+='/';path+=stem;path+="XXXXXX";
  int fd=mkstemp(&path[0]);
  if(fd<0)badge_mapper_error("mkstemp");
  return fd;
}
} // extern C++
'''

def stage_runtime_mapper(runtime, output):
    runtime=Path(runtime).resolve();output=Path(output)
    source=runtime/'test/drivers/stubs/esp_dlfcn.h'
    original=source.read_text();text=original;changes=[]
    def replace(before, after):
        nonlocal text
        assert text.count(before)==1, 'Selected Runtime mapper contract changed: '+before
        text=text.replace(before,after);changes.append((before,after))
    replace('#include <unistd.h>', '#include <unistd.h>\n'+_HELPER)
    replace('char tmp[]="/tmp/riscrte-instance-XXXXXX"; int fd=mkstemp(tmp);',
            'std::string tmp; int fd=badge_mapper_temp(tmp,"riscrte-instance-");')
    replace('char tmp[]="/tmp/riscrte-provider-XXXXXX";int fd=mkstemp(tmp);',
            'std::string tmp;int fd=badge_mapper_temp(tmp,"riscrte-provider-");')
    # Each original call appears twice, one for file copies and one for snapshots.
    for before,after in [('dlopen(tmp,','dlopen(tmp.c_str(),'),('unlink(tmp);','unlink(tmp.c_str());')]:
        assert text.count(before)==2
        text=text.replace(before,after);changes.append((before,after))
    replace('#include "../../../lib/elf_loader/include/esp_elf_image.h"',
            '#include '+json.dumps(str(runtime/'lib/elf_loader/include/esp_elf_image.h')))
    replace('fclose(in);\n  void* result=ok?', 'fclose(in);\n  if(!ok)badge_mapper_error("instance-copy");\n  void* result=ok?')
    replace('std::free(file.payload);void* result=ok?', 'std::free(file.payload);if(!ok)badge_mapper_error("provider-copy");void* result=ok?')
    restored=text
    for before,after in reversed(changes):restored=restored.replace(after,before)
    assert restored==original,'Staging changed non-temporary mapper semantics'
    output.mkdir(parents=True,exist_ok=True);header=output/'esp_dlfcn.h';header.write_text(text)
    receipt={'selected_source':str(source),'selected_source_sha256':hashlib.sha256(original.encode()).hexdigest(),
             'staged_sha256':hashlib.sha256(text.encode()).hexdigest(),'source_equivalent_outside_recorded_substitutions':True,
             'changes':changes,'scope':'Host-only TMPDIR temporary copy placement and failure diagnostics; Runtime source/target locks unchanged; mkstemp0600 uniqueness/unlink/payload ownership preserved.'}
    (output/'staging.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return output
