public class Test {
  public static void width(int i) { if(i != 7) return; String s = String.format("%5d", i); assert s.length() == 5; }
  public static void widthBad(int i) { if(i != 7) return; String s = String.format("%5d", i); assert s.length() == 1; }
  public static void prec(String x) { if(x == null || !x.equals("abc")) return; String s = String.format("%.2s", x); assert s.length() == 2; }
  public static void rel(String x, String y) { if(x == null || y == null || !x.equals("a") || !y.equals("bb")) return; String s = String.format("%s%<s", x, y); assert s.length() == 2; }
  public static void relBad(String x, String y) { if(x == null || y == null || !x.equals("a") || !y.equals("bb")) return; String s = String.format("%s%<s", x, y); assert s.length() == 3; }
  public static void date(long t) { String s = String.format("%tY", t); assert s.length() == 4; }
  public static void plain(int i) { if(i != 7) return; String s = String.format("%d", i); assert s.length() == 1; }
}
