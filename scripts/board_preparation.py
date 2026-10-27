#!/usr/bin/env python3
"""Validate board preparation data and generate a C config, never boot firmware.

This is not Runtime board.json: installation rejects the missing startup adapter,
provider graph and staged shared default. No fallback to a similarly named board.
"""
import argparse
import json
from pathlib import Path

def require(ok, message):
    if not ok:
        raise ValueError(message)

def integer(value, lo, hi, field):
    require(type(value) is int and lo <= value <= hi, field)
    return value

def validate(p):
    require(p['schema'] == 'riscrte.board-preparation' and type(p['schema_version']) is int and p['schema_version'] == 1, 'schema')
    pins = []
    def pin(value):
        integer(value, 0, 48, 'pin')
        require(value not in pins, 'overlapping pins')
        pins.append(value)
    i, d, t, power = (p[key] for key in ('i2c', 'display', 'touch', 'power'))
    integer(i['instance_id'], 1, 2147483647, 'I2C instance')
    integer(i['controller'], 0, 1, 'I2C controller')
    integer(i['frequency_hz'], 1, 400000, 'I2C speed')
    pin(i['sda']); pin(i['scl'])
    for key in ('width', 'height'):
        integer(d[key], 1, 4096, key)
        require(t[key] == d[key] and type(t[key]) is int, 'touch/display size agreement')
    for key, maximum in (('offset_x', 65535-d['width']), ('offset_y', 65535-d['height'])):
        integer(d[key], 0, maximum, key)
    require(type(d['rotation']) is int and d['rotation'] in (0, 90, 180, 270), 'rotation')
    q = d['qspi']
    integer(q['physical_controller'], 1, 3, 'SPI controller')
    integer(q['frequency_hz'], 1, 80000000, 'SPI speed')
    require(len(q['data']) == 4, 'four QSPI data pins')
    for v in [q['sclk'], *q['data'], q['cs']]: pin(v)
    for device in (d, t):
        reset = device['reset']; pin(reset['pin'])
        require(type(reset['active_high']) is bool, 'reset polarity')
        for k in ('assert_ms', 'recovery_ms'): integer(reset[k], 1, 500, k)
    pin(t['irq'])
    require(type(t['irq_active_high']) is bool, 'IRQ polarity')
    require(t['mode'] == 'polling', 'only existing polling consumer selected')
    for device in (t, power): integer(device['address'], 8, 0x77, 'I2C address')
    require(t['address'] != power['address'], 'duplicate I2C address')
    for k in ('identity_register','identity_mask','identity_value','enable_register','enable_mask','enable_value'):
        integer(power[k], 0, 255, k)
    for key in ('identity', 'enable'):
        require(power[key+'_mask'] and not (power[key+'_value'] & ~power[key+'_mask']), key+' mask')
    integer(power['transaction_timeout_ms'], 1, 100, 'I2C timeout')
    require(power['policy'] == 'read-only-preserve', 'no power writes in this helper')
    require(power['display_power_off_supported'] is False, 'no rail off capability')
    require(power['power_key'] == {'source':'pmic-pwron','gpio':None}, 'PMIC key is not a GPIO')
    require(power['boot_key']['source'] == 'gpio' and type(power['boot_key']['active_high']) is bool, 'BOOT key')
    pin(power['boot_key']['pin'])
    require(d['pixel_format'] == 'rgb565' and d['backlight'] is None, 'panel brightness is not PWM')
    require(len(d['damage_alignment']) == 2, 'damage alignment')
    for v in d['damage_alignment']: integer(v, 1, 64, 'damage alignment')
    safe = d['safe_insets']
    for k in ('top','right','bottom','left'): integer(safe[k], 0, 4095, 'safe inset')
    require(safe['left']+safe['right'] < d['width'] and safe['top']+safe['bottom'] < d['height'], 'nonempty safe area')
    for k in ('flash_bytes','psram_bytes'): integer(p['physical_memory'][k], 1, 1<<30, k)
    require(p['app']['role'] == 'default.elf' and p['app']['board_specific_fork'] is False, 'shared default only')
    require(p['integration']['installable'] is False and p['integration']['hardware_verified'] is False, 'preparation is not firmware')
    return p

