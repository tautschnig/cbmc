import java.util.Locale;
public class Test {
  public static void ascii() { String r = String.format("%d|%.2f|%05d|%e", -12, 1.5, 3, 2.0); assert r.equals("-12|1.50|00003|2.000000e+00"); }
  public static void asciiBad() { String r = String.format("%d|%.2f|%05d|%e", -12, 1.5, 3, 2.0); assert !r.equals("-12|1.50|00003|2.000000e+00"); }
  public static void arabic() { String r = String.format("%d|%.1f|%05d", -12, 1.5, 3); assert r.equals("-\u0661\u0662|\u0661\u066b\u0665|\u0660\u0660\u0660\u0660\u0663") || r.equals("-12|1.5|00003"); }
  public static void arabicBad() { String r = String.format("%d|%.1f|%05d", -12, 1.5, 3); assert r.equals("-12|1.5|00003"); }
  public static void mexican() { String r = String.format("%.1f", 1.5); assert r.equals("1.5") || r.equals("1,5"); }
  public static void mexicanBad() { String r = String.format("%.1f", 1.5); assert r.equals("1.5"); }
  public static void german() { String r = String.format("%.2f", 1.5); assert r.equals("1,50"); }
  public static void turkish() { String r = String.format("%S|%x|%X", "i", 255, 255); assert r.equals("\u0130|ff|FF"); }
  public static void anyLength(int x) { String r = String.format("%05d", x); assert r.length() >= 5; }
  public static void anyNotAscii() { String r = String.format("%d", 7); assert r.equals("7"); }
  public static void anyHex() { String r = String.format("%x|%a", 255, 1.0); assert r.equals("ff|0x1.0p0"); }
  public static void anyUpperI() { String r = String.format("%S", "i"); assert r.equals("I") || r.equals("\u0130"); }
  public static void nullLocale() { String r = String.format((Locale) null, "%d|%.1f", 12, 1.5); assert r.equals("12|1.5"); }
  public static void explicitLocale(Locale l) { if (l == null) return; String r = String.format(l, "%d", 7); assert r.equals("7"); }
  public static void explicitLength(Locale l) { if (l == null) return; String r = String.format(l, "%d", 7); assert r.length() == 1; }

  public static void upperI() { String r = "i".toUpperCase(); assert r.equals("I"); }
  public static void upperIAny() { String r = "i".toUpperCase(); assert r.equals("I") || r.equals("\u0130"); }
  public static void upperIAnyBad() { String r = "i".toUpperCase(); assert r.equals("I"); }
  public static void lowerI() { String r = "I".toLowerCase(); assert r.equals("i"); }
  public static void lowerITr() { String r = "I".toLowerCase(); assert r.equals("\u0131"); }
  public static void upperITr() { String r = "i".toUpperCase(); assert r.equals("\u0130"); }
  public static void lowerJLt() { String r = "J\u0300".toLowerCase(); assert r.length() == 3; }
  public static void plain() { String r = "abc".toUpperCase(); assert r.equals("ABC"); }
  public static void locale(Locale l) { if (l == null) return; String r = "i".toUpperCase(l); assert r.equals("I"); }
  public static void ignoreCase() { assert "i".equalsIgnoreCase("I"); }
  public static void dottedLength(String s) { if (s == null || s.length() != 1 || s.charAt(0) != 'I') return; assert s.toLowerCase().length() == 1; }
}
