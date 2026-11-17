public class Test {
  static String parseByte(String s) {
    try {
      return "" + Byte.parseByte(s);
    } catch (NumberFormatException e) {
      return e.getMessage();
    }
  }
  static String parseShort(String s) {
    try {
      return "" + Short.parseShort(s);
    } catch (NumberFormatException e) {
      return e.getMessage();
    }
  }
  public static void bytes() {
    assert parseByte("-128").equals("-128");
    assert parseByte("826.13").equals("For input string: \"826.13\"");
    assert parseByte("128").equals("Value out of range. Value:\"128\" Radix:10");
  }
  public static void shorts() {
    assert parseShort("-32768").equals("-32768");
    assert parseShort("lb").equals("For input string: \"lb\"");
    assert parseShort("32768").equals("Value out of range. Value:\"32768\" Radix:10");
  }
  public static void main(String[] a) {
    bytes(); shorts(); System.out.println("ok");
  }
}
