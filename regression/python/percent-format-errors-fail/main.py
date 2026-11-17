# printf-style formatting errors: wrong argument count and wrong argument
# type raise TypeError, an unknown conversion raises ValueError.
seen = []


def attempt(code: int) -> None:
    try:
        if code == 0:
            t = "%d" % "q"
        elif code == 1:
            t = "%s %s" % ("q",)
        elif code == 2:
            t = "%x" % (3, 4)
        elif code == 3:
            t = "no conversions" % 9
        else:
            t = "%k" % 1
        seen.append("none")
    except TypeError:
        seen.append("type")
    except ValueError:
        seen.append("value")


for c in range(5):
    attempt(c)
assert seen == ["type", "type", "type", "none", "value"]
assert "fixed" % [] == "fixed"
