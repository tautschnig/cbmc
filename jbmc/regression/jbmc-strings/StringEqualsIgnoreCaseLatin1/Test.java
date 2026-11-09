public class Test {
  public static void latin(String s) { if(s == null || !s.equals("\u00e9t\u00e9")) return; assert s.equalsIgnoreCase("\u00c9T\u00c9"); }
  public static void latinBad(String s) { if(s == null || !s.equals("\u00e9t\u00e9")) return; assert !s.equalsIgnoreCase("\u00c9T\u00c9"); }
  public static void ascii(String s) { if(s == null || !s.equals("abc")) return; assert s.equalsIgnoreCase("ABC"); }
  public static void asciiNe(String s) { if(s == null || !s.equals("abc")) return; assert !s.equalsIgnoreCase("ABD"); }
  public static void greek(String s) { if(s == null || !s.equals("\u03c0")) return; assert s.equalsIgnoreCase("\u03a0"); }
  public static void greekBad(String s) { if(s == null || !s.equals("\u03c0")) return; assert !s.equalsIgnoreCase("\u03a0"); }
}
