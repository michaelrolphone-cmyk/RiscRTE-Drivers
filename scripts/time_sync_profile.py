"""Verify selected shared time synchronization without changing grants implicitly."""
import hashlib
import json
def qualify(record,manifest,enabled,server,runtime,header):
    flags=record.get('build_defines',[])
    policy=record.get('time_server_sync')
    selected='-DPORTABLE_WIFI_TIME_SYNC' in flags
    if selected!=enabled or bool(policy)!=enabled:raise ValueError('Time sync selection mismatch')
    if not enabled:return
    expected={'capability':'net.time-sample@1','server':server,'timeout_ms':5000,
              'sample_max_age_ms':1000,'transport':'unauthenticated SNTP','timezone_changed':False,
              'clock_commit':'shared AlarmClockEditV2 + native seed/readback',
              'header_source':'sdk/driver/RiscTimeSampleV1.h','header_commit':runtime,
              'header_sha256':hashlib.sha256(header).hexdigest()}
    if policy!=expected:raise ValueError('Time sync policy/provenance mismatch')
    if '-DPORTABLE_TIME_SYNC_SERVER='+json.dumps(server) not in flags:
        raise ValueError('Time sync server compiler selection mismatch')
    for flag in ('-DPORTABLE_NATIVE_CUSTODY_FENCE','-DPORTABLE_FOREGROUND_ALARM_SLEEP','-DALARM_SERVICE_CLOCK_EDIT_V2'):
        if flag not in flags:raise ValueError('Time sync lifecycle/clock authority missing')
    for cap,api in (('net.time-sample',1),('runtime.realtime-control',1),('alarm.service',2)):
        if manifest.get('requires',[]).count({'capability':cap,'api':api})!=1:
            raise ValueError('Time sync manifest requirement mismatch: '+cap)
