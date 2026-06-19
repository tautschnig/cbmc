# PLR §4.2.1: a return annotation that is a bare name bound nowhere is
# evaluated at def time and raises NameError. The frontend models this as
# an uncaught exception (gated on the all_bound_names oracle).
def unknown_annotation() -> UnknownType:
    return 42


result = unknown_annotation()
