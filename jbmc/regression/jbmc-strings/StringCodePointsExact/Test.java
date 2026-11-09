public class Test {
  public static void cpc(String s) { if(s == null || !s.equals("abc")) return; assert s.codePointCount(0, 3) == 3; }
  public static void cpcBad(String s) { if(s == null || !s.equals("abc")) return; assert s.codePointCount(0, 3) != 3; }
  public static void cpcPair(String s) { if(s == null || s.length() != 2 || s.charAt(0) != '\uD83D' || s.charAt(1) != '\uDE00') return; assert s.codePointCount(0, 2) == 1; }
  public static void obc(String s) { if(s == null || !s.equals("abc")) return; assert s.offsetByCodePoints(0, 2) == 2; }
  public static void obcNeg(String s) { if(s == null || !s.equals("abc")) return; assert s.offsetByCodePoints(3, -2) == 1; }
  public static void obcNegBad(String s) { if(s == null || !s.equals("abc")) return; assert s.offsetByCodePoints(3, -2) != 1; }
  public static void nonConst(String f) { if(f == null) return; String s = String.format(f, 1); if(s.equals("x")) assert false; }
}
