import java.math.BigInteger;
import org.cprover.CProver;

public class Test {
  static BigInteger fact(int n) { BigInteger r = BigInteger.ONE; for (int i = 2; i <= n; i++) r = r.multiply(BigInteger.valueOf(i)); return r; }
  static BigInteger fib(int n) { BigInteger a = BigInteger.ZERO, b = BigInteger.ONE; for (int i = 0; i < n; i++) { BigInteger c = a.add(b); a = b; b = c; } return a; }
  static final BigInteger BIG = new BigInteger("123456789012345678901234567890");

  public static void fact25() { assert fact(25).toString().equals("15511210043330985984000000"); }
  public static void fact25Bad() { assert !fact(25).toString().equals("15511210043330985984000000"); }
  public static void fib100() { assert fib(100).toString().equals("354224848179261915075"); }
  public static void arithmetic() {
    assert BIG.divide(BigInteger.valueOf(97)).toString().equals("1272750402189130710322005854");
    assert BIG.mod(new BigInteger("98765432109876543210")).toString().equals("60185185207253086410");
    assert BIG.negate().toString(16).equals("-18ee90ff6c373e0ee4e3f0ad2");
    assert BIG.gcd(new BigInteger("9876543210")).intValue() == 90;
    assert BigInteger.valueOf(3).pow(70).toString().equals("2503155504993241601315571986085849");
    assert BigInteger.valueOf(-7).divide(BigInteger.valueOf(2)).intValue() == -3;
    assert BigInteger.valueOf(-7).mod(BigInteger.valueOf(2)).intValue() == 1;
  }
  public static void bits() {
    assert BIG.bitLength() == 97 && BIG.bitCount() == 54 && BIG.shiftRight(90).intValue() == 99;
    assert BigInteger.valueOf(-5).shiftRight(1).intValue() == -3;
    assert BigInteger.valueOf(-8).bitLength() == 3 && BigInteger.valueOf(-8).getLowestSetBit() == 3;
    assert BigInteger.valueOf(-6).and(BigInteger.valueOf(12)).intValue() == 8;
    assert new BigInteger(new byte[] {(byte) 0xff, 0x7f}).intValue() == -129;
  }
  public static void conversions() {
    assert BIG.hashCode() == 1915528825;
    assert BIG.doubleValue() == 1.2345678901234568E29;
    assert new BigInteger("9007199254740993").doubleValue() == 9.007199254740992E15;
    assert BIG.sqrt().longValue() == 351364182882014L;
    assert BigInteger.valueOf(3).modInverse(BigInteger.valueOf(1000000007)).intValue() == 333333336;
  }
  public static void parse() {
    assert new BigInteger("\u0661\u0662").intValue() == 12;
    assert new BigInteger("-00012").intValue() == -12;
    assert new BigInteger("Ff", 16).intValue() == 255;
  }
  public static void parseError() {
    try { new BigInteger("12x"); assert false; } catch (NumberFormatException e) { assert e.getMessage().equals("For input string: \"12x\""); }
    try { new BigInteger("+-1"); assert false; } catch (NumberFormatException e) { assert e.getMessage().equals("Illegal embedded sign character"); }
    try { new BigInteger("-"); assert false; } catch (NumberFormatException e) { assert e.getMessage().equals("Zero length BigInteger"); }
  }
  public static void radixMessage() {
    try { new BigInteger("g", 16); assert false; } catch (NumberFormatException e) { assert e.getMessage().equals("For input string: \"g\" under radix 16"); }
  }
  public static void sinhala() { assert new BigInteger("\u0de7\u0de6").intValue() == 10; }
  public static void sinhalaThrows() { try { new BigInteger("\u0de7\u0de6"); assert false; } catch (NumberFormatException e) { } }
  public static void divideByZero() {
    try { BigInteger.ONE.divide(BigInteger.ZERO); assert false; } catch (ArithmeticException e) { assert e.getMessage().equals("BigInteger divide by zero"); }
  }
  public static void wide() {
    BigInteger w = BigInteger.valueOf(Long.MAX_VALUE).pow(3);
    assert w.signum() == 1 && w.compareTo(BigInteger.valueOf(Long.MAX_VALUE)) > 0 && w.negate().signum() == -1;
  }
  public static void wideBad() {
    BigInteger w = BigInteger.valueOf(Long.MAX_VALUE).pow(3);
    assert w.toString().equals("784637716923335095224261902710254454442933591094742482943");
  }
  public static void symbolic(long a, long b) {
    BigInteger x = BigInteger.valueOf(a), y = BigInteger.valueOf(b);
    assert x.add(y).subtract(y).equals(x);
    assert Integer.signum(x.compareTo(y)) == Long.compare(a, b);
  }
  public static void symbolicSquare(long a) { BigInteger x = BigInteger.valueOf(a); assert x.multiply(x).signum() >= 0; }
  public static void symbolicOverflowBad(long a, long b) { assert BigInteger.valueOf(a).multiply(BigInteger.valueOf(b)).equals(BigInteger.valueOf(a * b)); }
  public static void symbolicLength(long a) { assert BigInteger.valueOf(a).toString().length() == Long.toString(a).length(); }
}
