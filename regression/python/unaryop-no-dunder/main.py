# PLR §3.3.8: a unary operator on a class whose MRO defines no matching dunder
# (__neg__/__pos__/__invert__) raises TypeError ('bad operand type for unary -').
class C:
    pass


x = -C()
