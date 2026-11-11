public class Test {
  // lastIndexOf with a fromIndex below -1 finds nothing. The solver required
  // the result to be at most fromIndex, so it was infeasible and any
  // assertion after it was proved.
  public static void found(int k) {
    if(k != -5)
      return;
    int r = String.valueOf(' ').lastIndexOf("a", k);
    assert r == -1;
  }
  public static void notFound(int k) {
    if(k != -5)
      return;
    int r = String.valueOf(' ').lastIndexOf("a", k);
    assert r != -1;
  }
  public static void charNeedle(String s, int k) {
    if(k != -3 || !s.equals("aba"))
      return;
    int r = s.lastIndexOf('a', k);
    assert r != -1;
  }
}
