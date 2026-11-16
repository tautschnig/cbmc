public class Test {
  // U+FFFF is one UTF-16 unit (the literal was decoded as two)
  public static void length() {
    assert "\uffff".length() == 1;
    assert "\uffffP".length() == 2;
    assert "\uffff".charAt(0) == '\uffff';
  }
  public static void lengthBad() {
    assert "\uffff".length() == 2;
  }
}
