# Limitation: except catches all exceptions regardless of type
# In Python, ValueError is NOT caught by except TypeError
# Our model catches it anyway (overapproximation)
def risky(x: int) -> int:
    if x < 0:
        raise ValueError("negative")
    return x

caught_wrong_type: bool = False
try:
    result: int = risky(-1)
except TypeError:
    # This should NOT execute (ValueError != TypeError)
    caught_wrong_type = True

# In correct Python, caught_wrong_type is False (ValueError propagates)
# In our model, caught_wrong_type is True (all exceptions caught)
assert not caught_wrong_type
