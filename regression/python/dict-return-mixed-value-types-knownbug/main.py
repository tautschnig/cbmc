# KNOWNBUG twin of dict-return-mixed-value-types: when the int-valued
# literal is the one nested under the `if` (visited FIRST by the
# return-shape walk), or an empty {} is mixed in, the unified layout
# commits to a concrete value column and the other literal's values
# are coerced through the wrong slot (read back as garbage / the None
# sentinel). Acceptance: all three asserts verify SUCCESSFULLY.
def api(op) -> dict:
    if op == 'Scan':
        return {'Count': 2}
    if op == 'Nothing':
        return {}
    return {'Status': 'ok'}


scan = api('Scan')
assert scan.get('Count') == 2
other = api('Upd')
assert other.get('Status') == 'ok'
empty = api('Nothing')
assert empty.get('Count') is None
