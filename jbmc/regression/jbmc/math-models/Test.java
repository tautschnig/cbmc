public class Test {
  // Math.sqrt of a non-square (the model assumed sqrt * sqrt == a: every
  // path after sqrt(2) was unreachable)
  public static void sqrtReachable() {
    double r = Math.sqrt(2.0);
    assert false;
  }
  public static void sqrtRange() {
    double r = Math.sqrt(2.0);
    assert r > 1.414 && r < 1.415;
  }
  // max and min of NaN are NaN (the model assumed a result that is a or b,
  // equal to one of them: none for NaN)
  public static void maxNaN() {
    assert Double.isNaN(Math.max(1.0, Double.NaN));
    assert Double.isNaN(Double.max(Double.NaN, 1.0));
    assert Float.isNaN(Math.min(Float.NaN, 1.0f));
  }
  public static void minZero() {
    assert Double.doubleToRawLongBits(Math.min(0.0, -0.0))
        == Double.doubleToRawLongBits(-0.0);
    assert Double.doubleToRawLongBits(Math.max(-0.0, 0.0)) == 0L;
  }
  public static void ints(int a, int b) {
    int m = Math.max(a, b);
    assert m >= a && m >= b && (m == a || m == b);
    assert Integer.min(a, b) <= a;
    assert Long.max(a, (long) b) >= b;
  }
}
