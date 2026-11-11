public class Test {
  // "".contains("") is true. The simplifier looked for the empty needle
  // with std::search, which returns the end of an empty haystack. (The
  // StringBuilder method makes the simplifier see the constant arrays.)
  static String unused(String s) {
    StringBuilder b = new StringBuilder(s);
    b.setLength(1);
    return b.toString();
  }
  public static void empty() {
    boolean r = "".contains("");
    assert r == true;
  }
  public static void emptyNotContained() {
    boolean r = "".contains("");
    assert !(r == true);
  }
  public static void notEmpty() {
    boolean r = "ab".contains("") && !"".contains("a");
    assert r;
  }
}
