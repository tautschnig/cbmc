public class S10 {
  public static void f(int[] a, int k) {
    if(a == null || a.length < 10 || k < 0 || k >= 10) return;
    String s = "";
    s = s + Integer.toString(a[0]) + ",";
    s = s + Integer.toString(a[1]) + ",";
    s = s + Integer.toString(a[2]) + ",";
    s = s + Integer.toString(a[3]) + ",";
    s = s + Integer.toString(a[4]) + ",";
    s = s + Integer.toString(a[5]) + ",";
    s = s + Integer.toString(a[6]) + ",";
    s = s + Integer.toString(a[7]) + ",";
    s = s + Integer.toString(a[8]) + ",";
    s = s + Integer.toString(a[9]) + ",";
    assert s.charAt(k) != 'x';
  }
}
