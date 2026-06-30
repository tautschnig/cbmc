# PLR §3.3.8: a binary operator where a concrete user class cannot handle it
# (lacks __op__ and the other operand cannot reflect-handle it) raises TypeError.
# Builtins never reflect-handle a user class, so `C() + 1` is a definite error.
class C:
    pass


x = C() + 1
