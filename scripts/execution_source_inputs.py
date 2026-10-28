"""Strict execution input validation, including the one reproducible mapper input."""
import hashlib
import json
import tempfile
from pathlib import Path
from badge_host_mapper import stage_runtime_mapper

def require(ok, message):
    if not ok: raise ValueError(message)

def validated_inputs(receipt, allow_changed=()):
    sources=receipt.get('source_sha256',{})
    require(bool(sources),'Missing execution source provenance')
    mapper=receipt.get('host_mapper_staging')
    generated={}
    if mapper is not None:
        require(isinstance(mapper,dict),'Invalid mapper staging receipt')
        helper=Path(__file__).with_name('badge_host_mapper.py').resolve()
        helper_inputs=[p for p in sources if Path(p).resolve().parts[-2:]==('scripts','badge_host_mapper.py')]
        helper_digest=hashlib.sha256(helper.read_bytes()).hexdigest()
        require(len({str(Path(p).resolve()) for p in helper_inputs})==1 and all(sources[p]==helper_digest for p in helper_inputs),'Mapper generator source mismatch')
        source=Path(mapper.get('selected_source',''))
        require(source.parts[-4:]==('test','drivers','stubs','esp_dlfcn.h'),'Unexpected mapper source')
        require(hashlib.sha256(source.read_bytes()).hexdigest()==mapper.get('selected_source_sha256'),'Mapper selected source changed')
        matches=[p for p in sources if Path(p).parts[-2:]==('host-mapper','esp_dlfcn.h')]
        require(len(matches)==1,'Expected exactly one generated mapper input')
        with tempfile.TemporaryDirectory(prefix='badge-package-mapper-') as tmp:
            output=stage_runtime_mapper(source.parents[3],Path(tmp)/'host-mapper')
            rebuilt=json.loads((output/'staging.json').read_text())
            require(rebuilt==mapper,'Mapper reconstruction recipe mismatch')
            data=(output/'esp_dlfcn.h').read_bytes()
        path=matches[0];digest=hashlib.sha256(data).hexdigest()
        require(digest==sources[path]==mapper.get('staged_sha256'),'Mapper generated digest mismatch')
        if Path(path).exists():
            require(Path(path).read_bytes()==data,'Existing mapper input changed')
        generated[path]=(data,('generated-host-mapper',str(source.resolve()),digest))
    result={}
    for path,digest in sources.items():
        if path in generated:data,key=generated[path]
        else:data,key=Path(path).read_bytes(),str(Path(path).resolve())
        require(path in allow_changed or hashlib.sha256(data).hexdigest()==digest,'Execution source changed: '+path)
        require(key not in result or result[key]==digest,'Conflicting canonical execution input')
        result[key]=digest
    return result
