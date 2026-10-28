"""Explicit Points successor admission; no occurrence or editor grant to the app."""
import json
def same(left,right):return json.dumps(left,sort_keys=True,separators=(',',':'))==json.dumps(right,sort_keys=True,separators=(',',':'))
PROFILE='native-utc-mixed-points-clock-edit'
POLICY={'alarm_utc_cfg':(3,'read'),'timer_utc_cfg':(3,'read'),'alarm_utc_occ':(4,'read-write'),'timer_utc_occ':(4,'read-write'),'alert_mode':(1,'read'),'alert_dnd':(1,'read'),'clock_edit_auth':(4,'read-write'),'points_utc_cfg':(5,'read'),'points_utc_occ':(4,'read-write'),'time_zone':(1,'read')}
def validate_selection(a):
 if a.include_points and not (a.include_timecard and a.native_alarms and a.foreground_idle and a.quick_actions and a.productivity):
  raise ValueError('include-points requires preserved Timecard, native alarms, foreground idle, QuickActions and exact Productivity')
def qualify_app(manifest,record):
 if manifest.get('id')!='points_in_time' or manifest.get('file_name')!='points_in_time.elf':raise ValueError('Points identity differs')
 if record.get('profile')!='points-current-badge' or not same(record.get('grant_bindings'),{'storage.key-value':[1,5],'storage.preferences':[1]}):raise ValueError('Points exact private grants required')
 if record.get('service_occurrence_access') is not False or record.get('clock_edit_authority') is not False or record.get('service_projection_context') is not True:raise ValueError('Points context-only authority differs')
 if not record.get('foreground_idle',{}).get('enabled'):raise ValueError('Points foreground lifecycle missing')
 flags=record.get('build_defines',[])
 for flag in ('POINTS_SERVICE_CONTEXT','ALARM_SERVICE_TAGGED_V2','PORTABLE_NATIVE_SCENE','PORTABLE_QUICK_ATOMIC_PREFERENCES','PORTABLE_FOREGROUND_ALARM_SLEEP'):
  if '-D'+flag not in flags:raise ValueError('Points composition missing '+flag)
 if any(row.get('capability') in ('runtime.realtime-control','storage.app-data') for row in manifest.get('requires',[])+manifest.get('optional',[])):raise ValueError('Points broadened authority')
def qualify_service(record,policy):
 expected={key:{'namespace':namespace,'access':access} for key,(namespace,access) in POLICY.items()}
 if not same(policy,expected) or not same(record.get('bound_keys'),expected):raise ValueError('Mixed service exact ten-key policy required')
 if record.get('profile')!=PROFILE or record.get('points_selected') is not True or record.get('points_context_selected') is not True:raise ValueError('Mixed Points service not selected')
 flags=record.get('defines',[])
 if 'ALARM_NATIVE_SCHEDULES_ONLY' in flags or not {'ALARM_NATIVE_MIXED_POINTS','POINTS_IN_TIME_SERVICE','POINTS_SERVICE_CONTEXT','ALARM_SERVICE_CLOCK_EDIT_V2'}.issubset(flags):raise ValueError('Mixed service defines differ')
def qualify_grants(grants):
 wanted={'storage.key-value':[1,5],'storage.preferences':[1]}
 for capability,instances in wanted.items():
  if not same([row for row in grants if row.get('capability')==capability],[{'capability':capability,'api':1,'instance_id':n} for n in instances]):raise ValueError('Points installed grants differ')
 if any(row.get('capability') in ('runtime.realtime-control','storage.app-data') for row in grants):raise ValueError('Points installed authority broadened')
