public class Test {
  public static void c0(int x) { if(x != 1) return; String r = String.format("%s", x); assert r.equals("1"); }
  public static void c0Bad(int x) { if(x != 1) return; String r = String.format("%s", x); assert !r.equals("1"); }
  public static void c1(boolean x) { if(x != true) return; String r = String.format("%s", x); assert r.equals("true"); }
  public static void c1Bad(boolean x) { if(x != true) return; String r = String.format("%s", x); assert !r.equals("true"); }
  public static void c2(char x) { if(x != 'x') return; String r = String.format("%s", x); assert r.equals("x"); }
  public static void c2Bad(char x) { if(x != 'x') return; String r = String.format("%s", x); assert !r.equals("x"); }
  public static void c3(long x) { if(x != -12L) return; String r = String.format("%s", x); assert r.equals("-12"); }
  public static void c3Bad(long x) { if(x != -12L) return; String r = String.format("%s", x); assert !r.equals("-12"); }
  public static void c4(String x) { if(x == null ? "ab" != null : !x.equals("ab")) return; String r = String.format("%s", x); assert r.equals("ab"); }
  public static void c4Bad(String x) { if(x == null ? "ab" != null : !x.equals("ab")) return; String r = String.format("%s", x); assert !r.equals("ab"); }
  public static void c5(String x) { if(x == null ? "null" != null : !x.equals("null")) return; String r = String.format("%s", x); assert r.equals("null"); }
  public static void c5Bad(String x) { if(x == null ? "null" != null : !x.equals("null")) return; String r = String.format("%s", x); assert !r.equals("null"); }
  public static void c6(Object x) { if(x != null) return; String r = String.format("[%s]", (Object) x); assert r.equals("[null]"); }
  public static void c6Bad(Object x) { if(x != null) return; String r = String.format("[%s]", (Object) x); assert !r.equals("[null]"); }
  public static void c7(int x) { if(x != 0) return; String r = String.format("%b", x); assert r.equals("true"); }
  public static void c7Bad(int x) { if(x != 0) return; String r = String.format("%b", x); assert !r.equals("true"); }
  public static void c8(String x) { if(x == null ? "null" != null : !x.equals("null")) return; String r = String.format("%b", x); assert r.equals("true"); }
  public static void c8Bad(String x) { if(x == null ? "null" != null : !x.equals("null")) return; String r = String.format("%b", x); assert !r.equals("true"); }
  public static void c9(boolean x) { if(x != false) return; String r = String.format("%b", x); assert r.equals("false"); }
  public static void c9Bad(boolean x) { if(x != false) return; String r = String.format("%b", x); assert !r.equals("false"); }
  public static void c10(int x) { if(x != 3) return; String r = String.format("%B", x); assert r.equals("TRUE"); }
  public static void c10Bad(int x) { if(x != 3) return; String r = String.format("%B", x); assert !r.equals("TRUE"); }
  public static void c11(Object x) { if(x != null) return; String r = String.format("%b", (Object) x); assert r.equals("false"); }
  public static void c11Bad(Object x) { if(x != null) return; String r = String.format("%b", (Object) x); assert !r.equals("false"); }
  public static void c12(int x) { if(x != 65) return; String r = String.format("%c", x); assert r.equals("A"); }
  public static void c12Bad(int x) { if(x != 65) return; String r = String.format("%c", x); assert !r.equals("A"); }
  public static void c13(char x) { if(x != 'q') return; String r = String.format("%c", x); assert r.equals("q"); }
  public static void c13Bad(char x) { if(x != 'q') return; String r = String.format("%c", x); assert !r.equals("q"); }
  public static void c14(int x) { if(x != 128512) return; String r = String.format("%c", x); assert r.equals("😀"); }
  public static void c14Bad(int x) { if(x != 128512) return; String r = String.format("%c", x); assert !r.equals("😀"); }
  public static void c15(int x) { if(x != -1) return; try { String.format("%c", x); assert false; } catch(java.util.IllegalFormatCodePointException e) { } }
  public static void c15Bad(int x) { if(x != -1) return; try { String.format("%c", x); } catch(java.util.IllegalFormatCodePointException e) { assert false; } }
  public static void c16(long x) { if(x != 65L) return; try { String.format("%c", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c16Bad(long x) { if(x != 65L) return; try { String.format("%c", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c17(String x) { if(x == null ? "x" != null : !x.equals("x")) return; try { String.format("%c", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c17Bad(String x) { if(x == null ? "x" != null : !x.equals("x")) return; try { String.format("%c", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c18(char x) { if(x != 'q') return; String r = String.format("%C", x); assert r.equals("Q"); }
  public static void c18Bad(char x) { if(x != 'q') return; String r = String.format("%C", x); assert !r.equals("Q"); }
  public static void c19(int x) { if(x != -12) return; String r = String.format("%d", x); assert r.equals("-12"); }
  public static void c19Bad(int x) { if(x != -12) return; String r = String.format("%d", x); assert !r.equals("-12"); }
  public static void c20(long x) { if(x != 12345678901L) return; String r = String.format("%d", x); assert r.equals("12345678901"); }
  public static void c20Bad(long x) { if(x != 12345678901L) return; String r = String.format("%d", x); assert !r.equals("12345678901"); }
  public static void c21(String x) { if(x == null ? "x" != null : !x.equals("x")) return; try { String.format("%d", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c21Bad(String x) { if(x == null ? "x" != null : !x.equals("x")) return; try { String.format("%d", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c22(boolean x) { if(x != true) return; try { String.format("%d", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c22Bad(boolean x) { if(x != true) return; try { String.format("%d", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c23(char x) { if(x != 'c') return; try { String.format("%d", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c23Bad(char x) { if(x != 'c') return; try { String.format("%d", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c24(Object x) { if(x != null) return; String r = String.format("%d", (Object) x); assert r.equals("null"); }
  public static void c24Bad(Object x) { if(x != null) return; String r = String.format("%d", (Object) x); assert !r.equals("null"); }
  public static void c25(int x) { if(x != 255) return; String r = String.format("%x", x); assert r.equals("ff"); }
  public static void c25Bad(int x) { if(x != 255) return; String r = String.format("%x", x); assert !r.equals("ff"); }
  public static void c26(String x) { if(x == null ? "x" != null : !x.equals("x")) return; try { String.format("%x", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c26Bad(String x) { if(x == null ? "x" != null : !x.equals("x")) return; try { String.format("%x", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c27(int x) { if(x != 1) return; try { String.format("%f", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c27Bad(int x) { if(x != 1) return; try { String.format("%f", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c28(char x) { if(x != 'c') return; try { String.format("%e", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c28Bad(char x) { if(x != 'c') return; try { String.format("%e", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c29(int x) { if(x != 1) return; try { String.format("%tY", x); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c29Bad(int x) { if(x != 1) return; try { String.format("%tY", x); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c30(int x) { if(x != 1) return; try { String.format("%q", x); assert false; } catch(java.util.UnknownFormatConversionException e) { } }
  public static void c30Bad(int x) { if(x != 1) return; try { String.format("%q", x); } catch(java.util.UnknownFormatConversionException e) { assert false; } }
  public static void c31(int x) { if(x != 1) return; try { String.format("%s %d", x, "a"); assert false; } catch(java.util.IllegalFormatConversionException e) { } }
  public static void c31Bad(int x) { if(x != 1) return; try { String.format("%s %d", x, "a"); } catch(java.util.IllegalFormatConversionException e) { assert false; } }
  public static void c32(int x) { if(x != 1) return; String r = String.format("%d %s", x, "a"); assert r.equals("1 a"); }
  public static void c32Bad(int x) { if(x != 1) return; String r = String.format("%d %s", x, "a"); assert !r.equals("1 a"); }
  public static void c33(Object x) { if(x != null) return; String r = String.format("%h", (Object) x); assert r.equals("null"); }
  public static void c33Bad(Object x) { if(x != null) return; String r = String.format("%h", (Object) x); assert !r.equals("null"); }
  public static void c34(int x) { if(x != 7) return; String r = String.format("%S", x); assert r.equals("7"); }
  public static void c34Bad(int x) { if(x != 7) return; String r = String.format("%S", x); assert !r.equals("7"); }
  public static void c35(short x) { if(x != (short) 5) return; String r = String.format("%s", x); assert r.equals("5"); }
  public static void c35Bad(short x) { if(x != (short) 5) return; String r = String.format("%s", x); assert !r.equals("5"); }
  public static void c36(byte x) { if(x != (byte) 66) return; String r = String.format("%c", x); assert r.equals("B"); }
  public static void c36Bad(byte x) { if(x != (byte) 66) return; String r = String.format("%c", x); assert !r.equals("B"); }
}