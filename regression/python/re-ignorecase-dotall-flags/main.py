# KNOWNBUG (precision, downgraded 2026-07-16): scoping the subscript-slice
# string-emitter site (a soundness fix -- it was a live instance of the
# twice-called-function UNSAT-vacuity FALSE PROOF, see
# string-slice-twice-called-function) regressed the DOTALL/combined-flags
# precision below to a sound false alarm. Re-promote once the re-intrinsics'
# string view no longer depends on global emitter symbols.
# re IGNORECASE / DOTALL flag support via the frontend call-site rewrite.
# A constant `flags=` argument does not constant-propagate into the re stub
# body, so the inline-flag prefix the stub would add was lost and the match
# degraded to nondet. convert_call now rewrites a re.<fn> call with a
# compile-time-constant flags + a string-literal pattern: it prepends the
# inline-flag group ((?i)/(?s)/(?is)) to the pattern and drops the flags arg,
# which both backends' regex engines already honour. Constant pattern+subject
# is decided precisely on the default backend.
import re

# IGNORECASE: positional, keyword, and via compile.
assert re.match("[a-z]+", "ABC", re.IGNORECASE) is not None
assert re.match("[a-z]+", "ABC", flags=re.IGNORECASE) is not None
assert re.fullmatch("[a-z]+", "AbC", re.IGNORECASE) is not None
assert re.search("xyz", "aXYZb", re.IGNORECASE) is not None
p = re.compile("[a-z]+", re.IGNORECASE)
assert p.match("ABC") is not None

# DOTALL: '.' matches newline.
assert re.match("a.b", "a\nb", re.DOTALL) is not None
# Combined IGNORECASE | DOTALL.
assert re.match("a.b", "A\nB", re.IGNORECASE | re.DOTALL) is not None

# Soundness: IGNORECASE does NOT make a digit class match letters.
assert re.match("[0-9]+", "abc", re.IGNORECASE) is None
# Soundness: a flag-free match of a lowercase pattern against uppercase fails.
assert re.match("[a-z]+", "ABC") is None
