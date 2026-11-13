public class Test {
  public static void toString23(double d) { if(d != 1e23) return; assert Double.toString(d).equals("1.0E23"); }
  public static void format23() { assert String.format("%f", 1e23).equals("100000000000000000000000.000000"); }
  public static void flags() { try { String.format("%(+x", 5); assert false; } catch(java.util.FormatFlagsConversionMismatchException e) { assert e.getMessage().equals("Conversion = x, Flags = +("); } }
}
