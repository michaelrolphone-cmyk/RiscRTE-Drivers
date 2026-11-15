"""Explicit scoped diagnostic selection; never infer from other trace settings."""
FLAG='-DPORTABLE_SPRINGBOARD_GESTURE_TRACE'
CLASSIFICATION='bounded-terminal-gesture-diagnostic-not-performance-metric'
def selected(product):
 enabled=product.get('springboard_gesture_diagnostics',False)
 if type(enabled) is not bool:raise ValueError('Gesture diagnostic selection must be a boolean')
 if enabled and product.get('native_performance_trace') is not True:raise ValueError('Gesture diagnostics require the selected native trace transport')
 return enabled
def runtime_defines(product):return ['-DBADGE_SPRINGBOARD_GESTURE_DIAGNOSTICS=1'] if selected(product) else []
def qualify(recipe,enabled,app='springboard'):
 if type(enabled) is not bool:raise ValueError('Gesture diagnostic selector must be boolean')
 receipt=recipe.get('springboard_gesture_diagnostics')
 defined=FLAG in recipe.get('build_defines',[])
 if defined is not enabled:raise ValueError('Gesture diagnostic actual compile selection mismatch')
 if not enabled:
  if receipt is not None:raise ValueError('Unselected gesture diagnostic receipt')
  return
 if app!='springboard':raise ValueError('Gesture diagnostics are scoped to Springboard')
 if not isinstance(receipt,dict):raise ValueError('Missing gesture diagnostic receipt')
 for key,value in {'schema':1,'classification':CLASSIFICATION,'selector':FLAG[2:],'tags':[224,225,226,227,228],'max_records_per_gesture':14,'max_semantic_points':4,'provider_coordinates':'decoded-not-raw-CST-bytes','scope':'Springboard-owned only; suppress Quick/alarm/other apps','transport':'existing-owner-RAM-trace; explicit perf capture only','physical_verified':False}.items():
  if type(receipt.get(key)) is not type(value) or receipt.get(key)!=value:raise ValueError('Gesture diagnostic schema mismatch: '+key)
 if recipe.get('watch_native_ui') is not True or recipe.get('time_profile')!='watch-native-time':raise ValueError('Wrong gesture diagnostic presentation')
 if recipe.get('native_performance_trace',{}).get('enabled') is not True or '-DPORTABLE_PERFORMANCE_TRACE' not in recipe.get('build_defines',[]):raise ValueError('Gesture diagnostic native trace transport missing')
