# PLR §3.2: strings are immutable sequences, so item assignment
# raises TypeError. CBMC must detect the uncaught exception.
s = "abc"
s[0] = "x"
