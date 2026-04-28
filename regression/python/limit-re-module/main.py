# PLR stdlib: re module — modeled as nondet returns
import re
m = re.match(r"(\d+)", "123abc")
# Returns nondet, but no crash
