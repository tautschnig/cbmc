import java.math.BigInteger;

public class Unwind {
  public static void arithmetic() {
    BigInteger m = BigInteger.valueOf(Long.MAX_VALUE);
    BigInteger sq = m.multiply(m);
    assert sq.toString().equals("85070591730234615847396907784232501249");
    assert sq.divide(BigInteger.valueOf(1000003)).mod(BigInteger.valueOf(97)).intValue() == 31;
    assert sq.gcd(m).equals(m);
    assert sq.sqrt().equals(m);
  }
}
