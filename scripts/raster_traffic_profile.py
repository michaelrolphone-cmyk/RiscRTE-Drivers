"""Prove selected owning-builder raster optimizations, never infer from intent."""
def add_arguments(parser):
    parser.add_argument('--nova-mask-bounds', action='store_true')
    parser.add_argument('--settings-alpha-geometry', action='store_true')
    parser.add_argument('--raster-overwrite-clear', action='store_true')
    parser.add_argument('--raster-underlay-exchange', action='store_true')

def validate(args, parser):
    if args.nova_mask_bounds and not args.sliced_native_frames:
        parser.error('Nova mask bounds require sliced native frames')
    if (args.settings_alpha_geometry or args.raster_overwrite_clear) and not (args.native_settings_ui and args.sliced_native_frames):
        parser.error('Settings raster traffic options require native Settings and sliced frames')
    if args.raster_underlay_exchange and not (args.sliced_native_frames and args.foreground_idle and args.native_alarms):
        parser.error('Underlay exchange requires sliced frames and foreground alarm clients')

def arguments(args, app):
    out=[]
    if app=='springboard' and getattr(args,'nova_mask_bounds',False):out.append('--nova-mask-bounds')
    if app=='settings':
        if args.settings_alpha_geometry:out.append('--settings-alpha-geometry')
        if args.raster_overwrite_clear:out.append('--raster-overwrite-clear')
    if app in ('settings','springboard') and args.raster_underlay_exchange:
        out.append('--raster-underlay-exchange')
    return out

def qualify(record, alpha=False, clear=False, exchange=False, mask=False):
    flags=record.get('build_defines',[])
    selections=[('PORTABLE_SETTINGS_ALPHA_GEOMETRY',alpha),('PORTABLE_RASTER_OVERWRITE_CLEAR',clear),('PORTABLE_RASTER_UNDERLAY_EXCHANGE',exchange),('PORTABLE_NOVA_MASK_BOUNDS',mask),('PORTABLE_SETTINGS_PREPARED_TEXT',False)]
    for macro,enabled in selections:
        actual=[x for x in flags if x=='-D'+macro or x.startswith('-D'+macro+'=') or x=='-U'+macro]
        if actual != (['-D'+macro] if enabled else []):
            raise ValueError('Raster traffic define mismatch: '+macro)
    geometry=record.get('settings_replay_geometry',{})
    memory=record.get('raster_memory',{})
    if geometry.get('alpha_geometry',False) is not alpha or memory.get('overwrite_clear',False) is not clear or memory.get('underlay_exchange',False) is not exchange:
        raise ValueError('Raster traffic receipt mismatch')
    if record.get('nova_mask_bounds',False) is not mask or geometry.get('prepared_text',False) is not False:
        raise ValueError('Unselected raster feature or mask receipt mismatch')
    if alpha and (geometry.get('pixel_budget_and_512_checkpoints_preserved') is not True or geometry.get('completed_frame_hit_maps_unchanged') is not True):
        raise ValueError('Settings transparent geometry authority receipt missing')
    if (alpha or clear or exchange or mask) and record.get('sliced_native_frames') is not True:
        raise ValueError('Raster traffic requires selected sliced frames')

def selection_record(args):
    return {'nova_mask_bounds':args.nova_mask_bounds,'settings_alpha_geometry':args.settings_alpha_geometry,
            'overwrite_clear_apps':['settings'] if args.raster_overwrite_clear else [],
            'underlay_exchange_apps':['settings','springboard'] if args.raster_underlay_exchange else [],
            'springboard_has_no_existing_viewport_clear':True,
            'physical_performance_verified':False}
