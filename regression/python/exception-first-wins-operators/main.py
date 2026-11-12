# The first exception raised wins: the operations below raised their
# exception over the IndexError raised before them.
import math

z = nondet_int()
if z == 0:
    hit = False
    try:
        try:
            x = [1][3] + (1 // z)
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (1 % z)
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (1 / z)
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (1.0 / (-0.0 * z))
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (math.sqrt(-1))
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (chr(-1))
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (math.log(0))
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (1 << (z - 1))
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + (None < 1)
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
    hit = False
    try:
        try:
            x = [1][3] + ("a" + z)
        except IndexError:
            hit = True
    except Exception:
        pass
    assert hit
