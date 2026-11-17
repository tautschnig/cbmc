public class S40 {
  public static void f(int[] a, int k) {
    if(a == null || a.length < 40 || k < 0 || k >= 40) return;
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
    s = s + Integer.toString(a[20]) + ",";
    s = s + Integer.toString(a[21]) + ",";
    s = s + Integer.toString(a[22]) + ",";
    s = s + Integer.toString(a[23]) + ",";
    s = s + Integer.toString(a[24]) + ",";
    s = s + Integer.toString(a[25]) + ",";
    s = s + Integer.toString(a[26]) + ",";
    s = s + Integer.toString(a[27]) + ",";
    s = s + Integer.toString(a[28]) + ",";
    s = s + Integer.toString(a[29]) + ",";
    s = s + Integer.toString(a[30]) + ",";
    s = s + Integer.toString(a[31]) + ",";
    s = s + Integer.toString(a[32]) + ",";
    s = s + Integer.toString(a[33]) + ",";
    s = s + Integer.toString(a[34]) + ",";
    s = s + Integer.toString(a[35]) + ",";
    s = s + Integer.toString(a[36]) + ",";
    s = s + Integer.toString(a[37]) + ",";
    s = s + Integer.toString(a[38]) + ",";
    s = s + Integer.toString(a[39]) + ",";
    assert s.charAt(k) != 'x';
  }
}