BADGE175_ID = 'waveshare-esp32-s3-touch-amoled-1.75c'
# Exact board admission is separate from reusable structural validation. These
# are source-evidenced electrical facts, not automatic board autodetection.
BADGE175 = {
    ('board_id',): BADGE175_ID,
    ('physical_memory',): {'flash_bytes':33554432,'psram_bytes':8388608},
    ('i2c',): {'instance_id':101,'controller':0,'sda':15,'scl':14,'frequency_hz':400000},
    ('display','controller'): 'co5300',
    ('display','width'): 466, ('display','height'): 466,
    ('display','offset_x'): 6, ('display','offset_y'): 0, ('display','rotation'): 0,
    ('display','qspi'): {'physical_controller':2,'sclk':38,'data':[4,5,6,7],'cs':12,'frequency_hz':40000000},
    ('display','reset'): {'pin':1,'active_high':False,'assert_ms':10,'recovery_ms':150},
    ('touch','controller'): 'cst9217', ('touch','address'): 90,
    ('touch','reset'): {'pin':2,'active_high':False,'assert_ms':10,'recovery_ms':50},
    ('touch','irq'): 11, ('touch','irq_active_high'): False,
    ('power','controller'): 'axp2101', ('power','address'): 52,
    ('power','identity_register'): 3, ('power','identity_mask'): 255, ('power','identity_value'): 74,
    ('power','enable_register'): 128, ('power','enable_mask'): 1, ('power','enable_value'): 1,
    ('power','display_touch_supply'): 'DCDC1', ('power','cpu_shares_display_touch_supply'): True,
    ('power','boot_key'): {'source':'gpio','pin':0,'active_high':False},
    ('app','repository'): 'michaelrolphone-cmyk/RiscRTE-Watch',
    ('present_unselected',): ['qmi8658','es8311','es7210'],
    ('absent_hardware',): ['external-rtc','sd-card','lora','infrared'],
}

def admit_badge175(p):
    validate(p)
    for path, expected in BADGE175.items():
        value = p
        for key in path:
            value = value[key]
        # JSON types remain distinct: true cannot stand in for GPIO1.
        require(json.dumps(value, sort_keys=True) == json.dumps(expected, sort_keys=True),
                '1.75C source mismatch: ' + '.'.join(path))
    return p

def header(p):
    validate(p)
    power = p['power']
    lines = ['/* Generated from validated preparation data. Not a runtime ABI. */',
             '#include "prepare.h"', 'static const risc_board_prepare_config board_prepare_config = {',
             f'    .bus_instance_id = {p["i2c"]["instance_id"]}u,']
    for k in ('address','identity_register','identity_mask','identity_value','enable_register','enable_mask','enable_value','transaction_timeout_ms'):
        lines.append(f'    .{"power_address" if k == "address" else k} = {power[k]}u,')
    lines += ['    .reset_count = 2u,', '    .resets = {']
    for kind in ('display','touch'):
        r = p[kind]['reset']
        lines.append('        {%du, %s, %du, %du},' % (r['pin'], str(r['active_high']).lower(), r['assert_ms'], r['recovery_ms']))
    return '\n'.join(lines + ['    }', '};', ''])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('--header', type=Path)
    parser.add_argument('--board-id', choices=[BADGE175_ID])
    args = parser.parse_args()
    p = validate(json.loads(args.profile.read_text()))
    if args.board_id:
        admit_badge175(p)
    if args.header:
        args.header.parent.mkdir(parents=True, exist_ok=True)
        args.header.write_text(header(p))
    print(p['board_id'] + ': preparation validated; NOT installable firmware')

if __name__ == '__main__': main()
