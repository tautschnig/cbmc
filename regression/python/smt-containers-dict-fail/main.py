# Fail-closed twins: (a) subscripting an absent key raises KeyError;
# (b) a dict past the scan cap is reported (python-model-bound), never
# silently mis-looked-up.
d = {"a": 1}
x = d["missing"]
