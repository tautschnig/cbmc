import java.util.function.*;
public class P {
  static void run(String name, Supplier<Object> f) {
    String r;
    try { Object o = f.get(); r = "value " + (o instanceof char[] ? new String((char[]) o) : o instanceof byte[] ? java.util.Arrays.toString((byte[]) o) : String.valueOf(o)); }
    catch (Throwable t) { r = t.getClass().getName() + ": " + t.getMessage(); }
    System.out.println(name + " => " + r);
  }
  public static void main(String[] a) {
    for (String s : new String[] {"abc", "a\u0100c"}) {
      String k = s.equals("abc") ? "L1" : "U16";
      for (int i : new int[] {-1, 3, 4}) {
        final int j = i;
        run(k + " charAt " + i, () -> s.charAt(j));
        run(k + " codePointAt " + i, () -> s.codePointAt(j));
        run(k + " codePointBefore " + i, () -> s.codePointBefore(j));
        run(k + " substring1 " + i, () -> s.substring(j));
        run(k + " offsetByCodePoints " + i + ",0", () -> s.offsetByCodePoints(j, 0));
      }
      run(k + " codePointBefore 0", () -> s.codePointBefore(0));
      run(k + " offsetByCodePoints 1,5", () -> s.offsetByCodePoints(1, 5));
      run(k + " offsetByCodePoints 1,-2", () -> s.offsetByCodePoints(1, -2));
      int[][] ranges = {{-1, 2}, {2, 1}, {1, 4}, {4, 5}, {0, 3}};
      for (int[] r : ranges) {
        String n = r[0] + "," + r[1];
        run(k + " substring " + n, () -> s.substring(r[0], r[1]));
        run(k + " subSequence " + n, () -> s.subSequence(r[0], r[1]));
        run(k + " codePointCount " + n, () -> s.codePointCount(r[0], r[1]));
        run(k + " getChars " + n + " ->0", () -> { char[] d = new char[3]; s.getChars(r[0], r[1], d, 0); return d; });
        run(k + " getBytes " + n + " ->0", () -> { byte[] d = new byte[3]; s.getBytes(r[0], r[1], d, 0); return d; });
      }
      run(k + " getChars 0,2 ->2", () -> { char[] d = new char[3]; s.getChars(0, 2, d, 2); return d; });
      run(k + " getChars 0,2 ->-1", () -> { char[] d = new char[3]; s.getChars(0, 2, d, -1); return d; });
      run(k + " getChars 0,2 null", () -> { s.getChars(0, 2, null, 0); return null; });
      run(k + " getChars 2,2 null", () -> { s.getChars(2, 2, null, 0); return null; });
      run(k + " getBytes 0,2 ->2", () -> { byte[] d = new byte[3]; s.getBytes(0, 2, d, 2); return d; });
      run(k + " getBytes 0,2 null", () -> { s.getBytes(0, 2, null, 0); return null; });
      run(k + " contentEquals null", () -> s.contentEquals((CharSequence) null));
      run(k + " contentEqualsSB null", () -> s.contentEquals((StringBuffer) null));
      run(k + " contentEquals sb", () -> s.contentEquals(new StringBuilder(s)));
      run(k + " regionMatches null", () -> s.regionMatches(0, null, 0, 1));
      run(k + " regionMatches null -1", () -> s.regionMatches(-1, null, 0, 1));
      run(k + " regionMatchesIC null", () -> s.regionMatches(true, 0, null, 0, 1));
      run(k + " regionMatchesIC null -1", () -> s.regionMatches(true, -1, null, 0, 1));
    }
    char[] c = {'a', 'b', 'c'};
    int[][] oc = {{-1, 1}, {1, -1}, {2, 2}, {4, 0}, {3, 0}, {1, 2}};
    for (int[] r : oc) {
      String n = r[0] + "," + r[1];
      run("valueOf(char[]," + n + ")", () -> String.valueOf(c, r[0], r[1]));
      run("copyValueOf(char[]," + n + ")", () -> String.copyValueOf(c, r[0], r[1]));
      run("new String(char[]," + n + ")", () -> new String(c, r[0], r[1]));
    }
    run("valueOf(null char[])", () -> String.valueOf((char[]) null));
    run("copyValueOf(null char[])", () -> String.copyValueOf((char[]) null));
    run("valueOf(null char[],0,0)", () -> String.valueOf((char[]) null, 0, 0));
    run("copyValueOf(null char[],0,0)", () -> String.copyValueOf((char[]) null, 0, 0));
  }
}
