"""Exact opt-in native Alarm/Countdown recipe selection, shared by build/seal."""
ALARM_NATIVE_FLAGS={'-D'+x for x in ('PORTABLE_ALARM_NATIVE_PRESENTATION','PORTABLE_APP_OWNS_TOUCH_CHROME','PORTABLE_NOVA_UI','PORTABLE_RF_NATIVE_CANVAS','PORTABLE_SAFE_VIEWPORT','PORTABLE_WATCH_SAFE_INSETS','PORTABLE_WATCH_NATIVE_HEIGHT','PORTABLE_WATCH_COLOR_ONLY')}
def qualify_alarm_geometry_recipe(recipe,selected):
 value=recipe.get('native_alarm_geometry',False)
 if type(selected) is not bool or type(value) is not bool or value!=selected:raise ValueError('Alarm geometry recipe selection mismatch')
 definitions=recipe.get('compile_inputs',{}).get('definitions',[])
 if type(definitions) is not list or any(type(x) is not str for x in definitions):raise ValueError('Malformed alarm compiler definitions')
 if ('-DPORTABLE_ALARM_NATIVE_PRESENTATION' in definitions)!=selected:raise ValueError('Alarm geometry compiler selection mismatch')
 if selected and not ALARM_NATIVE_FLAGS.issubset(definitions):raise ValueError('Incomplete native alarm compiler profile')
