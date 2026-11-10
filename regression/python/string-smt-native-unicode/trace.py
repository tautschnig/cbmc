# A one-character string may be "é": this assertion fails, and the trace
# shows the code point decoded back to UTF-8.
s = nondet_str()
if len(s) == 1:
    assert ord(s) != 233
