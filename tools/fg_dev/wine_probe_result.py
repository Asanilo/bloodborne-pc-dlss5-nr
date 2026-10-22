"""Conservative offscreen Evaluate classification; never claims Native Present."""
import re


def evaluate_status(text):
    rows = []
    for line in text.splitlines():
        match = re.fullmatch(r'FG_EVAL_GPU_COMPLETE frame=(\d+) result=0x([0-9a-f]{8}) disable_word=0x([0-9a-f]{8}) present_eligible=([01]) native_presents=0', line)
        if match:
            n, result, disable, eligible = match.groups()
            result, disable = int(result, 16), int(disable, 16)
            rows.append({'frame': int(n), 'result': result, 'disable_word': disable,
                         'eligible': result == 1 and disable & 255 == 0 and eligible == '1'})
    eligible = sum(row['eligible'] for row in rows)
    return {'status': 'OUTPUT_REQUIRES_PIXEL_VALIDATION' if eligible else 'NO_ELIGIBLE_OUTPUT' if rows else 'NOT_EVALUATED',
            'gpu_complete_frames': rows, 'eligible_outputs': eligible, 'native_presents': 0,
            'scope': 'synthetic offscreen fixture only; fence and eligibility do not prove interpolation content'}
