public class S5 {
  public static void f(int[] a, int k) {
    if(a == null || a.length < 5 || k < 0 || k >= 5) return;
    String s = "";
    s = s + Integer.toString(a[0]) + ",";
    s = s + Integer.toString(a[1]) + ",";
    s = s + Integer.toString(a[2]) + ",";
    s = s + Integer.toString(a[3]) + ",";
    s = s + Integer.toString(a[4]) + ",";
    assert s.charAt(k) != 'x';
  }
}
