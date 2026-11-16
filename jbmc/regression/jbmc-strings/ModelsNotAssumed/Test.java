import java.io.UnsupportedEncodingException;

public class Test {
  // the bytes of strings beyond ASCII (assumed away before)
  public static void encode() throws UnsupportedEncodingException {
    byte[] u = "\u00e9".getBytes("UTF-8");
    assert u.length == 2 && u[0] == (byte) 0xc3 && u[1] == (byte) 0xa9;
    byte[] l = "\u00e9".getBytes("ISO-8859-1");
    assert l.length == 1 && l[0] == (byte) 0xe9;
    byte[] a = "\u00e9".getBytes("US-ASCII");
    assert a.length == 1 && a[0] == '?';
    byte[] w = "\ud83d".getBytes("UTF-16BE");
    assert w.length == 2 && w[0] == (byte) 0xff && w[1] == (byte) 0xfd;
  }
  public static void decode() throws UnsupportedEncodingException {
    String u = new String(new byte[] {(byte) 0xc3, (byte) 0xa9}, "UTF-8");
    assert u.equals("\u00e9");
    String l = new String(new byte[] {(byte) 0xe9}, "ISO-8859-1");
    assert l.equals("\u00e9");
  }
  public static void reachable(char c) throws UnsupportedEncodingException {
    byte[] b = String.valueOf(c).getBytes("UTF-8");
    assert c < 128;
  }
  public static void codePoints() {
    String s = new String(new int[] {0x1f600}, 0, 1);
    assert s.length() == 2 && s.charAt(0) == '\ud83d';
  }
  public static void joinNull() {
    try {
      String.join(null, "a", "b");
      assert false;
    } catch (NullPointerException e) {
    }
  }
  // regexes that the models do not model fail a property (they were
  // assumed away)
  public static void splitRegex(String s) {
    if (s == null)
      return;
    String[] parts = s.split("a.b");
  }
  public static void splitPlain() {
    String[] parts = "a-b".split("-");
    assert parts.length == 2 && parts[1].equals("b");
  }
  public static void unknownCharset() throws UnsupportedEncodingException {
    byte[] b = "a".getBytes("windows-1252");
  }
}
