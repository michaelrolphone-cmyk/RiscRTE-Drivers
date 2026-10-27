"""Validate explicit temporary local Quick composition; never claims resident ownership."""
def qualify(record, manifest, grants, selected):
    if type(selected) is not bool or record.get('quick_actions', False) is not selected:
        raise ValueError('Springboard Quick selection mismatch')
    native = record.get('native_copied_quick')
    preference = [g for g in grants if g['capability'] == 'storage.preferences']
    if not selected:
        if native or preference:
            raise ValueError('Unselected Springboard acquired local Quick authority')
        return
    expected = {'selected': True, 'ownership': 'temporary-app-local',
                'resident_architecture_complete': False,
                'preference_capability': 'storage.preferences', 'preference_api': 1,
                'preference_instance': 1, 'completed_pending_frame_before_submit': True,
                'sliced_frame_identity': True, 'wifi_action': 'setup-launch',
                'wifi_destination': 'wifi_settings.elf', 'radio_control_grants': False}
    if not isinstance(native, dict) or any(type(native.get(k)) is not type(v) or native.get(k) != v for k,v in expected.items()):
        raise ValueError('Temporary copied Quick contract mismatch')
    required = ['PORTABLE_QUICK_ACTIONS','PORTABLE_QUICK_PENDING_FRAME',
                'PORTABLE_QUICK_NATIVE_GEOMETRY','PORTABLE_QUICK_CAPABILITY_SAFE',
                'PORTABLE_QUICK_ATOMIC_PREFERENCES','PORTABLE_PREFERENCE_STORE',
                'PORTABLE_FRAME_RECEIPTS','PORTABLE_RASTER_SNAPSHOT']
    if not all('-D'+name in record.get('build_defines', []) for name in required):
        raise ValueError('Native Quick implementation missing from target recipe')
    if preference != [{'capability':'storage.preferences','api':1,'instance_id':1}]:
        raise ValueError('Quick preference authority must be namespace1 only')
    needs = manifest.get('requires', []) + manifest.get('optional', [])
    if any(g['capability'] in ('rtc.clock','bluetooth.hci','net.wifi') for g in needs+grants):
        raise ValueError('Setup-launch Quick must not acquire legacy RTC or radio control')
    if {'capability':'storage.preferences','api':1} not in manifest.get('requires', []):
        raise ValueError('Missing required copied preference service')
