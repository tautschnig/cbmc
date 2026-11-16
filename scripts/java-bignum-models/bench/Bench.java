import java.math.BigInteger;
import org.cprover.CProver;

public class Bench {
  static BigInteger fact(int n) { BigInteger r = BigInteger.ONE; for (int i = 2; i <= n; i++) r = r.multiply(BigInteger.valueOf(i)); return r; }
  static BigInteger fib(int n) { BigInteger a = BigInteger.ZERO, b = BigInteger.ONE; for (int i = 0; i < n; i++) { BigInteger c = a.add(b); a = b; b = c; } return a; }
  // expected SUCCESSFUL
  public static void addConst() { assert BigInteger.valueOf(12).add(BigInteger.ONE).equals(BigInteger.valueOf(13)); }
  public static void parseConst() { assert new BigInteger("-42").toString().equals("-42"); }
  public static void square(long x) { BigInteger b = BigInteger.valueOf(x); assert b.multiply(b).signum() >= 0; }
  public static void succ(long x) { assert BigInteger.valueOf(x).add(BigInteger.ONE).compareTo(BigInteger.valueOf(x)) > 0; }
  public static void fact20() { assert fact(20).toString().equals("2432902008176640000"); }
  public static void fact25() { assert fact(25).toString().equals("15511210043330985984000000"); }
  public static void fib100() { assert fib(100).toString().equals("354224848179261915075"); }
  public static void fmtLen(long x) { assert String.format("%d", BigInteger.valueOf(x)).length() <= 20; }
  public static void mulNoOverflow(int a, int b) { assert BigInteger.valueOf(a).multiply(BigInteger.valueOf(b)).longValue() == (long) a * b; }
  public static void bigRoundTrip() { BigInteger m = BigInteger.valueOf(Long.MAX_VALUE); assert m.add(BigInteger.ONE).subtract(BigInteger.ONE).equals(m); }
  public static void wideCompare(long x) { BigInteger w = BigInteger.valueOf(Long.MAX_VALUE).multiply(BigInteger.TEN); assert w.compareTo(BigInteger.valueOf(x)) > 0; }
  // expected FAILED
  public static void mulOverflowBad(long a, long b) { assert BigInteger.valueOf(a).multiply(BigInteger.valueOf(b)).equals(BigInteger.valueOf(a * b)); }
  public static void succBad(long x) { assert BigInteger.valueOf(x).add(BigInteger.ONE).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0; }
  public static void fact25Bad() { assert !fact(25).toString().equals("15511210043330985984000000"); }
}
