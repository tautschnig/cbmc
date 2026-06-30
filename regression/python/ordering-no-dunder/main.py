# PLR §3.3 / 6.10.1: ordering (`<`/`<=`/`>`/`>=`) between instances whose classes
# define none of __lt__/__le__/__gt__/__ge__ (nor the reflected form) raises
# TypeError. (== / != are unaffected -- they have the object identity default.)
class C:
    pass


b = C() < C()
