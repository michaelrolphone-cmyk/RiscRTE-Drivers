"""Bind additive time-sync product selection to native, grant and execution proof."""
import time_sync_profile
from execution_case_inventory import time_sync_cases

def selected(built):
 policy=built.get('time_server_sync',{})
 if not isinstance(policy,dict) or type(policy.get('enabled',False)) is not bool:raise ValueError('Invalid time-sync selection')
 enabled=policy.get('enabled',False)
 if enabled and (not built.get('native_wifi_geometry') or not built.get('native_alarms') or not built.get('clock_edit_authority')):raise ValueError('Time sync requires Wi-Fi and alarm-safe clock authority')
 return enabled

def qualify_product(built,proof,record,manifest,boot,header):
 enabled=selected(built)
 if type(proof.get('time_sample',{}).get('enabled',False)) is not bool or proof.get('time_sample',{}).get('enabled',False)!=enabled:raise ValueError('Native time sampler selection mismatch')
 members={'time-sample/driver.elf','time-sample/manifest.json'}
 if members.intersection(built['store_files'])!=(members if enabled else set()):raise ValueError('Time sampler store mismatch')
 providers=[x for x in boot.get('drivers',[]) if x.get('manifest')=='time-sample/manifest.json']
 if len(providers)!=(1 if enabled else 0):raise ValueError('Time sampler boot provider mismatch')
 grants=[(x['manifest'],g) for x in boot.get('app_capabilities',[]) for g in x.get('grants',[]) if g.get('capability')=='net.time-sample']
 if grants!=([('wifi_settings.json',{'capability':'net.time-sample','api':1,'instance_id':0})] if enabled else []):raise ValueError('Time sampler application grant mismatch')
 policy=built.get('time_server_sync',{})
 time_sync_profile.qualify(record,manifest,enabled,policy.get('server'),built['source_pins']['runtime']['commit'],header)
 if enabled and policy!={'enabled':True,'server':record['time_server_sync']['server'],'application':'wifi_settings','authentication':'unauthenticated SNTP','clock_commit':'existing alarm-safe UTC transaction; timezone unchanged'}:raise ValueError('Time sync product policy mismatch')

def qualify_execution(built,receipt):
 if not selected(built):return
 cases=[x for x in receipt['results'] if x['target_index'] in time_sync_cases(built)]
 marker='TIME_SYNC actual WiFi client, facade, Runtime clock, durable alarm permit and verified UTC commit PASS; modeled UDP sample, no physical network claim'
 if len(cases)!=1 or marker not in cases[0]['stdout']:raise ValueError('Missing actual time synchronization integration proof')
