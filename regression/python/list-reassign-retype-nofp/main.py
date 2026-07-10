# Negated: the retyped value must NOT compare equal to a wrong value (guards the
# false proof from returning -- reinterpreted bits used to prove `!=` true).
xs = [6, 2]
xs = ["a", "b"]
assert xs[0] != "a"
