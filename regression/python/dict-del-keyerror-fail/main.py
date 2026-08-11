# Companion to python-dict-del-keyerror: an UNCAUGHT
# 'del d[missing]' must surface as VERIFICATION FAILED
# because Python raises KeyError there.

d = {1: "one", 2: "two"}
del d[3]   # KeyError, no try/except → uncaught exception
