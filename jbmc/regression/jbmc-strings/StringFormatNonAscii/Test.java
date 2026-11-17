public class Test {
  public static void text() {
    assert String.format("\u00e9").equals("\u00e9");
    assert String.format("\u0c5d").equals("\u0c5d");
    assert String.format("a\ud83d\ude00b").equals("a\ud83d\ude00b");
  }
  public static void textNot() {
    assert !String.format("\u0c5d").equals("\u0c5d");
  }
  public static void specifiers() {
    assert String.format("\u00e9%d\u4e2d%s", 7, "\u00fc").equals("\u00e97\u4e2d\u00fc");
    assert String.format("%s\u00e9", "x").length() == 2;
  }
  public static void unknownConversion() {
    try {
      String.format("x%\u00e9");
      assert false;
    } catch (java.util.UnknownFormatConversionException e) {
      assert e.getMessage().equals("Conversion = '\u00e9'");
    }
  }
  public static void main(String[] a) {
    text(); specifiers(); unknownConversion(); System.out.println("ok");
  }
}
