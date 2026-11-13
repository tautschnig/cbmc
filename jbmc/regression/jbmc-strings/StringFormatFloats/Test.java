public class Test {
  public static void c0() { String r = String.format("%f", 1.5); assert r.equals("1.500000"); }
  public static void c0Bad() { String r = String.format("%f", 1.5); assert !r.equals("1.500000"); }
  public static void c1() { String r = String.format("%f", -0.0); assert r.equals("-0.000000"); }
  public static void c1Bad() { String r = String.format("%f", -0.0); assert !r.equals("-0.000000"); }
  public static void c2() { String r = String.format("%.2f", 1.005); assert r.equals("1.01"); }
  public static void c2Bad() { String r = String.format("%.2f", 1.005); assert !r.equals("1.01"); }
  public static void c3() { String r = String.format("%.2f", 0.125); assert r.equals("0.13"); }
  public static void c3Bad() { String r = String.format("%.2f", 0.125); assert !r.equals("0.13"); }
  public static void c4() { String r = String.format("%.0f", 2.5); assert r.equals("3"); }
  public static void c4Bad() { String r = String.format("%.0f", 2.5); assert !r.equals("3"); }
  public static void c5() { String r = String.format("%.3f", 1e-4); assert r.equals("0.000"); }
  public static void c5Bad() { String r = String.format("%.3f", 1e-4); assert !r.equals("0.000"); }
  public static void c6() { String r = String.format("%f", 1e23); assert r.equals("100000000000000000000000.000000"); }
  public static void c6Java17() { String r = String.format("%f", 1e23); assert r.equals("99999999999999990000000.000000"); }
  public static void c6Java17Bad() { String r = String.format("%f", 1e23); assert !r.equals("99999999999999990000000.000000"); }
  public static void c6Any() { String r = String.format("%f", 1e23); assert r.equals("100000000000000000000000.000000") || r.equals("99999999999999990000000.000000"); }
  public static void c6AnyBad() { String r = String.format("%f", 1e23); assert r.equals("100000000000000000000000.000000"); }
  public static void toString23(double d) { if(d != 1e23) return; String r = Double.toString(d); assert r.equals("1.0E23"); }
  public static void toString23Java17(double d) { if(d != 1e23) return; String r = Double.toString(d); assert r.equals("9.999999999999999E22"); }
  public static void toString23Any(double d) { if(d != 1e23) return; String r = Double.toString(d); assert r.equals("1.0E23") || r.equals("9.999999999999999E22"); }
  public static void toString23AnyBad(double d) { if(d != 1e23) return; String r = Double.toString(d); assert r.equals("1.0E23"); }
  public static void indexZero() { String r = String.format("%0$s", "a"); assert r.equals("a"); }
  public static void indexZeroBad() { String r = String.format("%0$s", "a"); assert !r.equals("a"); }
  public static void c6Bad() { String r = String.format("%f", 1e23); assert !r.equals("100000000000000000000000.000000"); }
  public static void c7() { String r = String.format("%.1f", Double.NaN); assert r.equals("NaN"); }
  public static void c7Bad() { String r = String.format("%.1f", Double.NaN); assert !r.equals("NaN"); }
  public static void c8() { String r = String.format("%f", Double.NEGATIVE_INFINITY); assert r.equals("-Infinity"); }
  public static void c8Bad() { String r = String.format("%f", Double.NEGATIVE_INFINITY); assert !r.equals("-Infinity"); }
  public static void c9() { String r = String.format("%e", 123.456); assert r.equals("1.234560e+02"); }
  public static void c9Bad() { String r = String.format("%e", 123.456); assert !r.equals("1.234560e+02"); }
  public static void c10() { String r = String.format("%.0e", 9.5); assert r.equals("1e+01"); }
  public static void c10Bad() { String r = String.format("%.0e", 9.5); assert !r.equals("1e+01"); }
  public static void c11() { String r = String.format("%.3e", -0.0001234567); assert r.equals("-1.235e-04"); }
  public static void c11Bad() { String r = String.format("%.3e", -0.0001234567); assert !r.equals("-1.235e-04"); }
  public static void c12() { String r = String.format("%E", Double.POSITIVE_INFINITY); assert r.equals("INFINITY"); }
  public static void c12Bad() { String r = String.format("%E", Double.POSITIVE_INFINITY); assert !r.equals("INFINITY"); }
  public static void c13() { String r = String.format("%e", 0.0); assert r.equals("0.000000e+00"); }
  public static void c13Bad() { String r = String.format("%e", 0.0); assert !r.equals("0.000000e+00"); }
  public static void c14() { String r = String.format("%g", 123456.0); assert r.equals("123456"); }
  public static void c14Bad() { String r = String.format("%g", 123456.0); assert !r.equals("123456"); }
  public static void c15() { String r = String.format("%g", 1234567.0); assert r.equals("1.23457e+06"); }
  public static void c15Bad() { String r = String.format("%g", 1234567.0); assert !r.equals("1.23457e+06"); }
  public static void c16() { String r = String.format("%g", 0.0001); assert r.equals("0.000100000"); }
  public static void c16Bad() { String r = String.format("%g", 0.0001); assert !r.equals("0.000100000"); }
  public static void c17() { String r = String.format("%g", 0.00001); assert r.equals("1.00000e-05"); }
  public static void c17Bad() { String r = String.format("%g", 0.00001); assert !r.equals("1.00000e-05"); }
  public static void c18() { String r = String.format("%.0g", 0.0); assert r.equals("0"); }
  public static void c18Bad() { String r = String.format("%.0g", 0.0); assert !r.equals("0"); }
  public static void c19() { String r = String.format("%G", 1e-10); assert r.equals("1.00000E-10"); }
  public static void c19Bad() { String r = String.format("%G", 1e-10); assert !r.equals("1.00000E-10"); }
  public static void c20() { String r = String.format("%a", 1.0); assert r.equals("0x1.0p0"); }
  public static void c20Bad() { String r = String.format("%a", 1.0); assert !r.equals("0x1.0p0"); }
  public static void c21() { String r = String.format("%a", -0.1); assert r.equals("-0x1.999999999999ap-4"); }
  public static void c21Bad() { String r = String.format("%a", -0.1); assert !r.equals("-0x1.999999999999ap-4"); }
  public static void c22() { String r = String.format("%.2a", 0.1); assert r.equals("0x1.9ap-4"); }
  public static void c22Bad() { String r = String.format("%.2a", 0.1); assert !r.equals("0x1.9ap-4"); }
  public static void c23() { String r = String.format("%a", Double.MIN_VALUE); assert r.equals("0x0.0000000000001p-1022"); }
  public static void c23Bad() { String r = String.format("%a", Double.MIN_VALUE); assert !r.equals("0x0.0000000000001p-1022"); }
  public static void c24() { String r = String.format("%.1a", Double.MIN_VALUE); assert r.equals("0x1.0p-1074"); }
  public static void c24Bad() { String r = String.format("%.1a", Double.MIN_VALUE); assert !r.equals("0x1.0p-1074"); }
  public static void c25() { String r = String.format("%A", 255.5); assert r.equals("0X1.FFP7"); }
  public static void c25Bad() { String r = String.format("%A", 255.5); assert !r.equals("0X1.FFP7"); }
  public static void c26() { String r = String.format("%f", 0.1f); assert r.equals("0.100000"); }
  public static void c26Bad() { String r = String.format("%f", 0.1f); assert !r.equals("0.100000"); }
  public static void c27() { String r = String.format("%.10f", 0.1f); assert r.equals("0.1000000015"); }
  public static void c27Bad() { String r = String.format("%.10f", 0.1f); assert !r.equals("0.1000000015"); }
  public static void c28() { String r = String.format("%e", Float.MAX_VALUE); assert r.equals("3.402823e+38"); }
  public static void c28Bad() { String r = String.format("%e", Float.MAX_VALUE); assert !r.equals("3.402823e+38"); }
  public static void c29() { String r = String.format("%a", 1.5f); assert r.equals("0x1.8p0"); }
  public static void c29Bad() { String r = String.format("%a", 1.5f); assert !r.equals("0x1.8p0"); }
  public static void c30() { String r = String.format("%f", (Object) null); assert r.equals("null"); }
  public static void c30Bad() { String r = String.format("%f", (Object) null); assert !r.equals("null"); }
  public static void c31() { String r = String.format("%.2f %<e", 2.0 / 3); assert r.equals("0.67 6.666667e-01"); }
  public static void c31Bad() { String r = String.format("%.2f %<e", 2.0 / 3); assert !r.equals("0.67 6.666667e-01"); }
  public static void c32() { String r = String.format("%2$.1f%1$g", 1.25, 2.25); assert r.equals("2.31.25000"); }
  public static void c32Bad() { String r = String.format("%2$.1f%1$g", 1.25, 2.25); assert !r.equals("2.31.25000"); }
  public static void c33Bad() { String r = String.format("%f", new java.math.BigDecimal("1.5")); assert !r.equals("1.500000"); }
  public static void s0(double d) { if(d == 2.5) { String r = String.format("%.1f|%e|%a", d, d, d); assert r.equals("2.5|2.500000e+00|0x1.4p1"); } }
  public static void s0Bad(double d) { if(d == 2.5) { String r = String.format("%.1f|%e|%a", d, d, d); assert !r.equals("2.5|2.500000e+00|0x1.4p1"); } }
  public static void s1Bad(double d) { assert !String.format("%.2f", d).equals("3.14"); }
  public static void s2(double d) { if(String.format("%.2f", d).equals("3.14")) assert d >= 3.135 && d < 3.145; }
  public static void s2Bad(double d) { if(String.format("%.2f", d).equals("3.14")) assert d >= 3.135 && d < 3.1449; }
  public static void s3(double d) { if(String.format("%e", d).equals("1.500000e+00")) assert d > 1.499999 && d < 1.500001; }
  public static void s4(double d) { assert !String.format("%.2f", d).equals("3.1"); }
  public static void s5Bad(float f) { assert !String.format("%a", f).equals("0x1.8p1"); }
}
