public class Test {
  // append(s, start, end) of constants appends s.substring(start, end)
  public static void constant() {
    StringBuilder sb = new StringBuilder("<");
    sb.append((CharSequence) "abcd", 1, 3);
    assert sb.toString().equals("<bc");
  }
  public static void constantBad() {
    StringBuilder sb = new StringBuilder("<");
    sb.append((CharSequence) "abcd", 1, 3);
    assert !sb.toString().equals("<bc");
  }
  public static void constantEmpty() {
    StringBuilder sb = new StringBuilder("<");
    sb.append((CharSequence) "abcd", 4, 4);
    assert sb.toString().equals("<");
  }
  // (already so before) with a symbolic range
  public static void symbolic(int start) {
    if (start < 0 || start > 3)
      return;
    StringBuilder sb = new StringBuilder("<");
    sb.append((CharSequence) "abcd", start, 3);
    assert sb.length() == 4 - start;
  }
}
