public class Test {
  public static void absExact() {
    try {
      Math.absExact(Integer.MIN_VALUE);
      assert false;
    } catch (ArithmeticException e) {
      assert e.getMessage().equals(
        "Overflow to represent absolute value of Integer.MIN_VALUE");
    }
    assert Math.absExact(-5L) == 5L;
  }
  public static void values() {
    assert Math.multiplyHigh(Long.MAX_VALUE, 4L) == 1L;
    assert Math.multiplyHigh(-1L, 1L) == -1L;
    assert Math.multiplyFull(Integer.MAX_VALUE, 2) == 4294967294L;
    assert Math.floorMod(-7L, 3) == 2;
    assert Math.floorDiv(-7L, 2) == -4L;
    try {
      Math.multiplyExact(Long.MIN_VALUE, -1);
      assert false;
    } catch (ArithmeticException e) {
      assert e.getMessage().equals("long overflow");
    }
  }
  public static void main(String[] a) {
    absExact(); values();
    System.out.println("ok");
  }
}
