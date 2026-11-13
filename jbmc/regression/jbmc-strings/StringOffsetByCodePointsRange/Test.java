public class Test {
  // Java: IndexOutOfBoundsException when fewer code points remain.
  public static void over(String s) {
    if(s == null || !s.equals("abc")) return;
    try { s.offsetByCodePoints(1, 3); assert false; }
    catch(IndexOutOfBoundsException e) { }
  }
  public static void under(String s) {
    if(s == null || !s.equals("abc")) return;
    try { s.offsetByCodePoints(1, -2); assert false; }
    catch(IndexOutOfBoundsException e) { }
  }
  public static void inRange(String s) {
    if(s == null || !s.equals("abc")) return;
    assert s.offsetByCodePoints(1, 2) == 3;
    assert s.offsetByCodePoints(1, -1) == 0;
  }
  public static void builder(String s) {
    if(s == null || !s.equals("abc")) return;
    StringBuilder b = new StringBuilder(s);
    assert b.offsetByCodePoints(1, 2) == 3;
    try { b.offsetByCodePoints(1, 3); assert false; }
    catch(IndexOutOfBoundsException e) { }
  }
  public static void buffer(String s) {
    if(s == null || !s.equals("abc")) return;
    StringBuffer b = new StringBuffer(s);
    assert b.offsetByCodePoints(0, 3) == 3;
    try { b.offsetByCodePoints(0, 4); assert false; }
    catch(IndexOutOfBoundsException e) { }
  }
}
