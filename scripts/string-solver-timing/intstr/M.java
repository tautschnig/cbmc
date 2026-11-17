import org.cprover.CProver;
public class M {
  // one of two constants, as symex merges the versions
  public static void merged1() { boolean g = CProver.nondetBoolean(); int a = g ? -1 : 2; String s = "" + a; assert s.equals("-1") || s.equals("2"); }
  public static void merged3() { boolean g = CProver.nondetBoolean(); int a = g ? -1 : 2; int b = g ? 0 : 1; int l = g ? 0 : 3; String s = "begin " + a + ", end " + b + ", length " + l; assert s.equals("begin -1, end 0, length 0") || s.equals("begin 2, end 1, length 3"); }
  public static void mergedMsg() { int v = CProver.nondetInt(); CProver.assume(v >= 8 && v <= 25); int a = v <= 8 ? -1 : 2; String s = v <= 8 ? "String index out of range: " + a : v <= 17 ? "begin " + a + ", end 1, length 3" : "Range [" + a + ", 1) out of bounds for length 3"; assert !s.equals("x"); }
  public static void boundedSym(int i) { if (i < 0 || i > 100) return; String s = "Index " + i + " out of bounds for length 3"; assert !s.equals("Index 7 out of bounds for length 3"); }
  public static void symEq(int i) { String s = "Index " + i; assert !s.equals("Index 12345"); }
  public static void symLong(long i) { String s = "x" + i + "y"; assert s.length() >= 3; }
}
