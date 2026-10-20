"""Explicit candidate selection; no GPU operations or implicit environment opt-in."""


def requested(args):
    value = getattr(args, 'native_mouse', False)
    if type(value) is not bool:
        raise ValueError('native mouse: strict boolean required')
    if value and (getattr(args, 'pm4_cold_trace', False) is not True or
                  sum(bool(getattr(args, k, None)) for k in
                      ('reboot_diagnostic', 'context_recovery')) != 1):
        raise ValueError('native mouse: exactly one isolated scope and cold trace required')
    return value


def check_record(record, enabled):
    value = record.get('native_mouse', False)
    if type(value) is not bool or value is not enabled:
        raise ValueError('native mouse: explicit scope request drift')
    if enabled and record.get('schema') not in (
            'nr-reboot-sr-diagnostic-v2', 'nr-isolated-context-v2'):
        raise ValueError('native mouse: v2 scope required')


def check_environment(record, environment):
    enabled = record.get('native_mouse', False)
    check_record(record, enabled)
    if environment.get('BB_NATIVE_MOUSE') != ('1' if enabled else None):
        raise ValueError('native mouse: manifest selection drift')
    if enabled and any(k in environment for k in (
            'BB_ONLINE', 'BB_BBSC', 'BB_NET_MODULE', 'BB_RE_CAMERA', 'BB_RE_ENTRY')):
        raise ValueError('native mouse: online or diagnostic camera patches forbidden')
