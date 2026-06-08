# An f-string-returning method invoked on two distinct objects -- once
# directly on a base instance and once via super() from an override --
# must not poison the path. The f-string's string-refinement output
# symbols are now scoped per function and DECL'd, so each invocation
# gets a fresh instance. Previously they were global and shared, so the
# refinement backend piled conflicting content constraints onto one
# symbol, went UNSAT, and proved the (false) final assertion vacuously
# (class-attributes_fail).
#
# The final assertion is deliberately FALSE (get_age is 3, not 4): with
# the path feasible it must be caught (VERIFICATION FAILED). A vacuous
# path would wrongly verify SUCCESSFUL -- the regression guarded here.
class Vehicle:
    def __init__(self, model: str, year: int):
        self.model = model
        self.year = year

    def get_info(self) -> str:
        return f"{self.year} {self.model}"

    def get_age(self, current_year: int) -> int:
        return current_year - self.year


class Car(Vehicle):
    def __init__(self, model: str, year: int, doors: int):
        super().__init__(model, year)
        self.doors = doors

    def get_info(self) -> str:
        return f"{super().get_info()} with {self.doors} doors"


def test() -> None:
    v = Vehicle("Sedan", 2020)
    assert v.get_info() == "2020 Sedan"
    c = Car("SportsCar", 2022, 2)
    s = c.get_info()
    assert c.get_age(2025) == 4  # actually 3 -- must be caught


test()
