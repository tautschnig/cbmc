# Exercises Wave 2 of re support. The library's match/search/
# fullmatch entry points route through the cprover_string_*_func
# intrinsics; the SMT backend intercepts the intrinsic and
# (for --cvc5) checks the pattern's validity via the Python →
# SMT-LIB regex translator at src/solvers/strings/
# python_regex_to_smt.cpp.
#
# The subject-to-SMT-string integration is still a follow-up, so
# today the intrinsic returns 'no match' conservatively for every
# call. The library's 'if match: return Match(); else: None'
# wrapper keeps the result type correct. Once the subject is
# translated into an SMT string, the result will actually reflect
# str.in_re semantics for the supported patterns.

from re import match, search, fullmatch, compile


# Simple literal match — the pattern is a constant that the
# translator should handle.
m = match("abc", "abcxyz")
if m is not None:
    _ = m.group()


# Character class + quantifier.
s = search(r"\d+", "abc 123 xyz")
if s is not None:
    _ = s.group()


# Compiled pattern via Pattern class.
p = compile(r"[A-Za-z]+")
fm = p.fullmatch("Hello")
if fm is not None:
    _ = fm.group()


# Today the intrinsic conservatively reports 'no match', so each
# if-branch above is simply not entered. Reaching this line
# without 'no body for callee match/search/fullmatch' is the
# positive observation.
assert True
