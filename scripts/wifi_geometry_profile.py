"""Exact opt-in native WiFi presentation recipe, shared by build and seal."""
WIFI_NATIVE_FLAGS={'-D'+x for x in ('PORTABLE_WIFI_NATIVE_PRESENTATION','PORTABLE_NOVA_UI','PORTABLE_RF_NATIVE_CANVAS','PORTABLE_APP_OWNS_TOUCH_CHROME','PORTABLE_WATCH_NATIVE_HEIGHT','PORTABLE_WATCH_COLOR_ONLY','PORTABLE_WATCH_SAFE_INSETS','PORTABLE_SAFE_VIEWPORT')}
def qualify_wifi_geometry_recipe(recipe,selected):
 if type(selected) is not bool:raise ValueError('WiFi selection must be boolean')
 for name in ('watch_native_ui','safe_content_native_raster'):
  value=recipe.get(name,False)
  if type(value) is not bool or value!=selected:raise ValueError('WiFi geometry recipe selection mismatch: '+name)
 if 'compile_inputs' in recipe and type(recipe['compile_inputs']) is not dict:raise ValueError('Malformed WiFi compile_inputs')
 definitions=recipe.get('build_defines',recipe.get('compile_inputs',{}).get('definitions',[]))
 if 'compile_inputs' in recipe and recipe['compile_inputs'].get('definitions')!=definitions:raise ValueError('WiFi compiler definitions disagree')
 if type(definitions) is not list or any(type(x) is not str for x in definitions):raise ValueError('Malformed WiFi compiler definitions')
 if ('-DPORTABLE_WIFI_NATIVE_PRESENTATION' in definitions)!=selected:raise ValueError('WiFi geometry compiler selection mismatch')
 if selected:
  if not WIFI_NATIVE_FLAGS.issubset(definitions):raise ValueError('Incomplete native WiFi compiler profile')
  if any(type(recipe.get(k)) is not int or recipe[k]!=0 for k in ('display_rotation','touch_rotation')):raise ValueError('Native WiFi requires unrotated adapter coordinates')
