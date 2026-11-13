public class Test {
  // Java accepts any number of leading zeros: the parse succeeds, so the
  // code after it is reachable.
  public static void intReachable(String s) {
    if(s == null || !s.equals("000000000001")) return;
    try { Integer.parseInt(s); assert false; } catch(NumberFormatException e) { }
  }
  public static void longReachable(String s) {
    if(s == null || !s.equals("+00000000000000000000123")) return;
    try { Long.parseLong(s); assert false; } catch(NumberFormatException e) { }
  }
  // A value out of range still throws.
  public static void tooLong(String s) {
    if(s == null || !s.equals("100000000000")) return;
    try { Integer.parseInt(s); assert false; } catch(NumberFormatException e) { }
  }
}
