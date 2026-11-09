public class Test {
  public static void hexUpper(int i) { if(i != 255) return; String s = String.format("%X", i); if(s.equals("FF")) return; assert false; }
  public static void hexUpperBad(int i) { if(i != 255) return; String s = String.format("%X", i); if(s.equals("ff")) assert false; }
  public static void hexNegBad(int i) { if(i != -1) return; String s = String.format("%x", i); assert s.equals("-1"); }
  public static void hexPos(int i) { if(i != 4096) return; String s = String.format("%x", i); assert s.equals("1000"); }
  public static void longD(long x) { if(x != 12345678901L) return; String s = String.format("%d", x); assert s.length() == 11; }
  public static void minD(long x) { if(x != Long.MIN_VALUE) return; String s = String.format("%d", x); assert s.length() == 20; }
  public static void minDBad(long x) { if(x != Long.MIN_VALUE) return; String s = String.format("%d", x); assert s.length() != 20; }
  public static void upperE(double d) { String s = String.format("%E", d); assert s.length() != 7; }
  public static void upperEVal(double d) { if(d != 1.5) return; String s = String.format("%E", d); assert s.equals("1.500000E+00"); }
}
