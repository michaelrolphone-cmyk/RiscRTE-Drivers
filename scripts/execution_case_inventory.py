"""Read the authoritative full-runner case inventory without executing its code.

The owning runner's existing declarative selection expression is the authority.
Packaging reads that same expression, rather than maintaining another count
chain. Only conditional expressions, boolean conjunction, known selections and
integer counts are accepted; arbitrary calls/operators are rejected.
"""
import ast
from pathlib import Path

def case_ids(built,foreground=None,source=None):
 foreground=built.get('foreground_idle',{}).get('enabled',False) if foreground is None else foreground
 values={'time_server_sync':built.get('time_server_sync',{}).get('enabled',False),'native_wifi_geometry':built.get('native_wifi_geometry',False),'quick_actions':built.get('quick_actions',False),'alarms_enabled':built.get('native_alarms',False),'files_enabled':'file_browser.elf' in built.get('store_files',{}),'foreground':foreground}
 if any(type(v) is not bool for v in values.values()):raise ValueError('Scenario selections must be boolean')
 if values['time_server_sync'] and not (values['native_wifi_geometry'] and values['alarms_enabled']):raise ValueError('Time sync scenario requires Wi-Fi and alarm cohort')
 if source is None:source=Path(__file__).with_name('test_badge_prototype_execution.py').read_text()
 nodes=[n.value for n in ast.walk(ast.parse(source)) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='total_cases' for t in n.targets)]
 if len(nodes)!=1:raise ValueError('Expected one authoritative case inventory')
 def evaluate(n):
  if isinstance(n,ast.IfExp):
   test=evaluate(n.test);yes=evaluate(n.body);no=evaluate(n.orelse)
   if type(test) is not bool or type(yes) is not int or type(no) is not int:raise ValueError('Invalid case conditional')
   return yes if test else no
  if isinstance(n,ast.BoolOp) and isinstance(n.op,ast.And):
   v=[evaluate(x) for x in n.values]
   if any(type(x) is not bool for x in v):raise ValueError('Invalid case conjunction')
   return all(v)
  if isinstance(n,ast.Name) and n.id in values:return values[n.id]
  if isinstance(n,ast.Attribute) and isinstance(n.value,ast.Name) and n.value.id=='a' and n.attr=='foreground':return values['foreground']
  if isinstance(n,ast.Constant) and type(n.value) is int and n.value>0:return n.value
  raise ValueError('Unsupported case inventory syntax')
 count=evaluate(nodes[0])
 if type(count) is not int or count<=0:raise ValueError('Invalid scenario count')
 return tuple(range(count))

def time_sync_cases(built):
 other=dict(built);other['time_server_sync']={'enabled':False}
 return tuple(x for x in case_ids(built) if x not in case_ids(other))
