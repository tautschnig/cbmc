class Gauge:
    LIMIT = 2

    def check(self) -> None:
        # PLR 4.2.2: class-scope names are not visible in method bodies;
        # this condition raises NameError, and a translation that drops
        # the loop must not report success.
        while LIMIT > 0:
            assert False


Gauge().check()
