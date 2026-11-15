import org.cprover.CProver;

public class Test {
  public static void plain() { String r = String.format("%,d", 1234567); assert r.equals("1,234,567"); }
  public static void plainBad() { String r = String.format("%,d", 1234567); assert !r.equals("1,234,567"); }
  public static void negative() { String r = String.format("%,d", -1234567); assert r.equals("-1,234,567"); }
  public static void minLong() { String r = String.format("%,d", Long.MIN_VALUE); assert r.equals("-9,223,372,036,854,775,808"); }
  public static void short3() { String r = String.format("%,d", 999); assert r.equals("999"); }
  public static void zeros() { String r = String.format("%,010d", 1234); assert r.equals("000001,234"); }
  public static void parens() { String r = String.format("%(,d", -1234); assert r.equals("(1,234)"); }
  public static void plus() { String r = String.format("%+,d", 1000); assert r.equals("+1,000"); }
  public static void fixed() { String r = String.format("%,.2f", 1234567.891); assert r.equals("1,234,567.89"); }
  public static void general() { String r = String.format("%,g", 12345.678); assert r.equals("12,345.7"); }
  public static void fixedZeros() { String r = String.format("%,08.1f", -1234.5); assert r.equals("-1,234.5"); }
  public static void german() { String r = String.format("%,.2f", 1234567.891); assert r.equals("1.234.567,89"); }
  public static void french() { String r = String.format("%,d", 1234); assert r.equals("1\u00a0234") || r.equals("1\u202f234"); }
  public static void frenchBad() { String r = String.format("%,d", 1234); assert r.equals("1\u202f234"); }
  public static void toki() { String r = String.format("%,d", 12345); assert r.equals("1\u00a023\u00a045") || r.equals("12,345"); }
  public static void tokiBad() { String r = String.format("%,d", 12345); assert r.equals("12,345"); }
  public static void nullLocale() { String r = String.format((java.util.Locale) null, "%,d", 1234567); assert r.equals("1,234,567"); }
  public static void armenian9() {
    try { String.format("%,d", 12); assert false; } catch (ArithmeticException e) { }
  }
  public static void armenian9One() { String r = String.format("%,d", 5); assert r.equals("5"); }
  public static void armenian17() {
    try { String.format("%,d", 12); } catch (ArithmeticException e) { assert false; }
  }
  public static void anyLocale() {
    try { String.format("%,d", 12); } catch (ArithmeticException e) { assert false; }
  }
  public static void symbolic() {
    int x = CProver.nondetInt();
    String r = String.format("%,d", x);
    assert r.length() <= 14;
  }
  public static void symbolicBad() {
    int x = CProver.nondetInt();
    String r = String.format("%,d", x);
    assert r.length() <= 13;
  }
}
