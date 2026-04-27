# PLR §2.4.5: input() reads from stdin — model as nondet string
s: str = input()
assert len(s) >= 0
