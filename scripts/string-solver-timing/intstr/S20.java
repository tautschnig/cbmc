public class S20 {
  public static void f(int[] a, int k) {
    if(a == null || a.length < 20 || k < 0 || k >= 20) return;
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
    s = s + Integer.toString(a[10]) + ",";
    s = s + Integer.toString(a[11]) + ",";
    s = s + Integer.toString(a[12]) + ",";
    s = s + Integer.toString(a[13]) + ",";
    s = s + Integer.toString(a[14]) + ",";
    s = s + Integer.toString(a[15]) + ",";
    s = s + Integer.toString(a[16]) + ",";
    s = s + Integer.toString(a[17]) + ",";
    s = s + Integer.toString(a[18]) + ",";
    s = s + Integer.toString(a[19]) + ",";
    assert s.charAt(k) != 'x';
  }
}
