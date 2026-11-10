public class Test {
  public static void replaceSymbolic(String t) { if(t == null || t.length() != 2) return; String r = "abcd".replace(t, "X"); assert r.length() <= 4; }
  public static void regexSymbolic(String s) { if(s == null) return; if(s.matches("a+")) assert s.length() > 0; }
  public static void regexConstant() { assert "aaa".matches("a+"); }
}
