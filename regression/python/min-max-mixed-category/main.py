# PLR §6.10.1: sorted()/min()/max() compare elements pairwise, so a list spanning
# two incomparable orderable categories (int vs str) raises TypeError. Found by
# the property-based random fuzzer (rand_169). Direct-literal AND Name-bound-to-a-
# literal forms are covered by the shared constant_list_orderable_conflict helper.
# CPython: TypeError; expected: VERIFICATION FAILED.
xs = [1, "a"]
x = min(xs)
