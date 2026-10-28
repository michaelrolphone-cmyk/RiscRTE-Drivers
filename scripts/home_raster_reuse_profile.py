"""Bind optional renderer reuse to actual owning build flags and source bytes."""
import hashlib
from pathlib import Path

def qualify(record, glyph, stroke, source, picker=False, damage=False):
    flags = record.get('artifacts', {}).get('default', {}).get('defines', [])
    selected = record.get('native_raster_caches')
    for name, enabled in [('WATCH_NATIVE_COVERAGE_CACHE', glyph), ('WATCH_NATIVE_STROKE_CACHE', stroke), ('WATCH_PICKER_COMPOSITOR_PLAN', picker), ('WATCH_NATIVE_DAMAGE_REPLAY', damage)]:
        actual = [f for f in flags if f == '-D'+name or f.startswith('-D'+name+'=') or f == '-U'+name]
        if actual != (['-D'+name] if enabled else []):
            raise ValueError('Home raster cache compile selection mismatch: '+name)
    if selected is None and not glyph and not stroke and not picker and not damage:
        return {'glyph_enabled': False, 'stroke_enabled': False}
    if not isinstance(selected, dict) or selected.get('glyph_enabled') is not glyph or selected.get('stroke_enabled') is not stroke:
        raise ValueError('Home raster cache receipt selection mismatch')
    if selected.get('picker_compositor_enabled', False) is not picker:
        raise ValueError('Home picker composition receipt selection mismatch')
    if selected.get('native_damage_replay_enabled', False) is not damage:
        raise ValueError('Home damage replay receipt selection mismatch')
    paths = ('apps/clock/nova/nova.c', 'apps/clock/nova/native_stroke_cache.h','apps/clock/nova/native_reuse_admission.h', 'apps/clock/faces/render.inc')
    if picker or 'picker_compositor_enabled' in selected:
        paths += ('apps/clock/faces/picker_compositor.h',)
    if damage or 'native_damage_replay_enabled' in selected:
        paths += ('apps/clock/nova/native_damage_decl.h', 'apps/clock/faces/damage_replay.inc')
    for name in paths:
        expected = hashlib.sha256((Path(source)/name).read_bytes()).hexdigest()
        if selected.get('input_sha256', {}).get(name) != expected:
            raise ValueError('Home raster cache source identity mismatch: '+name)
    return selected
