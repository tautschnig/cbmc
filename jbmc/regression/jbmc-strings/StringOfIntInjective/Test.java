public class Test {
  public static void ints(int x, int y) {
    if (String.valueOf(x).equals(String.valueOf(y)))
      assert x == y;
  }
  public static void longs(long x, long y) {
    if (Long.toString(x).equals(Long.toString(y)))
      assert x == y;
  }
  public static void intAndLong(int x, long y) {
    if (("" + x).equals("" + y))
      assert x == y;
  }
  public static void notInjective(int x, int y) {
    if (String.valueOf(x).length() == String.valueOf(y).length())
      assert x == y;
  }
  public static void hexAndDecimal(int x, int y) {
    if (x >= 0 && y >= 0 && Integer.toString(x, 16).equals(Integer.toString(y)))
      assert x == y;
  }
  public static void guarded(int x, int y, boolean b) {
    String s = b ? String.valueOf(x) : "7";
    if (!b && String.valueOf(x).length() > 0 && s.equals(String.valueOf(y)))
      assert x == y;
  }
  public static void main(String[] a) {
    ints(5, 5); longs(-7, -7); intAndLong(12, 12L); System.out.println("ok");
  }
}
