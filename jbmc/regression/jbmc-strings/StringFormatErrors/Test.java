public class Test {
  public static void c0() { try { String.format("%q", 1); assert false; } catch(java.util.UnknownFormatConversionException e) { assert e.getMessage().equals("Conversion = 'q'"); } }
  public static void c0Bad() { try { String.format("%q", 1); } catch(java.util.UnknownFormatConversionException e) { assert !e.getMessage().equals("Conversion = 'q'"); } }
  public static void c1() { try { String.format("%0$d", 1); assert false; } catch(java.util.IllegalFormatException e) { assert e.getClass().getName().equals("java.util.IllegalFormatArgumentIndexException") && e.getMessage().equals("Illegal format argument index = 0"); } }
  public static void c1Bad() { try { String.format("%0$d", 1); } catch(java.util.IllegalFormatException e) { assert !e.getClass().getName().equals("java.util.IllegalFormatArgumentIndexException"); } }
  public static void c2() { try { String.format("%99999999999$d", 1); assert false; } catch(java.util.IllegalFormatException e) { assert e.getClass().getName().equals("java.util.IllegalFormatArgumentIndexException") && e.getMessage().equals("Format argument index: (not representable as int)"); } }
  public static void c2Bad() { try { String.format("%99999999999$d", 1); } catch(java.util.IllegalFormatException e) { assert !e.getClass().getName().equals("java.util.IllegalFormatArgumentIndexException"); } }
  public static void c3() { try { String.format("%--d", 1); assert false; } catch(java.util.DuplicateFormatFlagsException e) { assert e.getMessage().equals("Flags = '-'"); } }
  public static void c3Bad() { try { String.format("%--d", 1); } catch(java.util.DuplicateFormatFlagsException e) { assert !e.getMessage().equals("Flags = '-'"); } }
  public static void c4() { try { String.format("%99999999999d", 1); assert false; } catch(java.util.IllegalFormatWidthException e) { assert e.getMessage().equals("-2147483648"); } }
  public static void c4Bad() { try { String.format("%99999999999d", 1); } catch(java.util.IllegalFormatWidthException e) { assert !e.getMessage().equals("-2147483648"); } }
  public static void c5() { try { String.format("%.99999999999f", 1.0); assert false; } catch(java.util.IllegalFormatPrecisionException e) { assert e.getMessage().equals("-2147483648"); } }
  public static void c5Bad() { try { String.format("%.99999999999f", 1.0); } catch(java.util.IllegalFormatPrecisionException e) { assert !e.getMessage().equals("-2147483648"); } }
  public static void c6() { try { String.format("%+ d", 1); assert false; } catch(java.util.IllegalFormatFlagsException e) { assert e.getMessage().equals("Flags = '+ '"); } }
  public static void c6Bad() { try { String.format("%+ d", 1); } catch(java.util.IllegalFormatFlagsException e) { assert !e.getMessage().equals("Flags = '+ '"); } }
  public static void c7() { try { String.format("%#d", 1); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = d, Flags = #"); } }
  public static void c7Bad() { try { String.format("%#d", 1); } catch(java.util.FormatFlagsConversionMismatchException e) { assert !e.getMessage().equals("Conversion = d, Flags = #"); } }
  public static void c8() { try { String.format("%-d", 1); assert false; } catch(java.util.MissingFormatWidthException e) { assert e.getMessage().equals("%-d"); } }
  public static void c8Bad() { try { String.format("%-d", 1); } catch(java.util.MissingFormatWidthException e) { assert !e.getMessage().equals("%-d"); } }
  public static void c9() { try { String.format("%.2c", 'a'); assert false; } catch(java.util.IllegalFormatPrecisionException e) { assert e.getMessage().equals("2"); } }
  public static void c9Bad() { try { String.format("%.2c", 'a'); } catch(java.util.IllegalFormatPrecisionException e) { assert !e.getMessage().equals("2"); } }
  public static void c10() { try { String.format("%5n", 1); assert false; } catch(java.util.IllegalFormatWidthException e) { assert e.getMessage().equals("5"); } }
  public static void c10Bad() { try { String.format("%5n", 1); } catch(java.util.IllegalFormatWidthException e) { assert !e.getMessage().equals("5"); } }
  public static void c11() { try { String.format("%tq", 1L); assert false; } catch(java.util.UnknownFormatConversionException e) { assert e.getMessage().equals("Conversion = 'tq'"); } }
  public static void c11Bad() { try { String.format("%tq", 1L); } catch(java.util.UnknownFormatConversionException e) { assert !e.getMessage().equals("Conversion = 'tq'"); } }
  public static void c12() { try { String.format("%d %q", "x"); assert false; } catch(java.util.UnknownFormatConversionException e) { assert e.getMessage().equals("Conversion = 'q'"); } }
  public static void c12Bad() { try { String.format("%d %q", "x"); } catch(java.util.UnknownFormatConversionException e) { assert !e.getMessage().equals("Conversion = 'q'"); } }
  public static void c13() { try { String.format("%,x", 1); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = x, Flags = ,"); } }
  public static void c13Bad() { try { String.format("%,x", 1); } catch(java.util.FormatFlagsConversionMismatchException e) { assert !e.getMessage().equals("Conversion = x, Flags = ,"); } }
  public static void c14() { try { String.format("%d %d", 1); assert false; } catch(java.util.MissingFormatArgumentException e) { assert e.getMessage().equals("Format specifier '%d'"); } }
  public static void c14Bad() { try { String.format("%d %d", 1); } catch(java.util.MissingFormatArgumentException e) { assert !e.getMessage().equals("Format specifier '%d'"); } }
  public static void c15() { try { String.format("%<s", 1); assert false; } catch(java.util.MissingFormatArgumentException e) { assert e.getMessage().equals("Format specifier '%<s'"); } }
  public static void c15Bad() { try { String.format("%<s", 1); } catch(java.util.MissingFormatArgumentException e) { assert !e.getMessage().equals("Format specifier '%<s'"); } }
  public static void c16() { try { String.format("%d", "x"); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("d != java.lang.String"); } }
  public static void c16Bad() { try { String.format("%d", "x"); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("d != java.lang.String"); } }
  public static void c17() { try { String.format("%X", "x"); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("x != java.lang.String"); } }
  public static void c17Bad() { try { String.format("%X", "x"); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("x != java.lang.String"); } }
  public static void c18() { try { String.format("%tY", "x"); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("Y != java.lang.String"); } }
  public static void c18Bad() { try { String.format("%tY", "x"); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("Y != java.lang.String"); } }
  public static void c19() { try { String.format("%tY", 1); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("Y != java.lang.Integer"); } }
  public static void c19Bad() { try { String.format("%tY", 1); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("Y != java.lang.Integer"); } }
  public static void c20() { try { String.format("%c", 0x110000); assert false; } catch(java.util.IllegalFormatCodePointException e) { assert e.getMessage().equals("Code point = 0x110000"); } }
  public static void c20Bad() { try { String.format("%c", 0x110000); } catch(java.util.IllegalFormatCodePointException e) { assert !e.getMessage().equals("Code point = 0x110000"); } }
  public static void c21() { try { String.format("%c", (byte) -1); assert false; } catch(java.util.IllegalFormatCodePointException e) { assert e.getMessage().equals("Code point = 0xffffffff"); } }
  public static void c21Bad() { try { String.format("%c", (byte) -1); } catch(java.util.IllegalFormatCodePointException e) { assert !e.getMessage().equals("Code point = 0xffffffff"); } }
  public static void c22() { try { String.format("%#s", 1); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = s, Flags = #"); } }
  public static void c22Bad() { try { String.format("%#s", 1); } catch(java.util.FormatFlagsConversionMismatchException e) { assert !e.getMessage().equals("Conversion = s, Flags = #"); } }
  public static void c23() { try { String.format("%#s", (Object) null); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = s, Flags = #"); } }
  public static void c23Bad() { try { String.format("%#s", (Object) null); } catch(java.util.FormatFlagsConversionMismatchException e) { assert !e.getMessage().equals("Conversion = s, Flags = #"); } }
  public static void c24() { try { String.format("%(x", 5); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = x, Flags = ("); } }
  public static void c24Bad() { try { String.format("%(x", 5); } catch(java.util.FormatFlagsConversionMismatchException e) { assert !e.getMessage().equals("Conversion = x, Flags = ("); } }
  public static void c25() { try { String.format("%+o", 5L); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = o, Flags = +"); } }
  public static void c25Bad() { try { String.format("%+o", 5L); } catch(java.util.FormatFlagsConversionMismatchException e) { assert !e.getMessage().equals("Conversion = o, Flags = +"); } }
  public static void c26() { try { String.format("%e", new Object()); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("e != java.lang.Object"); } }
  public static void c26Bad() { try { String.format("%e", new Object()); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("e != java.lang.Object"); } }
  public static void c27() { try { String.format("%d", 1.5); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("d != java.lang.Double"); } }
  public static void c27Bad() { try { String.format("%d", 1.5); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("d != java.lang.Double"); } }
  public static void c28() { try { String.format("%f", 'c'); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("f != java.lang.Character"); } }
  public static void c28Bad() { try { String.format("%f", 'c'); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("f != java.lang.Character"); } }
  public static void c29() { try { String.format("%s %d", "a", "b"); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("d != java.lang.String"); } }
  public static void c29Bad() { try { String.format("%s %d", "a", "b"); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("d != java.lang.String"); } }
  public static void c30() { try { String.format("%c %c", 'a', 1.0f); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("c != java.lang.Float"); } }
  public static void c30Bad() { try { String.format("%c %c", 'a', 1.0f); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("c != java.lang.Float"); } }
  public static void c31() { try { String.format("%2$s%<d", 1, "b"); assert false; } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("d != java.lang.String"); } }
  public static void c31Bad() { try { String.format("%2$s%<d", 1, "b"); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("d != java.lang.String"); } }
  public static void c32() { try { String.format("%3$s", 1, 2); assert false; } catch(java.util.MissingFormatArgumentException e) { assert e.getMessage().equals("Format specifier '%3$s'"); } }
  public static void c32Bad() { try { String.format("%3$s", 1, 2); } catch(java.util.MissingFormatArgumentException e) { assert !e.getMessage().equals("Format specifier '%3$s'"); } }
  public static void c33() { String r = String.format("%1$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); assert r.equals("1"); }
  public static void c33Bad() { String r = String.format("%1$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); assert !r.equals("1"); }
  public static void c34() { try { String.format("%12$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); assert false; } catch(java.util.MissingFormatArgumentException e) { assert e.getMessage().equals("Format specifier '%12$d'"); } }
  public static void c34Bad() { try { String.format("%12$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); } catch(java.util.MissingFormatArgumentException e) { assert !e.getMessage().equals("Format specifier '%12$d'"); } }
  public static void c35() { try { String.format("%11$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); } catch(Exception e) { assert false; } }
  public static void c35Bad() { String.format("%11$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); assert false; }
  public static void c36() { try { String.format("%11$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, "x"); } catch(java.util.IllegalFormatConversionException e) { assert e.getMessage().equals("d != java.lang.String"); } }
  public static void c36Bad() { try { String.format("%11$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, "x"); } catch(java.util.IllegalFormatConversionException e) { assert !e.getMessage().equals("d != java.lang.String"); } }
  public static void c37() { try { String.format("%11$c", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -5); } catch(java.util.IllegalFormatCodePointException e) { assert e.getMessage().equals("Code point = 0xfffffffb"); } }
  public static void c37Bad() { try { String.format("%11$c", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -5); } catch(java.util.IllegalFormatCodePointException e) { assert !e.getMessage().equals("Code point = 0xfffffffb"); } }
  public static void c38() { try { String.format("%11$s %1$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); } catch(Exception e) { assert false; } }
  public static void c38Bad() { String.format("%11$s %1$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); assert false; }
  public static void c39() { String r = String.format("%10$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); assert r.equals("10"); }
  public static void c39Bad() { String r = String.format("%10$d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11); assert !r.equals("10"); }
  public static void c1Java11() { String r = String.format("%0$d", 1); assert r.equals("1"); }
  public static void c1Java11Bad() { String r = String.format("%0$d", 1); assert !r.equals("1"); }
  public static void c2Java11() { try { String.format("%99999999999$d", 1); assert false; } catch(java.util.MissingFormatArgumentException e) { assert e.getMessage().equals("Format specifier '%d'"); } }
  public static void c2Java11Bad() { try { String.format("%99999999999$d", 1); } catch(java.util.MissingFormatArgumentException e) { assert !e.getMessage().equals("Format specifier '%d'"); } }
  public static void c4Java11() { String r = String.format("%99999999999d", 1); assert r.equals("1"); }
  public static void c4Java11Bad() { String r = String.format("%99999999999d", 1); assert !r.equals("1"); }
  public static void c5Java11() { String r = String.format("%.99999999999f", 1.0); assert r.equals("1.000000"); }
  public static void c5Java11Bad() { String r = String.format("%.99999999999f", 1.0); assert !r.equals("1.000000"); }
  public static void flagsJava17() { try { String.format("%(+x", 5); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = x, Flags = ("); } }
  public static void flagsJava21() { try { String.format("%(+x", 5); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = x, Flags = +("); } }
  public static void flagsAny() { try { String.format("%(+x", 5); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = x, Flags = +(") || e.getMessage().equals("Conversion = x, Flags = ("); } }
  public static void flagsAnyBad() { try { String.format("%(+x", 5); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = x, Flags = +("); } }
}
