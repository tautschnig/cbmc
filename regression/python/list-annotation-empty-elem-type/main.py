# A parameterised annotation on an empty list literal (x: list[str] = []) must
# pin the element type. Previously the empty literal defaulted to an int
# element type and the annotation was ignored, so appending strings emitted a
# string->int element coercion that aborted the SMT back-end
# (smt_string -> signedbv PRECONDITION in convert_typecast). Now the annotation
# is authoritative.
parts: list[str] = []
parts.append("hello")
parts.append("world")
assert len(parts) == 2
assert parts[0] == "hello"
assert parts[1] == "world"

# Built by appending computed substrings in a loop (the re.findall shape).
toks: list[str] = []
s = "a12b34"
i = 0
while i < 6:
    toks.append(s[i:i + 2])
    i += 2
assert len(toks) == 3
