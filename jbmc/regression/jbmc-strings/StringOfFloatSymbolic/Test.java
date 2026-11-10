public class Test {
  // Some float outside [1.5, 1.6) prints with the prefix "1.5", as 1.5E7.
  public static void prefixNotRange(float f) { String s = String.valueOf(f); if(s.startsWith("1.5")) assert f >= 1.5f && f < 1.6f; }
  public static void prefixFound(float f) { String s = String.valueOf(f); assert !s.startsWith("1.5"); }
  public static void exactFound(float f) { String s = String.valueOf(f); assert !s.equals("2.75"); }
  public static void exactDetermines(float f) { String s = String.valueOf(f); if(s.equals("2.75")) assert f == 2.75f; }
  public static void lengthFound(float f) { String s = String.valueOf(f); assert s.length() != 3; }
  public static void lengthBound(float f) { String s = String.valueOf(f); assert s.length() >= 3; }
  public static void firstDigit(float f) { if(!(f >= 1 && f < 2)) return; String s = String.valueOf(f); assert s.charAt(0) == '1'; }
  public static void sign(float f) { if(!(f < 0)) return; String s = String.valueOf(f); assert s.charAt(0) == '-'; }
  public static void noSign(float f) { if(!(f > 0)) return; String s = String.valueOf(f); assert s.charAt(0) != '-'; }
  public static void nan(float f) { if(f == f) return; assert String.valueOf(f).equals("NaN"); }
  public static void doubleLengthBound(double d) { assert String.valueOf(d).length() <= 24; }
  public static void doubleLengthFound(double d) { assert String.valueOf(d).length() != 24; }
}
