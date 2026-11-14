public class Test {
  static int calls = 0;
  static class A { public String toString() { calls++; return "a" + calls; } public int hashCode() { calls += 10; return 255; } }
  static class N { public String toString() { calls++; return null; } }
  static class X { public String toString() { calls++; throw new IllegalStateException("x"); } }
  public static void twice() { calls = 0; A o = new A(); String r = String.format("%s %s", o, o); assert r.equals("a1 a2") && calls == 2; }
  public static void twiceBad() { calls = 0; A o = new A(); String r = String.format("%s %s", o, o); assert !(r.equals("a1 a2") && calls == 2); }
  public static void hash() { calls = 0; A o = new A(); String r = String.format("%h %s", o, o); assert r.equals("ff a11") && calls == 11; }
  public static void beforeError() { calls = 0; A o = new A(); try { String.format("%s %d", o, "x"); assert false; } catch (java.util.IllegalFormatConversionException e) { assert calls == 1; } }
  public static void beforeErrorBad() { calls = 0; A o = new A(); try { String.format("%s %d", o, "x"); assert false; } catch (java.util.IllegalFormatConversionException e) { assert calls != 1; } }
  public static void afterError() { calls = 0; A o = new A(); try { String.format("%d %s", "x", o); assert false; } catch (java.util.IllegalFormatConversionException e) { assert calls == 0; } }
  public static void parseError() { calls = 0; A o = new A(); try { String.format("%s %q", o); assert false; } catch (java.util.UnknownFormatConversionException e) { assert calls == 0; } }
  public static void nullText() { calls = 0; String r = String.format("%s", new N()); assert r.equals("null"); }
  public static void nullWidth() { calls = 0; try { String.format("%5s", new N()); assert false; } catch (NullPointerException e) { assert calls == 1; } }
  public static void throwing() { calls = 0; A o = new A(); try { String.format("%s %s", o, new X()); assert false; } catch (IllegalStateException e) { assert calls == 2; } }
  public static void noCall() { calls = 0; A o = new A(); String r = String.format("%b %c", o, 'c'); assert r.equals("true c") && calls == 0; }
  public static void hashBad() { calls = 0; A o = new A(); String r = String.format("%h %s", o, o); assert !(r.equals("ff a11") && calls == 11); }
  public static void length() { calls = 0; String r = String.format("%s", new A()); assert r.length() == 2; }
  public static void lengthBad() { calls = 0; String r = String.format("%s", new A()); assert r.length() != 2; }
  public static void throwingBad() { calls = 0; A o = new A(); try { String.format("%s %s", o, new X()); assert false; } catch (IllegalStateException e) { assert calls != 2; } }
}
