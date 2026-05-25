# PLR / cmath: math.modf returns (frac, integer); both are
# finite when x is finite. math.is{nan,inf,finite} use
# bitvector ops via @c_intrinsic decorators (not the library
# stub which always returns True/False).
#
# This requires:
#   - process_imported_module to infer python_tuple_type from
#     a Tuple-shape return statement (mirroring the same
#     inference in convert_function_def).
#   - math.modf body that synthesises (frac, integer) from
#     trunc(x) so the result is finite/typed.

import math


def modf_basic() -> None:
    frac, integer = math.modf(3.25)
    assert math.isfinite(frac)
    assert math.isfinite(integer)


def isfinite_inf_is_false() -> None:
    assert math.isfinite(1.0) == True
    assert math.isfinite(math.inf) == False
    assert math.isfinite(-math.inf) == False
    assert math.isfinite(math.nan) == False


def isnan_inf_basic() -> None:
    assert math.isnan(1.0) == False
    assert math.isnan(math.nan) == True
    assert math.isinf(math.inf) == True
    assert math.isinf(1.0) == False


modf_basic()
isfinite_inf_is_false()
isnan_inf_basic()
