public class Test {
  static String repeatMessage(String s, int n) {
    try {
      return s.repeat(n);
    } catch (IllegalArgumentException e) {
      return e.getMessage();
    }
  }
  public static void repeat() {
    assert "ab".repeat(3).equals("ababab");
    assert "ab".repeat(0).equals("");
    assert "".repeat(5).equals("");
    assert repeatMessage("ab", -2).equals("count is negative: -2");
  }
  public static void repeatSymbolic(int n) {
    if (n < 0 || n > 5)
      return;
    assert "xy".repeat(n).length() == 2 * n;
  }
  public static void blank() {
    assert "".isBlank();
    assert " \t\u2003\u3000".isBlank();
    assert !"\u00a0".isBlank();
    assert !" a ".isBlank();
  }
  public static void strip() {
    assert "\u2003a b\u3000".strip().equals("a b");
    assert "\u1680\u205fa ".stripLeading().equals("a ");
    assert "\u00a0a\u2007".strip().equals("\u00a0a\u2007");
  }
  public static void formatted() {
    assert "%s-%s".formatted("a", "b").equals("a-b");
  }
  public static void codePointString() {
    assert Character.toString(0x41).equals("A");
    assert Character.toString(0x1F600).equals("\ud83d\ude00");
    try {
      Character.toString(-1);
      assert false;
    } catch (IllegalArgumentException e) {
      assert e.getMessage().equals("Not a valid Unicode code point: 0xFFFFFFFF");
    }
  }
  public static void codePointOf() {
    Character.codePointOf("LATIN SMALL LETTER A");
  }
  static String decode(String s) {
    try {
      return "" + Integer.decode(s);
    } catch (NumberFormatException e) {
      return e.getMessage();
    }
  }
  public static void decodeEmpty() {
    assert decode("").equals("Zero length string");
    assert decode("0x1f").equals("31");
  }
  static String parse(CharSequence s, int b, int e, int radix) {
    try {
      return "" + Integer.parseInt(s, b, e, radix);
    } catch (NumberFormatException x) {
      return "NFE " + x.getMessage();
    } catch (IndexOutOfBoundsException x) {
      return "IOOBE " + x.getMessage();
    }
  }
  public static void parseRange17() {
    assert parse("x123y", 1, 4, 10).equals("123");
    assert parse("x-12y", 1, 4, 16).equals("-18");
    assert parse("12a", 0, 3, 10).equals("NFE Error at index 2 in: \"12a\"");
    assert parse("12", 1, 5, 10).equals("IOOBE null");
    assert parse("12", 1, 1, 16).equals("NFE For input string: \"\" under radix 16");
  }
  public static void parseRange21() {
    assert parse("12", 1, 5, 10).equals("IOOBE Range [1, 5) out of bounds for length 2");
    assert parse("+", 0, 1, 10).equals("NFE Error at index 1 in: \"+\"");
  }
  public static void parseRange25() {
    assert parse("12a", 0, 3, 10).equals("NFE Error at index 2 in: \"12a\"");
    assert parse("+", 0, 1, 10).equals("NFE Error at index 1 in: \"+\"");
    assert parse("1-", 0, 2, 10).equals("NFE Error at index 1 in: \"1-\"");
    assert parse(new StringBuilder("x77"), 1, 3, 8).equals("63");
  }
  public static void main(String[] a) {
    repeat(); repeatSymbolic(3); blank(); strip(); formatted(); codePointString(); decodeEmpty();
    int v = Runtime.version().feature();
    if (v <= 17) parseRange17();
    if (v >= 18 && v <= 21) parseRange21();
    if (v >= 22) parseRange25();
    System.out.println("ok " + v);
  }
}
