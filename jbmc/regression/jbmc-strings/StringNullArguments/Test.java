public class Test {
  // the handler of the NullPointerException of concat(null) is reachable
  public static void concatThrows(String t) {
    try {
      "ab".concat(t);
    } catch (NullPointerException e) {
      assert t != null;
    }
  }
  public static void concatValue(String t) {
    try {
      String r = "ab".concat(t);
      assert r.length() == 2 + t.length();
    } catch (NullPointerException e) {
      assert t == null;
    }
  }
  public static void containsThrows(String t) {
    try {
      "ab".contains(t);
    } catch (NullPointerException e) {
      assert t != null;
    }
  }
  public static void startsWithThrows(String t) {
    try {
      "ab".startsWith(t);
    } catch (NullPointerException e) {
      assert t != null;
    }
  }
  public static void constructorThrows(String t) {
    try {
      new StringBuilder(t);
    } catch (NullPointerException e) {
      assert t != null;
    }
  }
  // equalsIgnoreCase(null) is false, without an exception
  public static void equalsIgnoreCaseNull() {
    assert !"ab".equalsIgnoreCase(null);
  }
  public static void equalsIgnoreCaseNullBad() {
    assert "ab".equalsIgnoreCase(null);
  }
  public static void equalsIgnoreCaseAny(String t) {
    boolean eq = "ab".equalsIgnoreCase(t);
    if (t == null) {
      assert !eq;
    } else if (eq) {
      assert t.length() == 2;
    }
  }
  // (already so before) equals(null) is false
  public static void equalsNull() {
    assert !"ab".equals(null);
  }
  // without --throw-runtime-exceptions: a failed null-pointer check
  public static void concatCheck(String t) {
    String r = "ab".concat(t);
  }
  // null stands for "null" in append and insert
  public static void appendNull(String t, CharSequence c, StringBuffer b) {
    StringBuilder sb = new StringBuilder("<");
    sb.append((String) null).append((CharSequence) null)
        .append((StringBuffer) null).append((CharSequence) null, 1, 3);
    sb.insert(1, (String) null);
    assert sb.toString().equals("<nullnullnullnullul");
  }
  public static void appendNullBad() {
    StringBuilder sb = new StringBuilder("<");
    sb.append((String) null);
    assert !sb.toString().equals("<null");
  }
  public static void bufferAppendNull() {
    StringBuffer sb = new StringBuffer("<");
    sb.append((String) null).append((StringBuffer) null)
        .append((CharSequence) null, 0, 2);
    assert sb.toString().equals("<nullnullnu");
  }
  public static void appendAny(String t) {
    StringBuilder sb = new StringBuilder();
    sb.append(t);
    assert t != null || sb.toString().equals("null");
  }
}
