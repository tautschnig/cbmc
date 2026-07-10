# PLR §6.3.2/§3.2: list concatenation of two constant literals is constant-folded
# into a trackable merged list (with element-type promotion), so min()/max()/
# sorted() over a mixed-orderable-category concat raises TypeError as in CPython.
# Found by the property-based random fuzzer (rand_169 family). CPython: TypeError.
x = min([8, 8] + ["c"])
