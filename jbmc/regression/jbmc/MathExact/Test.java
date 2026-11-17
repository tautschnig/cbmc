public class Test {
  static String exact(int c) {
    try {
      switch (c) {
      case 0: return "" + Math.addExact(Integer.MAX_VALUE, 1);
      case 1: return "" + Math.addExact(Long.MIN_VALUE, -1L);
      case 2: return "" + Math.subtractExact(Integer.MIN_VALUE, 1);
      case 3: return "" + Math.multiplyExact(1 << 16, 1 << 16);
      case 4: return "" + Math.multiplyExact(Long.MIN_VALUE, -1L);
      case 5: return "" + Math.incrementExact(Integer.MAX_VALUE);
      case 6: return "" + Math.decrementExact(Long.MIN_VALUE);
      case 7: return "" + Math.negateExact(Integer.MIN_VALUE);
      case 8: return "" + Math.toIntExact(1L << 31);
      case 9: return "" + Math.floorDiv(7, 0);
      case 10: return "" + Math.floorMod(7L, 0L);
      default: return "none";
      }
    } catch (ArithmeticException e) {
      return e.getMessage();
    }
  }
  public static void overflow() {
    assert exact(0).equals("integer overflow");
    assert exact(1).equals("long overflow");
    assert exact(2).equals("integer overflow");
    assert exact(3).equals("integer overflow");
    assert exact(4).equals("long overflow");
    assert exact(5).equals("integer overflow");
    assert exact(6).equals("long overflow");
    assert exact(7).equals("integer overflow");
    assert exact(8).equals("integer overflow");
  }
  public static void byZero() {
    assert exact(9).equals("/ by zero");
    assert exact(10).equals("/ by zero");
  }
  public static void values() {
    assert Math.addExact(2, 3) == 5;
    assert Math.multiplyExact(-4L, 5L) == -20L;
    assert Math.floorDiv(-7, 2) == -4;
    assert Math.floorMod(-7, 2) == 1;
    assert Math.floorMod(7L, -2L) == -1L;
    assert Math.toIntExact(-5L) == -5;
  }
  public static void symbolic(int x, int y) {
    try {
      int r = Math.addExact(x, y);
      assert (long) r == (long) x + (long) y;
    } catch (ArithmeticException e) {
      long s = (long) x + (long) y;
      assert s > Integer.MAX_VALUE || s < Integer.MIN_VALUE;
    }
  }
  public static void symbolicCanThrow(int x) {
    try {
      Math.incrementExact(x);
    } catch (ArithmeticException e) {
      assert false;
    }
  }
  public static void main(String[] a) {
    overflow(); byZero(); values(); symbolic(Integer.MAX_VALUE, 2); symbolic(3, 4);
    System.out.println("ok");
  }
}
