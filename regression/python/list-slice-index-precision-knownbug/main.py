# KNOWNBUG (slice-precision root). `xs[1:4]` does not preserve every element
# precisely (e.g. `xs[1]` of the slice is nondet -- `assert (xs[1:4])[1] == 7`
# false-alarms), so `.index(v)`/`in` over a sliced list can spuriously match a
# nondet slot. For `.index`, whose ValueError is emitted only on the
# result==-1 path, the spurious-match path yields no exception -> false proof:
# CPython raises ValueError (0 not in [3,7]) but cbmc verifies SUCCESSFUL.
# Root is list-slice element preservation (a precision whole-group), NOT index()
# itself -- fixing the slice to preserve element values closes this and a family
# of slice false alarms. Found by the widened random fuzzer (rand_1497).
xs = [2, 3, 7]
xs = xs[1:4]
i = xs.index(0)
