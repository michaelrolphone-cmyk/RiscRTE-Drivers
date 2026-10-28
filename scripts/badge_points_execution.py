"""Points-specific recipe checks and read-only probes for the shared Runtime runner."""
import json
from pathlib import Path
from points_product_profile import qualify_app, qualify_grants

SCENARIOS = ['save-reopen', 'light-draft', 'service-draft']

def selected_recipe(target, store, product, sha):
    record = json.loads((target / 'points/points-build-record.json').read_text())
    manifest = json.loads((store / 'points_in_time.json').read_text())
    assert record == product['points_profile'], 'Points product/target recipe mismatch'
    assert record['manifest'] == manifest
    qualify_app(manifest, record)
    policies = [x for x in json.loads((store / 'boot.json').read_text())['app_capabilities']
                if x['manifest'] == 'points_in_time.json']
    assert len(policies) == 1
    qualify_grants(policies[0]['grants'])
    assert sha(store / 'points_in_time.elf') == product['applications']['points_in_time']['sha256']
    for path, digest in record['consumed_source_sha256'].items():
        assert sha(Path(path)) == digest, 'Changed Points dependency: ' + path
    return record

# App and adapter observations never invoke an action, write a model, or
# replace a provider result. Inputs still traverse the actual CST/PMIC drivers.
APP_PROBE = r'''
extern void badge_points_observe(const int*,const int*,const unsigned char*,const char*);
__attribute__((visibility("default"))) void badge_points_ui_probe(void){
 int s[24]={pn.active,page,selected,ready,writer.loaded,writer.uncertain,writer.meta_uncertain,
 pn.w,pn.h,pn.count,portable_points_native_ready(NULL,NULL),draft.kind,draft.hour,draft.minute,
 draft.duration_minutes,draft.enabled,draft.weekdays,points_retained,points_context_status,
 (int)writer.saved.revision,clock_valid,service_valid,service_state.state,service_state.error};
 int controls[64][6]={{0}};unsigned char saved[64];points_config_encode(&writer.saved,saved);
 for(unsigned i=0;i<pn.count&&i<64;i++){controls[i][0]=pn.controls[i].action;controls[i][1]=pn.controls[i].arg;
 controls[i][2]=pn.controls[i].r.x;controls[i][3]=pn.controls[i].r.y;controls[i][4]=pn.controls[i].r.w;controls[i][5]=pn.controls[i].r.h;}
 badge_points_observe(s,&controls[0][0],saved,notice?notice:"");
}
'''
