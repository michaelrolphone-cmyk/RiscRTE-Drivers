"""Host-only Battery alarm selector evidence; never physical acceptance."""
from package_badge_flash import require
from execution_source_inputs import validated_inputs

CASES = [25, 29]

def qualify(receipt, reference, sanitize):
    require(receipt.get('passed') is True and receipt.get('sanitize') is sanitize,
            'Battery execution failed or wrong sanitizer lane')
    require(receipt.get('alarm_foreground_app') == 'battery', 'Missing Battery selector')
    require(reference.get('passed') is True and reference.get('sanitize') is sanitize
            and reference.get('alarm_foreground_app') == 'calculator',
            'Missing same-lane generic Calculator control')
    require(receipt.get('selected_cases') == CASES and receipt.get('case_count') == len(CASES),
            'Missing exact Battery alarm cases')
    for key in ('source_pins', 'store_files'):
        require(bool(receipt.get(key)) and receipt[key] == reference.get(key),
                'Mixed Battery execution cohort: ' + key)
    require(validated_inputs(receipt) == validated_inputs(reference), 'Mixed Battery execution source inputs')
    require(receipt.get('target_instruction_execution') is False
            and receipt.get('physical_tested') is False, 'Battery host-only boundary changed')
    results = receipt.get('results', [])
    require([r.get('target_index') for r in results] == CASES, 'Missing Battery case results')
    for case, result in zip(CASES, results):
        output = result.get('stdout', '')
        require(not result.get('stderr') and 'APP_ENTER battery ' in output
                and f'PASS actual Runtime alarm case={case} ' in output,
                'Incomplete actual Battery alarm execution')
