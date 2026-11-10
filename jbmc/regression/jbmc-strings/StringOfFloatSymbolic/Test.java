public class Test {
  // true: a float whose string starts with "1.5" is in [1.5, 1.6)
  public static void prefixTrue(float f) { String s = String.valueOf(f); if(s.startsWith("1.5")) assert f >= 1.5f && f < 1.6f; }
  // falsifiable: some float prints with prefix "1.5"
  public static void prefixFalse(float f) { String s = String.valueOf(f); assert !s.startsWith("1.5"); }
  // falsifiable, rare: a specific string
  public static void exactFalse(float f) { String s = String.valueOf(f); assert !s.equals("2.75"); }
  // true: equality to a string determines the value
  public static void exactTrue(float f) { String s = String.valueOf(f); if(s.equals("2.75")) assert f == 2.75f; }
  // length only
  public static void lengthFalse(float f) { String s = String.valueOf(f); assert s.length() != 3; }
  public static void lengthTrue(float f) { String s = String.valueOf(f); assert s.length() >= 3; }
  // range-restricted value
  public static void rangeTrue(float f) { if(f < 1 || f >= 2) return; String s = String.valueOf(f); assert s.charAt(0) == '1'; }
}
