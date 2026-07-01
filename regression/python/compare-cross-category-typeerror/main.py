# PLR §6.10.1: ordering (< > <= >=) of two operands in DIFFERENT orderable
# categories raises TypeError (e.g. int vs list). convert_compare now flags a
# category mismatch (orderable_category_of) before the numeric/list paths.
# CPython: TypeError; expected: VERIFICATION FAILED.
r = 1 < [1]
