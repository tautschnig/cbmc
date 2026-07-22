# PLR: __file__ is the module path -- a str, never None (ESBMC
# github_4662_fail asserted `__file__ is None`, which must be False).
assert __file__ is not None
assert bool(__file__)
assert isinstance(__file__, str)
