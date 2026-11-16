public class Test {
  // isFinite of negative values (the model compared with MIN_VALUE)
  public static void negative() {
    assert Double.isFinite(-1.5);
    assert Float.isFinite(-1.5f);
    assert Double.isFinite(-Double.MAX_VALUE);
  }
  public static void infinite() {
    assert !Double.isFinite(Double.NEGATIVE_INFINITY);
    assert !Float.isFinite(Float.NaN);
  }
  public static void negativeBad() {
    assert !Double.isFinite(-1.5);
  }
}
