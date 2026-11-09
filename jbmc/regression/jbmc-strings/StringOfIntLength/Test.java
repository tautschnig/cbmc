public class Test {
  public static void zero(int i) { if(i < 0 || i > 0) return; String s = Integer.toString(i); assert s.length() == 1; }
  public static void zeroBad(int i) { if(i < 0 || i > 0) return; String s = Integer.toString(i); assert s.length() == 2; }
  public static void neg(int i) { if(i < -9 || i > -1) return; String s = Integer.toString(i); assert s.length() == 2; }
  public static void min(int i) { if(i > Integer.MIN_VALUE) return; String s = Integer.toString(i); assert s.length() == 11; }
  public static void maxv(int i) { if(i < 1000000000) return; String s = Integer.toString(i); assert s.length() == 10; }
  public static void lmax(long i) { if(i < 1000000000000000000L) return; String s = Long.toString(i); assert s.length() == 19; }
  public static void lmin(long i) { if(i > Long.MIN_VALUE) return; String s = Long.toString(i); assert s.length() == 20; }
  public static void bin(int i) { if(i < 0x40000000) return; String s = Integer.toString(i, 2); assert s.length() == 31; }
  public static void binNeg(int i) { if(i > Integer.MIN_VALUE) return; String s = Integer.toString(i, 2); assert s.length() == 33; }
}
