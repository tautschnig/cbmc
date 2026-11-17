# Each assertion fails in CPython (for some PYTHONHASHSEED). They verified:
# the first element of list(s) and of a loop was the first one inserted, !=
# compared positions, and pop() took the last one inserted.
def loop():
    first = ""
    for x in {"a", "b"}:
        first = x
        break
    assert __assert_k != 0 or (first == "a"), "loop order"
__assert_k = nondet_int()

def not_equal():
    s = {"a", "b"}
    t = {"b", "a"}
    assert __assert_k != 1 or (s != t), "not equal"


def popped():
    p = {"a", "b"}
    p.add("c")
    assert __assert_k != 2 or (p.pop() == "c"), "pop"


loop()
not_equal()
popped()
