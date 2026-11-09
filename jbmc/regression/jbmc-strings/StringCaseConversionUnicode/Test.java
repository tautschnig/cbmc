public class Test {
  public static void latin(String s) { if(s == null || !s.equals("\u00e9t\u00e9")) return; assert s.toUpperCase().equals("\u00c9T\u00c9"); }
  public static void sharpS(String s) { if(s == null || !s.equals("\u00df")) return; assert s.toUpperCase().equals("\u00df"); }
  public static void sharpSLength(String s) { if(s == null || !s.equals("a\u00dfb")) return; String u = s.toUpperCase(); assert u.length() == 3; }
  public static void greekUpper(String s) { if(s == null || !s.equals("\u03c0")) return; assert s.toUpperCase().equals("\u03c0"); }
  public static void lastKept(String s) { if(s == null || s.length() < 2 || !s.endsWith("A")) return; assert s.toLowerCase().endsWith("a"); }
}
