public class Test {
  // equals and length
  public static void eq(String s) { if(s == null || !s.equals("abc")) return; assert s.length() == 3; }
  public static void eqTwo(String s, String t) { if(s == null || t == null || !s.equals("abc") || t.length() != 0) return; assert s.length() == 3; }
  // int to string, constant arguments
  public static void intZero(int i) { if(i != 0) return; assert Integer.toString(i).length() == 1; }
  public static void intZeroBad(int i) { if(i != 0) return; assert Integer.toString(i).length() != 1; }
  public static void intBig(int i) { if(i != 2000000000) return; assert Integer.toString(i).length() == 10; }
  public static void intMinEq(int i) { if(i != Integer.MIN_VALUE) return; assert Integer.toString(i).equals("-2147483648"); }
  public static void longBig(long x) { if(x != 12345678901L) return; assert Long.toString(x).length() == 11; }
  public static void longMin(long x) { if(x != Long.MIN_VALUE) return; assert Long.toString(x).length() == 20; }
  public static void hexRadix(int i) { if(i != 255) return; assert Integer.toString(i, 16).length() == 2; }
  public static void builderAppend(int i) { if(i != 0) return; StringBuilder b = new StringBuilder(); b.append(i); assert b.toString().length() == 1; }
  // indexOf from beyond the end
  public static void indexOfCharMax(String s) { if(s == null || !s.equals("abca")) return; assert s.indexOf('a', Integer.MAX_VALUE) == -1; }
  // replace with constant arguments
  public static void replaceLiteral() { assert "abcbc".replace("bc", "X").equals("aXX"); }
  public static void replaceLiteralBad() { assert !"abcbc".replace("bc", "X").equals("aXX"); }
  public static void replaceBuilt() { String s = new StringBuilder("ab").append("cbc").toString(); assert s.replace("bc", "X").equals("aXX"); }
  public static void replaceChar(String s) { if(s == null || !s.equals("abca")) return; assert s.replace('a', 'z').equals("zbcz"); }
  public static void replaceCharBad(String s) { if(s == null || !s.equals("abca")) return; assert !s.replace('a', 'z').equals("zbcz"); }
  // format specifiers that were modelled before
  public static void formatUpperS(String x) { if(x == null || !x.equals("ab")) return; assert String.format("%S", x).equals("AB"); }
  public static void formatD(int i) { if(i != -42) return; assert String.format("%d", i).equals("-42"); }
  public static void formatDBad(int i) { if(i != -42) return; assert !String.format("%d", i).equals("-42"); }
  // float printing that was already right
  public static void floatSmall(float f) { if(f != 1.5f) return; assert String.valueOf(f).equals("1.5"); }
  public static void floatSmallBad(float f) { if(f != 1.5f) return; assert !String.valueOf(f).equals("1.5"); }
  // case conversion of ASCII
  public static void upperAscii(String s) { if(s == null || !s.equals("abC1")) return; assert s.toUpperCase().equals("ABC1"); }
  public static void lowerAsciiBad(String s) { if(s == null || !s.equals("abC1")) return; assert !s.toLowerCase().equals("abc1"); }
}
