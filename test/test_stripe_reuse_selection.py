#!/usr/bin/env python3
import importlib.util,pathlib
r=pathlib.Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0,str(r/'scripts'))
from build_badge_native import verify_stripe_reuse_selection
name='/repo/components/co5300/native/Co5300Display.cpp';n=0
for enabled in (False,True):
 exact='-DRISC_CO5300_REUSE_UNCHANGED='+str(int(enabled))
 for row in ({'file':name,'arguments':['c++',exact]},{'file':name,'command':'c++ '+exact}):assert verify_stripe_reuse_selection([row],enabled)['enabled']==enabled;n+=1
 for bad in ([],[exact,exact],[exact,'-URISC_CO5300_REUSE_UNCHANGED'],[exact,'-DRISC_CO5300_REUSE_UNCHANGED='+str(int(not enabled))]):
  try:verify_stripe_reuse_selection([{'file':name,'arguments':['c++',*bad]}],enabled)
  except RuntimeError:n+=1
  else:raise AssertionError(bad)
 for rows in ([],[{'file':name,'arguments':[exact]}]*2):
  try:verify_stripe_reuse_selection(rows,enabled)
  except RuntimeError:n+=1
  else:raise AssertionError(rows)
print(n,'selection/duplicate/contradiction/undefinition cases PASS')
