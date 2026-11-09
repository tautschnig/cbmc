public class Test {
  // Java finds an empty needle at the start of the search, clamped into
  // [0, length], and lastIndexOf finds nothing before 0.
  public static void constants() {
    assert "abc".indexOf("", 10) == 3;
    assert "abc".indexOf("", -5) == 0;
    assert "abc".indexOf("", 2) == 2;
    assert "abc".lastIndexOf("", 10) == 3;
    assert "abc".lastIndexOf("", 1) == 1;
    assert "abc".lastIndexOf("", -5) == -1;
    assert "abc".indexOf("b", -5) == 1;
    assert "abc".lastIndexOf("b", 10) == 1;
    assert "abcabc".lastIndexOf("bc", 4) == 4;
  }

  public static void symbolic(String s, String t, int k) {
    if(s == null || t == null || !s.equals("abc") || t.length() != 0)
      return;
    if(k > 3)
      assert s.indexOf(t, k) == 3 && s.lastIndexOf(t, k) == 3;
    if(k < 0)
      assert s.indexOf(t, k) == 0 && s.lastIndexOf(t, k) == -1;
    if(k >= 0 && k <= 3)
      assert s.indexOf(t, k) == k && s.lastIndexOf(t, k) == k;
  }

  public static void symbolicFail(String s, String t, int k) {
    if(s == null || t == null || !s.equals("abc") || t.length() != 0)
      return;
    if(k > 3)
      assert s.indexOf(t, k) != 3;
  }
}
