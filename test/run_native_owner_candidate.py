import sys,os,json,pathlib,hashlib,subprocess,traceback,zipfile,stat,shlex,re
if not __debug__ or sys.flags.optimize:raise RuntimeError("unoptimized Python required")
P=pathlib.Path
import argparse,platform
parser=argparse.ArgumentParser(description="Hosted actual-C native5 producer regression, not target/runtime authority")
for name in ("repo","core","cc","sdk","wheel","output"):
 parser.add_argument("--"+name,type=P,required=True)
args=parser.parse_args()
if platform.system()!="Darwin":raise RuntimeError("This preserved hosted C/SDK recipe is Darwin-only")
ROOT=args.repo.resolve(strict=True)
WORK=args.output.resolve();WORK.mkdir(parents=True,exist_ok=False)
H=lambda b:hashlib.sha256(b).hexdigest()
PRODUCT=args.core.resolve(strict=True)
CORE_COMMIT="ec86b26b2f06271c62405dc8976b07dae19b713f"
CORE_TREE="fc8f6da524a0bba27ec0d76361b22eaddca327cd"
CORE_URL="https://github.com/michaelrolphone-cmyk/RiscRTE.git"
CC=str(args.cc.resolve(strict=True));CCPIN=H(P(CC).read_bytes())
SYSROOT=str(args.sdk.resolve(strict=True))
if not P(SYSROOT).is_dir():raise RuntimeError("explicit host SDK directory required")
PY=str(P(sys.executable).resolve(strict=True));PYPIN=H(P(PY).read_bytes())
WHEEL="pyelftools-0.32-py3-none-any.whl"
URL="https://files.pythonhosted.org/packages/af/43/700932c4f0638c3421177144a2e86448c0d75dbaee2c7936bda3f9fd0878/pyelftools-0.32-py3-none-any.whl"
WPIN="013df952a006db5e138b1edf6d8a68ecc50630adbd0d83a2d41e7f846163d738"
CFILES=("esp_elf_validate.c","esp_privileged_imports.c","esp_privileged_manifest_imports.c")
HEADERS=("elf_types.h","esp_elf_data_layout.h","esp_elf_internal_v1.h","esp_privileged_imports.h","esp_privileged_manifest_imports.h")
INVENTORIES=tuple("privileged_os_cpu_symbols_v%d.def"%i for i in (1,2,5))+("privileged_public_libc_compat_v1.def","privileged_public_libc_compat_v2.def")
CLOSURE=tuple("lib/elf_loader/src/"+n for n in CFILES)+tuple("lib/elf_loader/include/private/"+n for n in HEADERS+INVENTORIES)
R={"status":"started","scope":"38 hosted additive Drivers producer cases against exact committed Core ec86; portable repository entry","commands":[],"input_hashes":{},"dependencies":{},"preflight":[],"errors":[],"target_execution":False,"runtime_admission":False,"physical_admission":False,"docker":False,"historical_receipt_pass_inherited":False}
def save():(WORK/"RESULT.json").write_text(json.dumps(R,sort_keys=True,indent=2)+"\n")
def check(p,pin=None,limit=300000000):
 p=P(p)
 if not p.is_file() or p.stat().st_size>limit:raise RuntimeError("missing/overlarge input: "+str(p))
 b=p.read_bytes();got=H(b)
 if pin and got!=pin:raise RuntimeError("pin mismatch: "+str(p))
 if str(p) in R["input_hashes"] and R["input_hashes"][str(p)]["sha256"]!=got:raise RuntimeError("observed input drift: "+str(p))
 R["input_hashes"][str(p)]={"sha256":got,"bytes":len(b),"realpath":str(p.resolve())};return b
