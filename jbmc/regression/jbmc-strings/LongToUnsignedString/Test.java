public class Test {
  // the digits of negative and large values (assumed away before)
  public static void hex() {
    assert Long.toHexString(-1L).equals("ffffffffffffffff");
    assert Long.toHexString(7341930217534983664L).equals("65e3c6a235e10df0");
  }
  public static void octal() {
    assert Long.toOctalString(Long.MIN_VALUE).equals("1000000000000000000000");
  }
  public static void binary() {
    assert Long.toBinaryString(-5L).equals(
        "1111111111111111111111111111111111111111111111111111111111111011");
  }
  public static void unsigned32() {
    assert Long.toUnsignedString(-1L, 32).equals("fvvvvvvvvvvvv");
  }
  public static void reachable(long x) {
    String s = Long.toHexString(x);
    assert x >= 0 && x <= Integer.MAX_VALUE;
  }
  // (already so before) small values
  public static void small() {
    assert Long.toHexString(255L).equals("ff");
  }
}
