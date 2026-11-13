public class Test {
  public static void constants() {
    assert Double.doubleToRawLongBits(1.0) == 0x3ff0000000000000L;
    assert Double.doubleToRawLongBits(-0.0) == 0x8000000000000000L;
    assert Float.floatToRawIntBits(1.5f) == 0x3fc00000;
    assert Float.floatToRawIntBits(-0.0f) == 0x80000000;
    assert Double.doubleToLongBits(Double.NaN) == 0x7ff8000000000000L;
    assert Float.floatToIntBits(Float.NaN) == 0x7fc00000;
  }

  public static void symbolic(double d, float f) {
    if(Double.doubleToRawLongBits(d) == 0x4000000000000000L)
      assert d == 2.0;
    if(Float.floatToRawIntBits(f) == 0x40400000)
      assert f == 3.0f;
  }

  public static void symbolicBad(double d) {
    if(Double.doubleToRawLongBits(d) == 0x4000000000000000L)
      assert d != 2.0;
  }
}
