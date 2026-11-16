"""Source-profile inventory, not a proof of runtime or hardware compatibility."""
import ast,json,re
from pathlib import Path

def read(p):return json.loads(Path(p).read_text())
def identity(m):return Path(m.get('file_name','')).stem or m.get('id')

def collect(system,utilities,controllers,store,catalog,productivity=None):
 store=Path(store);boot=read(store/'boot.json');selected={}
 for policy in boot['app_capabilities']:
  manifest=read(store/policy['manifest']);name=identity(manifest)
  if not name:raise ValueError('Missing installed app identity')
  selected[name]={'manifest':policy['manifest'],'requires':manifest.get('requires',[]),'grants':policy['grants']}
 capabilities={(x['capability'],int(x['api'])) for p in boot['app_capabilities'] for x in p['grants']}
 for row in boot['drivers']:
  for item in read(store/row['manifest']).get('provides',[]):capabilities.add((item['capability'],int(item['api'])))
 # This headless Runtime implements this read-only allowlist on existing
 # RuntimeFileSources, without a new driver. Product still needs explicit grant.
 capabilities.add(('storage.installed-files',1))
 def absent(req):
  result=[]
  for item in req:
   api=item.get('api');match=re.fullmatch(r'(>=)?([0-9]+)',str(api))
   if not match or not any(cap==item['capability'] and (version>=int(match[2]) if match[1] else version==int(match[2])) for cap,version in capabilities):result.append(item)
  return result
 rows={}
 for repo,root in [('system',Path(system)),('utilities',Path(utilities)),*([('productivity',Path(productivity))] if productivity is not None else [])]:
  for path in sorted((root/'Apps').glob('**/*.json')):
   m=read(path);name=identity(m)
   if not name or not m.get('file_name','').endswith('.elf'):continue
   row=rows.setdefault((repo,name),{'repository':repo,'id':name,'profiles':[]})
   req=m.get('requires');missing=absent(req or [])
   row['profiles'].append({'path':str(path.relative_to(root)),'version':m.get('version'),'runtime_profile':m.get('runtime_profile'),'requires':req,'missing_capabilities':missing,'assessment':'capability-declaration-absent; legacy/API inspection required' if req is None else 'additional-capabilities-required' if missing else 'declared-capability-closure-only; build/lifecycle/geometry qualification required'})
 names={}
 tree=ast.parse((Path(controllers)/'scripts/current_apps_overlay.py').read_text())
 for node in tree.body:
  if isinstance(node,ast.Assign) and len(node.targets)==1 and isinstance(node.targets[0],ast.Name) and node.targets[0].id in ('SYSTEM_APPS','UTILITY_APPS','PRODUCTIVITY_APPS','CLOCK_APPS'):names[node.targets[0].id]=ast.literal_eval(node.value)
 intended=set(sum((tuple(v) for v in names.values()),()))|{'contexts'}
 for repo,group in [('system','SYSTEM_APPS'),('utilities','UTILITY_APPS'),('productivity','PRODUCTIVITY_APPS'),('watch','CLOCK_APPS')]:
  for name in names[group]:rows.setdefault((repo,name),{'repository':repo,'id':name,'profiles':[]})
 launch={Path(x['file_name']).stem for x in read(catalog)['apps']}
 if not launch<=set(selected):raise ValueError('Catalog contains unpackaged apps: '+str(launch-set(selected)))
 for (_,name),row in rows.items():
  row.update(installed=name in selected,launcher_entry=name in launch,intended_watch_role=name in intended)
  if name in selected:row['selected_contract']=selected[name]
 return {'schema':2,'scope':'Selected source Apps manifests and exact intended Watch overlay roles; service closure is static, never hardware qualification','installed_app_ids':sorted(selected),'launcher_app_ids':sorted(launch),'intended_watch_roles':sorted(intended),'missing_intended_roles':sorted(intended-set(selected)),'available_capability_versions':sorted([{'capability':c,'api':a} for c,a in capabilities],key=lambda r:(r['capability'],r['api'])),'apps':list(rows.values()),'limits':'No unsupported hardware inference from app absence. Raw/legacy/native profiles are alternatives, not interchangeable. Presence of capability names does not establish policy, lifecycle, sleeping alarms, rendering or real-device support.'}
