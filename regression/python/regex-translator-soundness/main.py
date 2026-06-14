# PLR-soundness of the shared Python-regex -> SMT-LIB translator
# (src/solvers/strings/python_regex_to_smt.cpp), exercised via the
# __cbmc_re_* intrinsics under the native SMT-String backend. Each
# assertion encodes the EXACT CPython re semantics; a regression here
# means the SMT model disagrees with Python (an unsound over- or
# under-approximation of the regex language).

# Anchors must not be silently dropped: ^ restricts search to the
# start, $ to the end (with the single-trailing-newline allowance).
assert not __cbmc_re_search("^abc", "xabc")     # ^ anchors to start
assert __cbmc_re_search("^abc", "abcd")
assert not __cbmc_re_match("abc$", "abcd")       # $ anchors to end
assert __cbmc_re_match("abc$", "abc")
assert __cbmc_re_search("abc$", "xabc\n")        # $ matches before final \n
assert not __cbmc_re_search("abc$", "xabc\n\n")  # ...but only one

# '.' excludes newline (no re.DOTALL).
assert __cbmc_re_fullmatch("a.c", "abc")
assert not __cbmc_re_fullmatch("a.c", "a\nc")

# Negated classes / \D \S \W match exactly one character.
assert __cbmc_re_fullmatch("[^a]", "b")
assert not __cbmc_re_fullmatch("[^a]", "bb")
assert not __cbmc_re_fullmatch("[^a]", "")
assert not __cbmc_re_fullmatch("\\D", "ab")

# Control-character escapes denote the actual code point.
assert __cbmc_re_fullmatch("\\n", "\n")
assert __cbmc_re_fullmatch("\\s", "\t")

# Escaped metacharacters are literals; the anchor strip must not eat \$.
assert __cbmc_re_fullmatch("a\\.b", "a.b")
assert not __cbmc_re_fullmatch("a\\.b", "axb")
assert __cbmc_re_search("a\\$", "ba$c")
