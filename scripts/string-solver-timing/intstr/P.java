import org.cprover.CProver;
public class P {
  public static void eq1(int x0) { String s = "[" + x0 + "]"; if (s.equals("[7]")) assert x0 == 7; }
  public static void mergedEq1() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; String s = "[" + y0 + "]"; assert s.equals("[0]") || s.equals("[-10]"); }
  public static void eq2(int x0, int x1) { String s = "[" + x0 + ", " + x1 + "]"; if (s.equals("[7, 17]")) assert x0 == 7 && x1 == 17; }
  public static void mergedEq2() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; String s = "[" + y0 + ", " + y1 + "]"; assert s.equals("[0, 1]") || s.equals("[-10, -11]"); }
  public static void eq3(int x0, int x1, int x2) { String s = "[" + x0 + ", " + x1 + ", " + x2 + "]"; if (s.equals("[7, 17, 27]")) assert x0 == 7 && x1 == 17 && x2 == 27; }
  public static void mergedEq3() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; int y2 = g ? 2 : -12; String s = "[" + y0 + ", " + y1 + ", " + y2 + "]"; assert s.equals("[0, 1, 2]") || s.equals("[-10, -11, -12]"); }
  public static void eq4(int x0, int x1, int x2, int x3) { String s = "[" + x0 + ", " + x1 + ", " + x2 + ", " + x3 + "]"; if (s.equals("[7, 17, 27, 37]")) assert x0 == 7 && x1 == 17 && x2 == 27 && x3 == 37; }
  public static void mergedEq4() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; int y2 = g ? 2 : -12; int y3 = g ? 3 : -13; String s = "[" + y0 + ", " + y1 + ", " + y2 + ", " + y3 + "]"; assert s.equals("[0, 1, 2, 3]") || s.equals("[-10, -11, -12, -13]"); }
  public static void inj1(int x, int y) { if (("" + x).equals("" + y)) assert x == y; }
  public static void injPos(int x, int y) { if (x >= 0 && y >= 0 && x < 1000 && y < 1000 && ("" + x).equals("" + y)) assert x == y; }
  public static void lenBound(int x) { if (x >= 0 && x < 100) assert ("" + x).length() <= 2; }
  public static void digitsOnly(int x) { if (x >= 0) { String s = "" + x; for (int i = 0; i < s.length(); i++) assert s.charAt(i) >= 48 && s.charAt(i) <= 57; } }
}
