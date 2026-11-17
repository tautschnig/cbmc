# f-string format specs on symbolic ints and strs are exact.
def nondet_int() -> int: ...


n = nondet_int()
__CPROVER_assume(0 <= n < 100000)
s = f"{n:03}"
assert len(s) >= 3
assert int(s) == n
t = f"[{'ok':>4}|{n % 16:x}]"
assert t.startswith("[  ok|")
