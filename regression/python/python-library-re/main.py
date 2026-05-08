# Exercises the library's re model.
#
# The model returns nondet-chosen Match / None, so assertions
# depending on a match outcome should explore both paths.

from re import match, search, compile, findall, split, sub, fullmatch, escape

# Compile + method chain.
p = compile(r'abc')
m = p.match('abc')
if m is not None:
    # Match methods should resolve through the library.
    _ = m.group()
    _ = m.groups()
    _ = m.start()
    _ = m.end()
    _ = m.span()

# Module-level search returns Optional[Match].
s = search(r'\d+', '123')
if s is not None:
    _ = s.group()

# Other entry points.
_ = findall(r'\d+', '12 34')
_ = split(r'\s+', 'a b c')
_ = sub(r'a', 'b', 'abc')
_ = fullmatch(r'abc', 'abc')
_ = escape('abc.def')

# A verifiable property: after library resolution, re's entry
# points are real Python functions with bodies, not no-body stubs.
# Reaching here without a 'no body for callee' failure is the
# assertion.
assert True
