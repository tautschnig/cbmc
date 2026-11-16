import java.math.BigDecimal;
import java.math.BigInteger;
import org.cprover.CProver;

public class Test {
  public static void d() { assert String.format("%d", new BigInteger("-123456789012345678901234567890")).equals("-123456789012345678901234567890"); }
  public static void dBad() { assert !String.format("%d", new BigInteger("-123456789012345678901234567890")).equals("-123456789012345678901234567890"); }
  public static void dGroup() { assert String.format("%,d", new BigInteger("1234567890123")).equals("1,234,567,890,123"); }
  public static void dWidth() { assert String.format("%+012d", BigInteger.valueOf(-42)).equals("-00000000042"); }
  public static void ox() { assert String.format("%#x|%(o|%+X", BigInteger.valueOf(-255), BigInteger.valueOf(-8), BigInteger.valueOf(255)).equals("-0xff|(10)|+FF"); }
  public static void oxZeros() { assert String.format("%(#010x", BigInteger.valueOf(-255)).equals("(0x0000ff)"); }
  public static void s() { assert String.format("%s", BigInteger.TEN.pow(30)).equals("1000000000000000000000000000000"); }
  public static void f() { assert String.format("%.2f", new BigDecimal("1234.5678")).equals("1234.57"); }
  public static void fBad() { assert !String.format("%.2f", new BigDecimal("1234.5678")).equals("1234.57"); }
  public static void fGroup() { assert String.format("%,.1f", new BigDecimal("-1234567.25")).equals("-1,234,567.3"); }
  public static void e() { assert String.format("%.3e", new BigDecimal("-0.000123456")).equals("-1.235e-04"); }
  public static void eAlternate() { assert String.format("%#.0e", new BigDecimal("5")).equals("5.e+00"); }
  public static void g() { assert String.format("%g", new BigDecimal("12345.6789")).equals("12345.7"); }
  public static void g17() { assert String.format("%.3g", new BigDecimal("99.995")).equals("100"); }
  public static void g16() { assert String.format("%.3g", new BigDecimal("99.995")).equals("100.0"); }
  public static void fGerman() { assert String.format("%.2f", new BigDecimal("1234.5678")).equals("1234,57"); }
  public static void symbolic(long a) { String r = String.format("%d", BigInteger.valueOf(a)); assert r.length() <= 20; }
  public static void symbolicBad(long a) { String r = String.format("%d", BigInteger.valueOf(a)); assert r.length() <= 19; }
}
