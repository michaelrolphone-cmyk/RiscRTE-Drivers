"""Stage a pinned SPIFFS implementation as an explicit IDF component override.

The ESP-IDF checkout is never edited. The wrapper is still the pinned IDF
component; the independently identified SPIFFS source supplies validated hints.
"""
import hashlib,json,shlex,subprocess
from pathlib import Path

def git(root,*args):
    return subprocess.check_output(['git','-C',str(root),*args])
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def stage(idf,source,expected,out,entries=64):
    if entries != 64:raise ValueError('Selected bounded lookup profile must use64 entries')
    head=git(source,'rev-parse','HEAD').decode().strip()
    if head!=expected or git(source,'status','--porcelain').strip():
        raise ValueError('SPIFFS source must be the exact clean selected commit')
    base=git(idf,'ls-tree','HEAD','components/spiffs/spiffs').decode().split()[2]
    subprocess.run(['git','-C',str(source),'merge-base','--is-ancestor',base,head],check=True)
    if out.exists():raise ValueError('Preserve previous component staging; use a fresh output')
    out.mkdir(parents=True)
    wrapper={}
    paths=git(idf,'ls-tree','-r','--name-only','HEAD','components/spiffs').decode().splitlines()
    for name in paths:
        rel=Path(name).relative_to('components/spiffs')
        if len(rel.parts)>1 and rel.parts[0]!='include':continue
        if rel.parts[0]=='spiffs':continue
        data=git(idf,'show','HEAD:'+name);p=out/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        wrapper[str(rel)]=digest(p)
    sources={}
    paths=git(source,'ls-tree','-r','--name-only',head,'src','LICENSE').decode().splitlines()
    for name in paths:
        rel=Path(name)
        if rel.is_absolute() or '..' in rel.parts:raise ValueError('Invalid tracked source path')
        p=out/'spiffs'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(git(source,'show',head+':'+name));sources[str(rel)]=digest(p)
    p=out/'CMakeLists.txt'
    p.write_text(p.read_text()+'\n# Product-selected bounded validated-name hints; all ABI consumers share size.\n'
                 'target_compile_definitions(${COMPONENT_LIB} PUBLIC SPIFFS_NAME_HINT_ENTRIES=64)\n')
    receipt={'enabled':True,'entries':entries,'idf_base':git(idf,'rev-parse','HEAD').decode().strip(),
             'spiffs_base':base,'spiffs_commit':head,'spiffs_tree':git(source,'rev-parse','HEAD^{tree}').decode().strip(),
             'component_path':str(out),'idf_wrapper_sha256':wrapper,'spiffs_sources_sha256':sources,
             'staged_sha256':{str(p.relative_to(out)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
             'ordinary_name_validation_retained':True,'physical_verified':False}
    return receipt

def verify_compiled(build,receipt):
    root=Path(receipt['component_path']).resolve()
    for name,value in receipt['staged_sha256'].items():
        if digest(root/name)!=value:raise ValueError('Selected SPIFFS source changed during build')
    rows=json.loads((build/'compile_commands.json').read_text())
    expected={'esp_spiffs.c','spiffs_api.c','spiffs/src/spiffs_cache.c','spiffs/src/spiffs_check.c',
              'spiffs/src/spiffs_gc.c','spiffs/src/spiffs_hydrogen.c','spiffs/src/spiffs_nucleus.c'}
    found=set()
    for row in rows:
        p=Path(row['file']).resolve()
        if p.is_relative_to(root):
            rel=str(p.relative_to(root));found.add(rel)
            args=row.get('arguments') or shlex.split(row['command'])
            definitions=[x for x in args if x.startswith(('-DSPIFFS_NAME_HINT_ENTRIES','-USPIFFS_NAME_HINT_ENTRIES'))]
            if definitions!=['-DSPIFFS_NAME_HINT_ENTRIES=64']:
                raise ValueError('Inconsistent SPIFFS struct-size selection: '+rel)
        elif p.name in {'esp_spiffs.c','spiffs_api.c','spiffs_cache.c','spiffs_check.c','spiffs_gc.c','spiffs_hydrogen.c','spiffs_nucleus.c'}:
            raise ValueError('Unexpected second SPIFFS implementation in link')
    if found!=expected:raise ValueError('Incomplete selected SPIFFS component: '+str(found))
    return sorted(found)
