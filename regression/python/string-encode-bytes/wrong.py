# Each of these assertions is false: encode() returned a str, which never
# equals bytes.
__assert_k = nondet_int(); s = nondet_str()
x = "a b".encode()
assert __assert_k != 0 or (x != b"a b")
assert __assert_k != 1 or ("a b".encode("ascii") != b"a b")
assert __assert_k != 2 or ("\xe9".encode() != b"\xc3\xa9")
assert __assert_k != 3 or ("\xe9".encode("latin-1") != b"\xe9")
assert __assert_k != 4 or ("a b".encode(encoding="ascii") != b"a b")
