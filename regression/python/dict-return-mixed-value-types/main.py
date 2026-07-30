# PLR §3.2: a function whose dict-literal returns have DIVERGING value
# types must keep every literal's values readable through the unified
# return layout. Pinned working shape: the str-valued literal nested
# under the guard, the int-valued literal trailing — the boto3-stub
# `call_boto3` dispatch shape:
#     if op == 'Upd':  return {'Status': 'ok'}
#     return {'Count': 2}
# The reverse order and empty-{} mixes still lose values — pinned in
# dict-return-mixed-value-types-knownbug.
def api(op) -> dict:
    if op == 'Upd':
        return {'Status': 'ok'}
    return {'Count': 2}


scan = api('Scan')
assert scan.get('Count') == 2
