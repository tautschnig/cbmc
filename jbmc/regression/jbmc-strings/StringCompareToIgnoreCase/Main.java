class Main {
  public static void pass(String s, String t) {
    if(s.equals("aBc") && t.equals("ABD")) {
      assert s.compareToIgnoreCase(t) < 0;
      assert t.compareToIgnoreCase(s) > 0;
      assert s.compareToIgnoreCase("AbC") == 0;
      assert s.compareToIgnoreCase("ab") > 0;
      assert "a".compareToIgnoreCase(s) < 0;
      assert s.compareToIgnoreCase("_") > 0;
    }
  }

  public static void fail(String s, String t) {
    if(s.equals("aBc") && t.equals("ABD")) {
      assert s.compareToIgnoreCase(t) >= 0;
    }
  }
}
