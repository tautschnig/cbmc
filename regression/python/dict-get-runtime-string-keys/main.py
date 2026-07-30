# PLR §6.4.6: the runtime key scan of dict.get() must compare string
# keys by CONTENT. The raw struct equality compared the refined-string
# DATA POINTER, so a dict whose keys reached the scan through a
# return/call boundary (fresh backing arrays) never matched its own
# probe and get() silently returned the default.
def mk() -> dict:
    return {'alpha': 1, 'beta': 2}


d = mk()
assert d.get('beta') == 2
assert d.get('gamma') is None
