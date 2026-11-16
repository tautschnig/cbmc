import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.MathContext;
import java.math.RoundingMode;
import org.cprover.CProver;

public class Test {
  public static void arithmetic() {
    BigDecimal a = new BigDecimal("1234.5678"), b = new BigDecimal("-0.05");
    assert a.add(b).toString().equals("1234.5178");
    assert a.multiply(b).toString().equals("-61.728390");
    assert a.divide(b, 3, RoundingMode.HALF_EVEN).toString().equals("-24691.356");
    assert a.divide(b, new MathContext(5)).toString().equals("-24691");
    assert new BigDecimal("1").divide(new BigDecimal("8")).toString().equals("0.125");
    assert new BigDecimal("1.5").remainder(new BigDecimal("0.4")).toString().equals("0.3");
  }
  public static void rounding() {
    assert new BigDecimal("2.5").setScale(0, RoundingMode.HALF_EVEN).intValue() == 2;
    assert new BigDecimal("-2.5").setScale(0, RoundingMode.HALF_DOWN).intValue() == -2;
    assert new BigDecimal("-2.5").setScale(0, RoundingMode.CEILING).intValue() == -2;
    assert new BigDecimal("999.9").round(new MathContext(3)).toString().equals("1.00E+3");
  }
  public static void strings() {
    BigDecimal x = new BigDecimal("1E+5");
    assert x.toString().equals("1E+5") && x.toPlainString().equals("100000") && x.toEngineeringString().equals("100E+3");
    assert new BigDecimal("0.00001").toString().equals("0.000010") == false;
    assert new BigDecimal("0.000001").toString().equals("0.0000010") == false;
    assert new BigDecimal("1.0E-7").toString().equals("1.0E-7");
    assert new BigDecimal("1.50").stripTrailingZeros().toString().equals("1.5");
  }
  public static void comparison() {
    BigDecimal x = new BigDecimal("1.0"), y = new BigDecimal("1.00");
    assert !x.equals(y) && x.compareTo(y) == 0 && x.hashCode() == 311;
  }
  public static void fromDouble() {
    assert new BigDecimal(1.5).toString().equals("1.5");
    assert BigDecimal.valueOf(0.1).toString().equals("0.1");
    assert new BigDecimal(0.25).scale() == 2;
  }
  public static void fromDoubleBad() { assert !new BigDecimal(0.1).toString().startsWith("0.1000000000000000055511151231257827"); }
  public static void exceptions() {
    try { new BigDecimal("10").divide(new BigDecimal("3")); assert false; } catch (ArithmeticException e) { assert e.getMessage().equals("Non-terminating decimal expansion; no exact representable decimal result."); }
    try { new BigDecimal("2.5").setScale(0, RoundingMode.UNNECESSARY); assert false; } catch (ArithmeticException e) { assert e.getMessage().equals("Rounding necessary"); }
    try { new BigDecimal("1e20").longValueExact(); assert false; } catch (ArithmeticException e) { assert e.getMessage().equals("Overflow"); }
  }
  public static void parseMessage9() { try { new BigDecimal("1.2.3"); assert false; } catch (NumberFormatException e) { assert "Character array contains more than one decimal point.".equals(e.getMessage()); } }
  public static void parseMessage8() { try { new BigDecimal("1.2.3"); assert false; } catch (NumberFormatException e) { assert e.getMessage() == null; } }
  public static void exponent19() { assert new BigDecimal("1e2147483648").scale() == Integer.MIN_VALUE; }
  public static void exponent18() { try { new BigDecimal("1e2147483648"); assert false; } catch (NumberFormatException e) { assert "Exponent overflow.".equals(e.getMessage()); } }
  public static void movePoint() { assert new BigDecimal(BigInteger.valueOf(42), -2).movePointLeft(0).toString().equals("4200"); }
  public static void symbolic(long a, int s) {
    CProver.assume(s >= 0 && s < 10);
    BigDecimal x = BigDecimal.valueOf(a, s);
    assert x.add(x).compareTo(x.multiply(BigDecimal.valueOf(2))) == 0;
  }
  public static void symbolicRound(int a) {
    BigDecimal x = BigDecimal.valueOf(a, 2).setScale(0, RoundingMode.HALF_UP);
    assert x.scale() == 0 && x.abs().compareTo(BigDecimal.valueOf(Math.abs((long) a))) <= 0;
  }
}
