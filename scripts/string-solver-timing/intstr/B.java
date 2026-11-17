public class B {
  // the string of an int: a property of the digits
  public static void intLen(int x) { String s = Integer.toString(x); assert s.length() <= 11; }
  public static void intWitness(int x) { String s = Integer.toString(x); assert !s.equals("-2147483648"); }
  public static void intEqual(int x) { if (x > 0) { String s = String.valueOf(x); assert !s.equals("12345"); } }
  public static void intInjective(int x, int y) { if (String.valueOf(x).equals(String.valueOf(y))) assert x == y; }
  public static void intConcat(int x) { String s = "id-" + x; assert s.startsWith("id-"); }
  public static void intRoundTrip(int x) { assert Integer.parseInt(Integer.toString(x)) == x; }
  public static void longLen(long x) { String s = Long.toString(x); assert s.length() <= 20; }
  public static void longWitness(long x) { String s = Long.toString(x); assert !s.equals("9223372036854775807"); }
  public static void longInjective(long x, long y) { if (Long.toString(x).equals(Long.toString(y))) assert x == y; }
  public static void longRoundTrip(long x) { assert Long.parseLong(Long.toString(x)) == x; }
  public static void parseInt(String s) { if (s != null && s.length() <= 4) { try { int v = Integer.parseInt(s); assert v != 4321; } catch (NumberFormatException e) {} } }
  public static void hex(int x) { String s = Integer.toHexString(x); assert s.length() <= 8; }
  public static void twoInts(int x, int y) { String s = x + "," + y; assert !s.equals("12,-7"); }
  public static void intCompare(int x, int y) { if (x < y && x >= 0) assert Integer.toString(x).length() <= Integer.toString(y).length(); }
}
