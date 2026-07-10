# PLR §3.1: `xs = xs` is a no-op. The local-alias by-reference transform used to
# fire on it, binding `xs = address_of(xs)` -- a self-referential pointer that
# corrupted the list: OOB index checks stopped firing (soundness) and
# len(xs)/xs[i] became nondet (precision). Found by the property-based random
# fuzzer. Here the OOB read must still raise IndexError.
xs = [1, 7, 7]
xs = xs
x = xs[9]
