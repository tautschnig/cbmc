# PLR 7.12: a called function that mutates a module global must be
# reflected at a later module-level read. Conversion-time string
# tracking previously kept the pre-call value, so this verified
# spuriously (a false positive: reported FAILED when cfg IS "overload").
cfg = "production"


def f() -> None:
    global cfg
    cfg = "overload"


def unrelated() -> None:
    pass


f()
assert cfg == "overload"  # f set it
unrelated()
assert cfg == "overload"  # unrelated() didn't touch it
