# Vacuity twin: the values coming back through the unified pv layout
# must be the REAL values (CPython: AssertionError).
def api(op) -> dict:
    if op == 'Scan':
        return {'Count': 2}
    return {'Status': 'ok'}


scan = api('Scan')
assert scan.get('Count') == 3
