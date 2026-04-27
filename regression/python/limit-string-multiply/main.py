# PLR §6.7: string * int repeats the string
s: str = "ab" * 3
assert len(s) == 6
assert s == "ababab"
