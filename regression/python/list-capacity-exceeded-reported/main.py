# Exceeding the modelled container capacity (PYTHON_MAX_LIST_LENGTH = 64) via
# repetition is now REPORTED as a python-model-bound property failure (a checked
# BMC bound, like an unwinding assertion) rather than silently truncating the
# data while the length field claims more. Raising --max-list-length would clear
# it. Concatenation and extend past capacity are guarded the same way.
a = [0] * 100
x = len(a)
