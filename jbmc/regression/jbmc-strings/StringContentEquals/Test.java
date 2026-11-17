public class Test {
  static class Chars implements CharSequence {
    String s;
    Chars(String s) {
      this.s = s;
    }
    public int length() {
      return s.length();
    }
    public char charAt(int i) {
      return s.charAt(i);
    }
    public CharSequence subSequence(int b, int e) {
      return s.substring(b, e);
    }
    public String toString() {
      return "not the content";
    }
  }
  public static void contentEquals() {
    assert "abc".contentEquals(new StringBuffer("abc"));
    assert !"abc".contentEquals(new StringBuilder("abd"));
    assert "abc".contentEquals("abc");
    assert !"abc".contentEquals("ab");
    assert "abc".contentEquals(new Chars("abc"));
    assert !"abc".contentEquals(new Chars("abC"));
  }
  public static void contentEqualsNull() {
    try {
      "abc".contentEquals((CharSequence) null);
      assert false;
    } catch (NullPointerException e) {
    }
    try {
      "abc".contentEquals((StringBuffer) null);
      assert false;
    } catch (NullPointerException e) {
    }
  }
  public static void regionMatches() {
    assert "Hello".regionMatches(1, "xell", 1, 3);
    assert !"Hello".regionMatches(1, "xelL", 1, 3);
    assert "HELLO".regionMatches(true, 1, "ell", 0, 3);
    assert !"HELLO".regionMatches(false, 1, "ell", 0, 3);
    assert "abc".regionMatches(1, "xyz", 2, -1);
    assert !"abc".regionMatches(2, "abc", 0, 2);
    assert !"abc".regionMatches(-1, "abc", 0, 1);
  }
  public static void regionMatchesNull() {
    try {
      "abc".regionMatches(0, null, 0, 1);
      assert false;
    } catch (NullPointerException e) {
    }
  }
  public static void valueOfChars() {
    char[] c = {'x', 'y', 'z'};
    assert String.valueOf(c).equals("xyz");
    assert String.copyValueOf(c).equals("xyz");
    assert String.valueOf(c, 1, 2).equals("yz");
    assert String.copyValueOf(c, 0, 1).equals("x");
    try {
      String.valueOf((char[]) null);
      assert false;
    } catch (NullPointerException e) {
    }
  }
  public static void main(String[] args) {
    contentEquals();
    contentEqualsNull();
    regionMatches();
    regionMatchesNull();
    valueOfChars();
    System.out.println("ok");
  }
}
