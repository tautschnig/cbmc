# Case B: function with no return type annotation — return type unknown
def mystery(x: int):
    if x > 0:
        return x
    return "negative"

# With nondet x, mystery could return int or str
# Tier 3 (tagged union) needed to handle this correctly
