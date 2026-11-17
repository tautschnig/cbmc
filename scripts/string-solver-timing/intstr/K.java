import org.cprover.CProver;
public class K {
  public static void free1(int x0) { String s = "[" + x0 + "]"; assert !s.equals("[7]"); }
  public static void merged1() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; String s = "[" + y0 + "]"; assert s.length() > 2; }
  public static void free2(int x0, int x1) { String s = "[" + x0 + ", " + x1 + "]"; assert !s.equals("[7, 17]"); }
  public static void merged2() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; String s = "[" + y0 + ", " + y1 + "]"; assert s.length() > 2; }
  public static void free3(int x0, int x1, int x2) { String s = "[" + x0 + ", " + x1 + ", " + x2 + "]"; assert !s.equals("[7, 17, 27]"); }
  public static void merged3() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; int y2 = g ? 2 : -12; String s = "[" + y0 + ", " + y1 + ", " + y2 + "]"; assert s.length() > 2; }
  public static void free4(int x0, int x1, int x2, int x3) { String s = "[" + x0 + ", " + x1 + ", " + x2 + ", " + x3 + "]"; assert !s.equals("[7, 17, 27, 37]"); }
  public static void merged4() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; int y2 = g ? 2 : -12; int y3 = g ? 3 : -13; String s = "[" + y0 + ", " + y1 + ", " + y2 + ", " + y3 + "]"; assert s.length() > 2; }
  public static void free5(int x0, int x1, int x2, int x3, int x4) { String s = "[" + x0 + ", " + x1 + ", " + x2 + ", " + x3 + ", " + x4 + "]"; assert !s.equals("[7, 17, 27, 37, 47]"); }
  public static void merged5() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; int y2 = g ? 2 : -12; int y3 = g ? 3 : -13; int y4 = g ? 4 : -14; String s = "[" + y0 + ", " + y1 + ", " + y2 + ", " + y3 + ", " + y4 + "]"; assert s.length() > 2; }
  public static void free6(int x0, int x1, int x2, int x3, int x4, int x5) { String s = "[" + x0 + ", " + x1 + ", " + x2 + ", " + x3 + ", " + x4 + ", " + x5 + "]"; assert !s.equals("[7, 17, 27, 37, 47, 57]"); }
  public static void merged6() { boolean g = CProver.nondetBoolean(); int y0 = g ? 0 : -10; int y1 = g ? 1 : -11; int y2 = g ? 2 : -12; int y3 = g ? 3 : -13; int y4 = g ? 4 : -14; int y5 = g ? 5 : -15; String s = "[" + y0 + ", " + y1 + ", " + y2 + ", " + y3 + ", " + y4 + ", " + y5 + "]"; assert s.length() > 2; }
}
