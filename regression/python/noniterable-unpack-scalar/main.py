# PLR §3.3.1 / §7.2.2: unpacking a non-iterable raises TypeError
# ('cannot unpack non-iterable int'). Shared provably-non-iterable-scalar
# predicate at the tuple/list unpack site. CPython: TypeError; VERIFICATION FAILED.
a, b = 5
