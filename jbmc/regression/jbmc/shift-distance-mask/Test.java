public class Test {
  // JLS 15.19: the distance of a shift is masked by 31 (int) or 63 (long)
  public static void ushr(int i) { assert (i >>> 33) == (i >>> 1); }
  public static void shr(int i) { assert (i >> -1) == (i >> 31); }
  public static void lushr(long i) { assert (i >>> 65) == (i >>> 1); }
  public static void lshr(long i) { assert (i >> -1) == (i >> 63); }
  public static void ushrBad() { assert (-8 >>> 33) == 0; }
  public static void shrBad() { assert (-8 >> 32) == -1; }
  // (already so before) shl
  public static void shl(int i) { assert (i << 33) == (i << 1); }
  public static void rotate() { assert Integer.rotateLeft(135590, 77114896) == 296091650; }
}
