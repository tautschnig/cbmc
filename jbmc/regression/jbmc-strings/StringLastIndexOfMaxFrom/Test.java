public class Test {
  public static void maxFrom(String s) { if(s == null || !s.equals("abca")) return; assert s.lastIndexOf('a', Integer.MAX_VALUE) == 3; }
  public static void maxFromBad(String s) { if(s == null || !s.equals("abca")) return; assert s.lastIndexOf('a', Integer.MAX_VALUE) != 3; }
  public static void maxFromSym(String s, int k) { if(s == null || s.length() != 2 || s.charAt(1) != 'x' || k != Integer.MAX_VALUE) return; assert s.lastIndexOf('x', k) == 1; }
  public static void maxFromStr(String s) { if(s == null || !s.equals("abca")) return; assert s.lastIndexOf("a", Integer.MAX_VALUE) == 3; }
}
