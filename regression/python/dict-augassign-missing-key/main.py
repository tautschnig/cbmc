# PLR §6.2.7: `d[k] += v` on a REGULAR dict with a MISSING key raises KeyError
# (the read half). The augmented-subscript path skipped the KeyError check to
# support defaultdict/Counter auto-insert, but applied that to ALL dicts -> a
# false proof (found by the mutation-oracle). The skip is now restricted to
# dicts actually tracked as defaultdicts; a regular dict raises.
d = {1: 9}
d[0] += 5
