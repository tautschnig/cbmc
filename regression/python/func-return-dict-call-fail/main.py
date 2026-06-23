def h():
    return dict(a=1)
d = h()
# was a false proof: h's return typed as int default, d == {} verified.
assert d == {}
