# PLR §8.3: `for u, v in d` iterates the dict's KEYS; a Tuple target
# unpacks each (tuple) key. The dict for-variant never unpacked a Tuple
# target, so body statements using the names were DROPPED and the loop
# proved vacuously (ESBMC dict_tuple_key_for_iter_fail).
def f() -> int:
    d = {(1, 2): 10, (3, 4): 20}
    total = 0
    for u, v in d:
        total = total + u + v
    return total


assert f() == 10
