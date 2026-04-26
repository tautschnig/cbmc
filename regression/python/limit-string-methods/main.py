# PLR §4.7.1: string methods
# str.upper() is recognized as a method (returns nondet string)
s: str = "hello"
u: str = s.upper()
# Can't assert content equality (nondet), but no crash
