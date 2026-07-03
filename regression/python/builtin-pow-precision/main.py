# PLR §6 / §3.3.8: pow(a, b) == a ** b; pow(a, b, m) == (a ** b) % m. The builtin
# is routed through the **/% operator lowering (was nondet). Precise now.
assert pow(2, 10) == 1024
assert pow(2, 10, 1000) == 24
assert pow(3, 3, 5) == 2


def f(n):
    return pow(n, 2)


assert f(4) == 16