def run(label,argv,env=None,timeout=120,required=True):
 row={"label":label,"argv":list(map(str,argv))};R["commands"].append(row);save()
 try:
  p=subprocess.run(row["argv"],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
 except subprocess.TimeoutExpired as e:
  row["timeout"]=True
  p=subprocess.CompletedProcess(row["argv"],124,e.stdout or b"",e.stderr or b"")
 except OSError as e:
  row["launch_error"]=repr(e)
  p=subprocess.CompletedProcess(row["argv"],125,b"",repr(e).encode())
 row["returncode"]=p.returncode
 for key,b in (("stdout",p.stdout),("stderr",p.stderr)):
  dest=WORK/(label+"."+key+".txt");dest.write_bytes(b)
  row[key]={"path":str(dest),"sha256":H(b),"bytes":len(b),"tail":b[-5000:].decode("utf-8","replace") if p.returncode else None}
 save()
 if p.returncode and required:raise RuntimeError(label+" failed, exit "+str(p.returncode))
 return p
try:
 supplied=json.loads((ROOT/"test/native_owner_candidate/SOURCE_PINS.json").read_text())
 expected_inputs=["scripts/native_owner_candidate/core-source.json","scripts/native_owner_candidate/core_binding.py","scripts/native_owner_candidate/generate_privileged_imports_v1.py","scripts/native_owner_candidate/generate_provider_package_inputs_v1.py","scripts/native_owner_candidate/package_admission.py","scripts/native_owner_candidate/package_candidate.py","scripts/native_owner_candidate/package_mapping.py","scripts/native_owner_candidate/package_preflight.c","scripts/native_owner_candidate/package_resource_paths.py","test/native_owner_candidate/elf_fixture.py","test/native_owner_candidate/run_cases.py","test/native_owner_candidate/test_package_binding.py","test/native_owner_candidate/test_package_candidate.py","test/native_owner_candidate/test_package_mapping.py","test/run_native_owner_candidate.py"]
 if set(supplied)!=set(expected_inputs):raise RuntimeError("exact repository-owned input set required")
 for name,pin in supplied.items():check(ROOT/name,pin)
 if run("core-head",["/usr/bin/git","-C",PRODUCT,"rev-parse","HEAD"]).stdout.decode().strip()!=CORE_COMMIT:raise RuntimeError("Core head drift")
 if run("core-tree",["/usr/bin/git","-C",PRODUCT,"rev-parse","HEAD^{tree}"]).stdout.decode().strip()!=CORE_TREE:raise RuntimeError("Core tree drift")
 if run("core-clean",["/usr/bin/git","-C",PRODUCT,"status","--porcelain"]).stdout:raise RuntimeError("Core not clean")
 sys.path.insert(0,str(ROOT/"scripts/native_owner_candidate"))
 import core_binding
 binding=core_binding.validate_core_source(PRODUCT);members=binding["files"]
 if set(members)!=set(CLOSURE):raise RuntimeError("Core closure mismatch")
 R["committed_core"]={"repository":CORE_URL,"commit":CORE_COMMIT,"tree":CORE_TREE,"binding":binding,"binding_sha256":core_binding.BINDING_SHA256}
 for name in CLOSURE:check(PRODUCT/name,members[name])
 check(CC,CCPIN);check(PY,PYPIN)
 R["compiler"]={"path":CC,"sha256":CCPIN};R["python"]={"path":PY,"sha256":PYPIN}
 env={"PATH":"/usr/bin:/bin:/usr/sbin:/sbin","LANG":"C","TMPDIR":str(WORK),"PYTHONDONTWRITEBYTECODE":"1"}
 run("compiler-version",[CC,"--version"],env)
 run("python-version",[PY,"-B","-c","import sys;print(sys.version);print(sys.flags.optimize);assert sys.flags.optimize==0"],env)
 # Exact official pyelftools wheel, supplied explicitly; no download/install here.
 wheel=WORK/WHEEL;wheel.write_bytes(check(args.wheel,WPIN,188525));check(wheel,WPIN,188525)
 deps=WORK/"dependencies";deps.mkdir()
 with zipfile.ZipFile(wheel) as z:
  rows=z.infolist()
  if len(rows)>1000 or sum(x.file_size for x in rows)>8*1024*1024:raise RuntimeError("wheel bounds")
  seen=set()
  for x in rows:
   rel=P(x.filename)
   if rel.is_absolute() or ".." in rel.parts or x.filename in seen or stat.S_ISLNK(x.external_attr>>16):raise RuntimeError("unsafe wheel entry")
   seen.add(x.filename);dest=deps/rel
   if x.is_dir():dest.mkdir(parents=True,exist_ok=True);continue
   dest.parent.mkdir(parents=True,exist_ok=True);b=z.read(x)
   if len(b)!=x.file_size:raise RuntimeError("wheel entry size mismatch")
   dest.write_bytes(b);check(dest)
 R["dependency_restore"]={"name":"pyelftools","version":"0.32","url":URL,"wheel_sha256":WPIN,"wheel_bytes":188525,"directory":str(deps),"entries":len(rows),"dependencies_installed":False,"shared_environment_changed":False}
 env["PYTHONPATH"]=str(deps)
 run("pyelftools-version",[PY,"-B","-c","import elftools,sys;print(elftools.__version__);print(elftools.__file__);assert elftools.__version__=='0.32';assert sys.flags.optimize==0"],env)
 # Only the repository-owned adjacent three helpers may enter the producer.
 helpers={}
 for name in ("generate_privileged_imports_v1.py","generate_provider_package_inputs_v1.py","package_resource_paths.py"):
  relative="scripts/native_owner_candidate/"+name
  p=ROOT/relative;check(p,supplied[relative])
  helpers[name]={"path":str(p),"sha256":supplied[relative]}
 R["repository_helpers"]=helpers
 context={"work_parent":str(WORK),"admission_source":str(PRODUCT),"compiler":CC,"compiler_sha256":CCPIN,"host_sdk":SYSROOT}
 ctx=WORK/"CONTEXT.json";ctx.write_text(json.dumps(context,sort_keys=True,indent=2)+"\n");R["context"]={"path":str(ctx),"sha256":H(check(ctx))}
 # Exact four-TU hosted closure barrier before the existing tests compile.
 pre=WORK/"preflight";pre.mkdir();host=pre/"host";host.mkdir()
 (host/"sdkconfig.h").write_text("/* Host structural/import preflight only. */\n");check(host/"sdkconfig.h")
 for name in CLOSURE:
  dest=pre/"source"/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(check(PRODUCT/name,members[name]));check(dest,members[name])
 wrapper=pre/"package_preflight.c";wrapper.write_bytes(check(ROOT/"scripts/native_owner_candidate/package_preflight.c",supplied["scripts/native_owner_candidate/package_preflight.c"]));check(wrapper)
 sdk=P(SYSROOT)
 resource=P(run("clang-resource",[CC,"-print-resource-dir"],env).stdout.decode().strip()).resolve()
 R["host_header_authority"]={"sdk_root":str(sdk),"compiler_resource_root":str(resource),"system_headers":"observed content baseline for this hosted run, not target SDK"}
 flags=["-isysroot",SYSROOT,"-std=c11","-D_POSIX_C_SOURCE=200809L","-DRISC_NATIVE_ESP_IDF=1","-DRISC_CPU_SCOPE_V5_CANDIDATE=1","-Wall","-Wextra","-Werror","-I"+str(host),"-I"+str(pre/"source/lib/elf_loader/include")]
 macros=run("effective-macros",[CC,*flags,"-dM","-E","-x","c","/dev/null"],env).stdout.decode()
 if re.search(r"^#define NDEBUG\b",macros,re.M) or "#define RISC_NATIVE_ESP_IDF 1" not in macros or "#define RISC_CPU_SCOPE_V5_CANDIDATE 1" not in macros:raise RuntimeError("incorrect effective macros")
 units=[wrapper]+[pre/"source/lib/elf_loader/src"/n for n in CFILES];failures=[]
 for i,unit in enumerate(units):
  dep=pre/("unit%d.d"%i);dp=run("dependency-%d"%i,[CC,*flags,"-M","-MT","unit","-MF",dep,unit],env,required=False)
  row={"unit":str(unit),"dependency_returncode":dp.returncode};R["preflight"].append(row)
  if dp.returncode==0:
   db=check(dep);body=db.decode().replace("\\\n"," ");tokens=shlex.split(body.split(":",1)[1]);row["dependency_count"]=len(tokens)
   for name in tokens:
    p=P(name).resolve()
    if not any(p==r or r in p.parents for r in (pre.resolve(),sdk,resource)):failures.append("unowned dependency "+str(p));continue
    check(p);R["dependencies"][str(p)]=R["input_hashes"][str(p)]
  syntax=run("syntax-%d"%i,[CC,*flags,"-fsyntax-only",unit],env,required=False);row["syntax_returncode"]=syntax.returncode
  if dp.returncode or syntax.returncode:failures.append("preflight unit "+str(i))
 if failures:raise RuntimeError("; ".join(failures))
 R["preflight_passed"]=True;save()
 env["RISC_PACKAGE_TEST_CONTEXT"]=str(ctx);env["RISC_TEST_OUTPUT"]=str(WORK/"CASES.json")
 p=run("host-tests",[PY,"-B",ROOT/"test/native_owner_candidate/run_cases.py",ROOT/"test/native_owner_candidate"],env,timeout=300,required=False)
 cases=json.loads((WORK/"CASES.json").read_text());R["cases"]=cases
 if p.returncode or cases["run"]!=38 or cases["expected"]!=38 or cases["failures"] or cases["errors"] or cases["skipped"] or len(cases["passed"])!=38:raise RuntimeError("38-case committed-Core qualification did not pass")
 for name,record in list(R["input_hashes"].items()):
  if H(P(name).read_bytes())!=record["sha256"]:raise RuntimeError("input drift: "+name)
 R["status"]="passed"
except Exception as e:R["status"]="failed";R["errors"].append({"error":repr(e),"traceback":traceback.format_exc()})
finally:
 save();raw=(WORK/"RESULT.json").read_bytes()
 summary={"status":R["status"],"report":{"path":str(WORK/"RESULT.json"),"sha256":H(raw),"bytes":len(raw)},"errors":R["errors"],"preflight":R["preflight"],"cases":R.get("cases"),"failed_commands":[x for x in R["commands"] if x.get("returncode")],"target_execution":False,"physical_admission":False}
 (WORK/"SUMMARY.json").write_text(json.dumps(summary,sort_keys=True,indent=2)+"\n")
 print(json.dumps(summary,sort_keys=True),flush=True)
 import base64,zlib
 packed=zlib.compress(raw,9)
 (WORK/"RESULT.zlib").write_bytes(packed)
 print(json.dumps({"saved_full_result":str(WORK/"RESULT.json"),"sha256":H(raw),"bytes":len(raw),
  "saved_compressed_result":str(WORK/"RESULT.zlib"),"compressed_sha256":H(packed),"compressed_bytes":len(packed),
  "inline_complete":len(packed)<=192*1024,"descendant_quiescence_established":False}),flush=True)
 if len(packed)<=192*1024:print("RESULT_ZLIB_BASE64="+base64.b64encode(packed).decode(),flush=True)
sys.exit(0 if R["status"]=="passed" else 1)
