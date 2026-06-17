# A Python set is modelled as a 64-bit bitmap over [offset, offset+64) (offset
# 0), so an element outside that range cannot be represented. Previously it was
# SILENTLY DROPPED -- `100 not in {0, 100}` falsely held (a false proof). It is
# now reported as a python-model-bound violation at the set construction.
s = {0, 100}
x = 100 in s
