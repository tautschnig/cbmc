import org.cprover.CProver;

public class Test {
  public static void pos() { String r = String.format("%012.2a", 1.0); assert r.equals("0x000001.00p0"); }
  public static void posBad() { String r = String.format("%012.2a", 1.0); assert !r.equals("0x000001.00p0"); }
  public static void wide() { String r = String.format("%020.15a", 1.5); assert r.equals("0x00000000000001.800000000000000p0"); }
  public static void wideBad() { String r = String.format("%020.15a", 1.5); assert !r.equals("0x00000000000001.800000000000000p0"); }
  public static void pi() { String r = String.format("%012.12a", Math.PI); assert r.equals("0x1.921fb54442d2p1"); }
  public static void neg() { String r = String.format("%012.2a", -1.0); assert r.equals("-0x000001.00p0") || r.equals("-0x00001.00p0"); }
  public static void negBad() { String r = String.format("%012.2a", -1.0); assert r.equals("-0x00001.00p0"); }
  public static void neg8() { String r = String.format("%012.2a", -1.0); assert r.equals("-0x000001.00p0"); }
  public static void neg17() { String r = String.format("%012.2a", -1.0); assert r.equals("-0x00001.00p0"); }
  public static void sub8() { String r = String.format("%012.2a", -Double.MIN_VALUE); assert r.equals("-0x01.00p-1074"); }
  public static void sub17() { String r = String.format("%012.2a", -Double.MIN_VALUE); assert r.equals("-0x1.00p-1074"); }
  public static void upper17() { String r = String.format("%030.2A", -123456.789); assert r.equals("-0X000000000000000000001.E2P16"); }
  public static void symbolic() {
    double d = CProver.nondetDouble();
    CProver.assume(!Double.isNaN(d) && !Double.isInfinite(d));
    String r = String.format("%015.3a", d);
    assert r.length() >= 15;
  }
  public static void symbolicBad() {
    double d = CProver.nondetDouble();
    CProver.assume(!Double.isNaN(d) && !Double.isInfinite(d));
    String r = String.format("%015.3a", d);
    assert r.length() > 15;
  }
}
