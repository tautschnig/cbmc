# Verify that --python-no-exception-checks suppresses
# the uncaught-exception property at module level and
# the top-level raise-as-assertion emission. Tool should
# succeed despite the reachable raise Exception(...)
# because exception checks are disabled by the flag.

def might_fail(x: int) -> int:
    if x < 0:
        raise Exception("negative not allowed")
    return x


y = might_fail(-1)  # reachable raise — would normally fail
# No assertions — verification should still succeed.
