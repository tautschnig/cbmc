public class Test {
  static String parseInt(String s, int radix) {
    try {
      return "value " + Integer.parseInt(s, radix);
    } catch (NumberFormatException e) {
      return e.getMessage();
    }
  }
  static String parseLong(String s, int radix) {
    try {
      return "value " + Long.parseLong(s, radix);
    } catch (NumberFormatException e) {
      return e.getMessage();
    }
  }
  static String parseUnsignedInt(String s, int radix) {
    try {
      return "value " + Integer.parseUnsignedInt(s, radix);
    } catch (NumberFormatException e) {
      return e.getMessage();
    }
  }
  static String parseUnsignedLong(String s, int radix) {
    try {
      return "value " + Long.parseUnsignedLong(s, radix);
    } catch (NumberFormatException e) {
      return e.getMessage();
    }
  }
  public static void null16() {
    assert parseInt(null, 10).equals("null");
    assert parseLong(null, 10).equals("null");
    assert parseUnsignedInt(null, 10).equals("null");
    assert parseUnsignedLong(null, 10).equals("null");
  }
  public static void null17() {
    assert parseInt(null, 10).equals("Cannot parse null string");
    assert parseLong(null, 10).equals("Cannot parse null string");
    assert parseUnsignedInt(null, 10).equals("Cannot parse null string");
    assert parseUnsignedLong(null, 10).equals("Cannot parse null string");
  }
  public static void input11() {
    assert parseInt("1x", 10).equals("For input string: \"1x\"");
    assert parseInt("g", 16).equals("For input string: \"g\"");
    assert parseLong("g", 16).equals("For input string: \"g\"");
    assert parseUnsignedInt("-1", 16).equals(
      "Illegal leading minus sign on unsigned string -1.");
  }
  public static void input12() {
    assert parseInt("1x", 10).equals("For input string: \"1x\"");
    assert parseInt("g", 16).equals("For input string: \"g\" under radix 16");
    assert parseLong("g", 16).equals("For input string: \"g\" under radix 16");
  }
  public static void unsignedEmpty21() {
    assert parseUnsignedInt("", 1).equals(
      "For input string: \"\" under radix 1");
    assert parseUnsignedLong("", 99).equals(
      "For input string: \"\" under radix 99");
  }
  public static void unsignedEmpty22() {
    assert parseUnsignedInt("", 1).equals(
      "radix 1 less than Character.MIN_RADIX");
    assert parseUnsignedLong("", 99).equals(
      "radix 99 greater than Character.MAX_RADIX");
  }
  public static void main(String[] args) {
    System.out.println(parseInt(null, 10));
    System.out.println(parseInt("1x", 10));
    System.out.println(parseInt("g", 16));
    System.out.println(parseLong("g", 16));
    System.out.println(parseUnsignedInt("-1", 16));
    System.out.println(parseUnsignedInt("", 1));
    System.out.println(parseUnsignedLong("", 99));
  }
}
