# A function imported from another module that mutates that module's
# global dict must be reflected at a later read of the imported global.
from modx import state, mutate
assert state["a"] == 1
mutate()
assert "b" in state
assert len(state) == 2
