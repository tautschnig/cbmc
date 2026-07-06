# PLR §6.10.1: min()/max() compare elements pairwise, so a list spanning two
# incomparable orderable categories (int vs str) raises TypeError -- the same
# rule sorted() enforces. Found by the property-based random fuzzer (rand_169).
# CPython: TypeError; expected: VERIFICATION FAILED.
x = min([8, "c"])
