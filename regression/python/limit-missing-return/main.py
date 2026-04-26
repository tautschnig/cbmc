# PLR §7.6: implicit None return should not satisfy int assertions
# When a function falls through without return, Python returns None.
# Our model returns 0, which may incorrectly satisfy some assertions.
def get_value(x: int) -> int:
    if x > 0:
        return x
    # Falls through — should return None, not 0

assert get_value(-1) != 0  # should FAIL: get_value(-1) returns None/0
