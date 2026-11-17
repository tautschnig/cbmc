public class Test {
  public static void nanConst() { assert Float.floatToRawIntBits(Float.NaN) == 0x7fc00000; assert Double.doubleToRawLongBits(Double.NaN) == 0x7ff8000000000000L; }
  public static void payload() { assert Float.floatToRawIntBits(Float.intBitsToFloat(0x7fc00123)) == 0x7fc00123; }
  // a computed NaN may have the bits of HotSpot on x86-64 (JBMC proved it never had)
  public static void computedMayBeX86(float x) { float n = (x - x) / (x - x); assert Float.floatToRawIntBits(n) != 0xffc00000; }
  public static void computedIsNaN(float x) { float n = (x - x) / (x - x); int b = Float.floatToRawIntBits(n); assert (b & 0x7f800000) == 0x7f800000 && (b & 0x7fffff) != 0; }
  public static void canonical(float x) { float n = (x - x) / (x - x); assert Float.floatToIntBits(n) == 0x7fc00000; }
  public static void roundTrip(long x) { if (!Double.isNaN(Double.longBitsToDouble(x))) assert Double.doubleToRawLongBits(Double.longBitsToDouble(x)) == x; }
  public static void values() { assert Double.longBitsToDouble(0x3ff0000000000000L) == 1.0; assert Float.intBitsToFloat(0xbf800000) == -1.0f; assert Float.floatToRawIntBits(-0.0f) == 0x80000000; }
  public static void main(String[] a) { nanConst(); payload(); computedIsNaN(1); canonical(1); roundTrip(5); values(); System.out.println("ok"); }
}
