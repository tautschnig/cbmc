public class Test {
  // a string that is not a number throws (any double before)
  public static void malformed() {
    try {
      Double.parseDouble("1.2.3");
      assert false;
    } catch (NumberFormatException e) {
      assert e.getMessage().equals("multiple points");
    }
  }
  public static void empty() {
    try {
      Double.parseDouble("  ");
      assert false;
    } catch (NumberFormatException e) {
      assert e.getMessage().equals("empty String");
    }
  }
  public static void message8() {
    try {
      Float.parseFloat("x ");
      assert false;
    } catch (NumberFormatException e) {
      assert e.getMessage().equals("For input string: \"x\"");
    }
  }
  public static void message25() {
    try {
      Float.parseFloat("x ");
      assert false;
    } catch (NumberFormatException e) {
      assert e.getMessage().equals("For input string: \"x \"");
    }
  }
  // the exact value of a short decimal
  public static void values() {
    assert Double.parseDouble(" -1.5e2 ") == -150.0;
    assert Double.parseDouble("0.1") == 0.1;
    assert Float.parseFloat("2.5f") == 2.5f;
    assert Double.isNaN(Double.valueOf("NaN"));
    assert Double.parseDouble("-Infinity") == Double.NEGATIVE_INFINITY;
  }
  public static void valuesBad() {
    assert Double.parseDouble("0.1") == 0.10000000000000002;
  }
  // a long decimal is any value of its sign
  public static void longDecimal() {
    double d = Double.parseDouble("3.14159265358979323846");
    assert d >= 0;
  }
}
