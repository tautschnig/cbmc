# KNOWNBUG (PLR §6.10.1): min()/max()/sorted() over a list that becomes
# mixed-orderable-category via CONCATENATION (`xs = [8, 8]; xs = xs + ["c"]`)
# raises TypeError, but cbmc verifies SUCCESSFUL. The DIRECT and Name-bound-to-a-
# literal forms ARE caught (min-max-mixed-category CORE); this concat form is not,
# because list `+` builds an opaque symbolic struct rather than a constant literal,
# so the merged list is not tracked in list_literals. Closing it needs constant-
# folding of list concatenation (with operand resolution + element-type promotion
# + aliasing safety) so the merged literal is trackable -- a feature, not a fold.
# Found by the property-based random fuzzer (rand_169).
xs = [8, 8]
xs = xs + ["c"]
r = min(xs)
