# PLR §3.3.1: obj[k] = v requires __setitem__. A class whose MRO defines none
# does not support item assignment -> TypeError.
class C:
    pass


c = C()
c[0] = 5
