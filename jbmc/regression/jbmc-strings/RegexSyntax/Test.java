import java.util.regex.Pattern;
import java.util.regex.PatternSyntaxException;

public class Test {
  static String matches(String re, String s) {
    try {
      return "" + s.matches(re);
    } catch (PatternSyntaxException e) {
      return "PSE";
    }
  }
  // Pattern rejects these (JBMC proved they did not throw)
  public static void dangling() { assert !matches("|+?", "ab").equals("PSE"); }
  public static void unclosed() { assert !matches("(ab", "ab").equals("PSE"); }
  public static void twoQuantifiers() { assert !matches("a**", "ab").equals("PSE"); }
  public static void compileBad() {
    try {
      Pattern.compile("[z-a]");
    } catch (PatternSyntaxException e) {
      assert false;
    }
  }
  // the JDK's message is possible
  public static void message() {
    try {
      Pattern.compile("(ab");
    } catch (PatternSyntaxException e) {
      assert !e.getMessage().equals("Unclosed group near index 3\n(ab");
    }
  }
  // a regex that the translation supports is valid
  public static void valid() {
    assert matches("a+b", "ab").equals("true");
    assert matches("[a-z]+[0-9]*", "ab").equals("true");
    Pattern.compile("(ab|c)*");
  }
  public static void nullRegex() {
    try {
      "x".matches(null);
      assert false;
    } catch (NullPointerException e) {
    }
  }
  public static void main(String[] a) {
    for (String re : new String[] {"|+?", "(ab", "a**", "[z-a]", "a+b"})
      System.out.println(re + " " + matches(re, "ab"));
    try { Pattern.compile("(ab"); } catch (PatternSyntaxException e) { System.out.println(e.getMessage()); }
  }
}
