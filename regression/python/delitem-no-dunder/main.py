# PLR §3.3.1: del obj[k] requires __delitem__. A class whose MRO defines none
# does not support item deletion -> TypeError.
class C:
    pass


c = C()
del c[0]
