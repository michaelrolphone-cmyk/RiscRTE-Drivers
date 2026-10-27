"""Apply explicit app-owned private storage bindings, never a diagnostic fallback.

Capability declarations and exact owning recipe receipts remain separate from
Runtime backend/admission proof. KV can name several namespaces; AppData has one
private namespace per app, whose cross-app uniqueness Runtime also validates.
"""
PRIVATE=('storage.key-value','storage.app-data')
def private_bindings(manifest,record):
 declarations={}
 for row in manifest.get('requires',[])+manifest.get('optional',[]):
  cap=row.get('capability')
  if cap not in PRIVATE:continue
  if cap in declarations or type(row.get('api')) is not int or row['api']!=1:
   raise ValueError('Invalid private storage declaration: '+str(cap))
  declarations[cap]=row['api']
 result={}
 bindings=record.get('grant_bindings',{})
 if not isinstance(bindings,dict):raise ValueError('Invalid owning grant bindings')
 for cap in PRIVATE:
  if cap not in bindings:continue
  values=bindings[cap]
  if cap not in declarations:raise ValueError('Undeclared private storage binding: '+cap)
  if not isinstance(values,list) or not values or any(type(n) is not int or not 1<=n<=2147483647 for n in values) or len(set(values))!=len(values):
   raise ValueError('Invalid private storage namespaces: '+cap)
  if cap=='storage.app-data' and len(values)!=1:raise ValueError('AppData requires one private namespace')
  result[cap]=list(values)
 return result

def apply_private_bindings(manifest,record,grants):
 bindings=private_bindings(manifest,record)
 result=[dict(row) for row in grants if row.get('capability') not in bindings]
 for cap,values in bindings.items():
  result.extend({'capability':cap,'api':1,'instance_id':n} for n in values)
 return result

def qualify_private_grants(manifest,record,grants):
 expected=private_bindings(manifest,record)
 for cap,namespaces in expected.items():
  actual=[row for row in grants if row.get('capability')==cap]
  wanted=[{'capability':cap,'api':1,'instance_id':n} for n in namespaces]
  if actual!=wanted:raise ValueError('Installed private storage grants differ from owning recipe: '+cap)
 return expected
