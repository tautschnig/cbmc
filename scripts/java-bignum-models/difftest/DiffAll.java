import java.math.BigInteger;
import java.util.*;
import java.util.function.*;

/** Compares the model (model.BigInteger) with java.math.BigInteger on random
 * values, for each operation: the results (as strings), the exceptions (class
 * and message), skipping results that the model gives as wide. */
public class DiffAll {
  static Random r = new Random(11);
  static long ok, bad, skipped;
  static Map<String, long[]> per = new TreeMap<>();
  static BigInteger rand() {
    int k = r.nextInt(10);
    BigInteger v;
    if (k == 0) v = BigInteger.valueOf(r.nextInt(5) - 2);
    else if (k == 1) v = BigInteger.ONE.shiftLeft(r.nextInt(135)).add(BigInteger.valueOf(r.nextInt(3) - 1));
    else if (k < 4) v = BigInteger.valueOf(r.nextLong() >> r.nextInt(64));
    else v = new BigInteger(r.nextInt(132), r);
    return r.nextBoolean() ? v.negate() : v;
  }
  static model.BigInteger m(BigInteger x) { return new model.BigInteger(x.toString()); }
  static String show(Object o) {
    if (o instanceof Object[]) return Arrays.deepToString((Object[]) o);
    if (o instanceof byte[]) return Arrays.toString((byte[]) o);
    return String.valueOf(o);
  }
  static String run(Supplier<Object> s) {
    try { return show(s.get()); }
    catch (RuntimeException e) {
      if (e.getMessage() != null && e.getMessage().startsWith("Cannot execute")) return null;
      return e.getClass().getName() + ": " + e.getMessage();
    }
  }
  static boolean wide(Object o) {
    if (o instanceof model.BigInteger) { try { java.lang.reflect.Field f = model.BigInteger.class.getDeclaredField("wide"); f.setAccessible(true); return f.getBoolean(o); } catch (Exception e) { throw new RuntimeException(e); } }
    if (o instanceof Object[]) { for (Object x : (Object[]) o) if (wide(x)) return true; }
    return false;
  }
  static void check(String op, Supplier<Object> want, Supplier<Object> got) {
    long[] c = per.computeIfAbsent(op, k -> new long[3]);
    Object g0;
    try { g0 = got.get(); } catch (RuntimeException e) { g0 = e; }
    final Object g = g0;
    if (wide(g)) { skipped++; c[2]++; return; }
    String w = run(want), h = g instanceof RuntimeException ? run(() -> { throw (RuntimeException) g; }) : show(g);
    if (h == null) { skipped++; c[2]++; return; }
    if (w.equals(h)) { ok++; c[0]++; } else { bad++; c[1]++; if (c[1] <= 3) System.out.println("BAD " + op + ": want " + w + " got " + h); }
  }
  static RuntimeException got_ex(Object g) { return (RuntimeException) g; }
  public static void main(String[] a) {
    int n = Integer.parseInt(a[0]);
    for (int i = 0; i < n; i++) {
      BigInteger x = rand(), y = rand();
      int e = r.nextInt(70) - 3, b = r.nextInt(150) - 5, radix = r.nextInt(40);
      model.BigInteger mx = m(x), my = m(y);
      check("add", () -> x.add(y), () -> mx.add(my));
      check("subtract", () -> x.subtract(y), () -> mx.subtract(my));
      check("multiply", () -> x.multiply(y), () -> mx.multiply(my));
      check("divide", () -> x.divide(y), () -> mx.divide(my));
      check("remainder", () -> x.remainder(y), () -> mx.remainder(my));
      check("divideAndRemainder", () -> x.divideAndRemainder(y), () -> mx.divideAndRemainder(my));
      check("mod", () -> x.mod(y), () -> mx.mod(my));
      check("pow", () -> x.pow(e), () -> mx.pow(e));
      check("gcd", () -> x.gcd(y), () -> mx.gcd(my));
      check("negate abs signum", () -> x.negate() + " " + x.abs() + " " + x.signum(), () -> mx.negate() + " " + mx.abs() + " " + mx.signum());
      check("compareTo equals", () -> x.compareTo(y) + " " + x.equals(y) + " " + x.equals(x.add(BigInteger.ZERO)), () -> mx.compareTo(my) + " " + mx.equals(my) + " " + mx.equals(mx.add(model.BigInteger.ZERO)));
      check("min max", () -> x.min(y) + " " + x.max(y), () -> mx.min(my) + " " + mx.max(my));
      check("hashCode", () -> x.hashCode(), () -> mx.hashCode());
      check("toString radix", () -> x.toString(radix), () -> mx.toString(radix));
      check("parse radix", () -> new BigInteger(x.toString(radix < 2 || radix > 36 ? 10 : radix), radix < 2 || radix > 36 ? 10 : radix), () -> new model.BigInteger(x.toString(radix < 2 || radix > 36 ? 10 : radix), radix < 2 || radix > 36 ? 10 : radix));
      check("shiftLeft", () -> x.shiftLeft(b), () -> mx.shiftLeft(b));
      check("shiftRight", () -> x.shiftRight(b), () -> mx.shiftRight(b));
      check("and or xor andNot not", () -> x.and(y) + " " + x.or(y) + " " + x.xor(y) + " " + x.andNot(y) + " " + x.not(), () -> mx.and(my) + " " + mx.or(my) + " " + mx.xor(my) + " " + mx.andNot(my) + " " + mx.not());
      check("testBit", () -> x.testBit(b), () -> mx.testBit(b));
      check("setBit clearBit flipBit", () -> x.setBit(b) + " " + x.clearBit(b) + " " + x.flipBit(b), () -> mx.setBit(b) + " " + mx.clearBit(b) + " " + mx.flipBit(b));
      check("bitLength bitCount lowest", () -> x.bitLength() + " " + x.bitCount() + " " + x.getLowestSetBit(), () -> mx.bitLength() + " " + mx.bitCount() + " " + mx.getLowestSetBit());
      check("toByteArray", () -> x.toByteArray(), () -> mx.toByteArray());
      check("fromByteArray", () -> new BigInteger(x.toByteArray()), () -> new model.BigInteger(x.toByteArray()));
      check("signum-magnitude", () -> new BigInteger(x.signum(), x.abs().toByteArray()), () -> new model.BigInteger(x.signum(), x.abs().toByteArray()));
      check("intValue longValue", () -> x.intValue() + " " + x.longValue(), () -> mx.intValue() + " " + mx.longValue());
      check("exact", () -> x.longValueExact() + " " + x.intValueExact(), () -> mx.longValueExact() + " " + mx.intValueExact());
      check("doubleValue floatValue", () -> x.doubleValue() + " " + x.floatValue(), () -> mx.doubleValue() + " " + mx.floatValue());
      check("sqrt", () -> x.sqrt(), () -> mx.sqrt());
      BigInteger mod = x.abs().shiftRight(r.nextInt(130)).add(BigInteger.ONE);
      check("modPow", () -> y.modPow(x.abs().shiftRight(60), mod), () -> my.modPow(m(x.abs().shiftRight(60)), m(mod)));
      check("modInverse", () -> y.modInverse(mod), () -> my.modInverse(m(mod)));
      check("isProbablePrime", () -> x.isProbablePrime(20), () -> mx.isProbablePrime(20));
    }
    // parse errors and odd strings
    String[] ins = {"", "-", "+", "+-1", "-+1", "--1", "\u0661\u0662", "\u0de6\u0de7", "1_0", " 1", "1 ", "0x10", "00012", "-0", "+0", "\uff11\uff12", "1e3", "z", "0000000000000000000000000000000000000000001", "12345678901234567890x", "\uff21"};
    for (String s : ins) for (int radix : new int[]{10, 16, 36, 1, 37}) check("parse [" + s + "] " + radix, () -> new BigInteger(s, radix), () -> new model.BigInteger(s, radix));
    for (Map.Entry<String, long[]> en : per.entrySet()) System.out.printf("%-26s ok %7d bad %5d wide %6d%n", en.getKey(), en.getValue()[0], en.getValue()[1], en.getValue()[2]);
    System.out.println("TOTAL ok " + ok + " bad " + bad + " wide/unknown " + skipped);
  }
}
