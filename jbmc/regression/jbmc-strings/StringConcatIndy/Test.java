public class Test {
  public static void str() { String a = "x"; String s = a + "y"; assert s.equals("xy"); }
  public static void integer() { Integer i = 3; String s = "" + i; assert s.equals("3"); }
  public static void prim() { int i = 3; long l = -4; char c = 'q'; boolean b = true; double d = 1.5; String s = "v" + i + l + c + b + d; assert s.equals("v3-4qtrue1.5"); }
  public static void obj() { Object o = new StringBuilder("q"); String s = "<" + o + ">"; assert s.equals("<q>"); }
  public static void nul() { String n = null; String s = "a" + n; assert s.equals("anull"); }
  public static void bytes() { byte x = -3; short y = 7; String s = x + "," + y; assert s.equals("-3,7"); }
  public static void special() { String a = "\u00e9"; String s = a + "\u0001\u0002" + a; assert s.length() == 4; }
  public static void neg() { String a = "x"; String s = a + "y"; assert !s.equals("xy"); }
  public static void sym(String a) { if (a == null || a.length() != 2) return; String s = "[" + a + "]"; assert s.length() == 4 && s.charAt(0) == '['; }
  public static void main(String[] args) { str(); integer(); prim(); obj(); nul(); bytes(); special(); sym("ab"); System.out.println("ok"); }
}
