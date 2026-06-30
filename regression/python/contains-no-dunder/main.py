# PLR §3.3.1: `x in obj` needs __contains__ (or __iter__/__getitem__ fallback).
# A class whose MRO defines none is not a container -> TypeError.
class C:
    pass


c = C()
b = 1 in c
