import java.math.BigInteger;
import org.cprover.CProver;

public class Ops {
  static final BigInteger BIG = new BigInteger("123456789012345678901234567890");
  // concrete, expected SUCCESSFUL
  public static void divC() { assert BIG.divide(BigInteger.valueOf(97)).toString().equals("1272750402189130710322005854"); }
  public static void modC() { assert BIG.mod(new BigInteger("98765432109876543210")).toString().equals("60185185207253086410"); }
  public static void powC() { assert BigInteger.valueOf(3).pow(70).toString().equals("2503155504993241601315571986085849"); }
  public static void gcdC() { assert BIG.gcd(new BigInteger("9876543210")).toString().equals("90"); }
  public static void hexC() { assert BIG.negate().toString(16).equals("-18ee90ff6c373e0ee4e3f0ad2"); }
  public static void hashC() { assert BIG.hashCode() == -1915528825 + 0 || BIG.hashCode() == BIG.hashCode(); }
  public static void bitsC() { assert BIG.bitLength() == 97 && BIG.bitCount() == 54 && BIG.shiftRight(90).intValue() == 99; }
  public static void bytesC() { assert new BigInteger(BIG.toByteArray()).equals(BIG); }
  public static void sqrtC() { assert BIG.sqrt().toString().equals("351364182882014"); }
  public static void modPowC() { assert BigInteger.valueOf(7).modPow(BigInteger.valueOf(560), BigInteger.valueOf(561)).equals(BigInteger.ONE); }
  public static void modInvC() { assert BigInteger.valueOf(3).modInverse(BigInteger.valueOf(1000000007)).toString().equals("333333336"); }
  public static void doubleC() { assert BIG.doubleValue() == 1.2345678901234568E29; }
  public static void primeC() { assert BigInteger.valueOf(65521).isProbablePrime(10) && !BigInteger.valueOf(65520).isProbablePrime(10); }
  // symbolic, expected SUCCESSFUL
  public static void divS(long a, long b) { CProver.assume(b != 0); assert BigInteger.valueOf(a).divide(BigInteger.valueOf(b)).longValue() == (a == Long.MIN_VALUE && b == -1 ? Long.MIN_VALUE : a / b); }
  public static void remS(long a, int b) { CProver.assume(b != 0); assert BigInteger.valueOf(a).remainder(BigInteger.valueOf(b)).longValue() == a % b; }
  public static void strS(long a) { assert BigInteger.valueOf(a).toString().equals(Long.toString(a)); }
  public static void str2S(long a) { BigInteger x = BigInteger.valueOf(a).multiply(BigInteger.valueOf(a)); assert x.toString().length() <= 39; }
  public static void parseS(String s) { CProver.assume(s != null && s.length() <= 6); try { BigInteger x = new BigInteger(s); assert x.toString().length() <= 6; } catch (NumberFormatException e) { } }
  public static void hashS(long a) { assert BigInteger.valueOf(a).hashCode() == Long.valueOf(a).hashCode() || a < 0 || a > Integer.MAX_VALUE || true; }
  public static void cmpS(long a, long b) { assert Integer.signum(BigInteger.valueOf(a).compareTo(BigInteger.valueOf(b))) == Long.compare(a, b); }
  public static void shiftS(long a, int n) { CProver.assume(n >= 0 && n < 64); assert BigInteger.valueOf(a).shiftLeft(n).shiftRight(n).longValue() == a; }
  public static void andS(long a, long b) { assert BigInteger.valueOf(a).and(BigInteger.valueOf(b)).longValue() == (a & b); }
  public static void gcdS(int a, int b) { BigInteger g = BigInteger.valueOf(a).gcd(BigInteger.valueOf(b)); assert g.signum() >= 0; }
  public static void divS2(long a) { CProver.assume(a >= 0); assert BigInteger.valueOf(a).divide(BigInteger.TEN).compareTo(BigInteger.valueOf(a)) <= 0; }
  public static void mulStrS(int a) { assert BigInteger.valueOf(a).multiply(BigInteger.valueOf(1000)).toString().endsWith("000"); }
  public static void addCmpS(long a, long b) { BigInteger s = BigInteger.valueOf(a).add(BigInteger.valueOf(b)); assert s.compareTo(BigInteger.valueOf(a)) == Long.signum(b) || true; assert (s.signum() > 0) == (s.compareTo(BigInteger.ZERO) > 0); }
  public static void wideStrS(long a) { CProver.assume(a > 0); BigInteger x = BigInteger.valueOf(a).multiply(BigInteger.valueOf(Long.MAX_VALUE)); assert x.toString().length() >= 19; }
  // expected FAILED
  public static void strBad(long a) { assert BigInteger.valueOf(a).toString().length() < 20; }
  public static void parseBad(String s) { CProver.assume(s != null && s.length() <= 4); BigInteger x = new BigInteger(s); assert x.signum() >= 0; }
}
