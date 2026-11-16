public class Test {
  static String aioobe(int i) {
    int[] a = new int[3];
    try {
      a[i] = 1;
    } catch (ArrayIndexOutOfBoundsException e) {
      return e.getMessage();
    }
    return "none";
  }
  // the messages of the JVM (JBMC's exceptions had none)
  public static void arrayIndex() {
    String m = aioobe(5);
    assert m.equals("5") || m.equals("Index 5 out of bounds for length 3");
  }
  public static void arrayIndexNull() {
    assert aioobe(5) == null;
  }
  public static void arrayIndex8() {
    assert aioobe(-1).equals("-1");
  }
  public static void arrayIndex17() {
    assert aioobe(-1).equals("Index -1 out of bounds for length 3");
  }
  public static void division(int x) {
    try {
      int y = 5 / (x - x);
    } catch (ArithmeticException e) {
      assert e.getMessage().equals("/ by zero");
      assert e.toString().equals("java.lang.ArithmeticException: / by zero");
    }
  }
  public static void negativeSize() {
    try {
      int[] a = new int[-4];
    } catch (NegativeArraySizeException e) {
      assert e.getMessage() == null || e.getMessage().equals("-4");
    }
  }
  public static void negativeSize17() {
    try {
      int[] a = new int[-4];
    } catch (NegativeArraySizeException e) {
      assert e.getMessage().equals("-4");
    }
  }
  public static void nullPointer11() {
    try {
      String s = null;
      s.length();
    } catch (NullPointerException e) {
      assert e.getMessage() == null;
    }
  }
  public static void nullPointer17() {
    try {
      String s = null;
      s.length();
    } catch (NullPointerException e) {
      assert e.getMessage() == null;
    }
  }
  // the message of an exception of the program is its own
  public static void own() {
    try {
      throw new ArrayIndexOutOfBoundsException("mine");
    } catch (ArrayIndexOutOfBoundsException e) {
      assert e.getMessage().equals("mine");
    }
  }
}
