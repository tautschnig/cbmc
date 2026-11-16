import java.math.*;
import java.util.*;
import java.util.function.*;

/** Compares the model's BigDecimal (model.BigDecimal) with java.math's. */
public class DiffBD {
  static Random r = new Random(5);
  static long ok, bad, skipped;
  static Map<String, long[]> per = new TreeMap<>();
  static BigDecimal rand() {
    int k = r.nextInt(10);
    BigInteger u;
    if (k == 0) u = BigInteger.valueOf(r.nextInt(21) - 10);
    else if (k < 4) u = BigInteger.valueOf(r.nextLong() >> r.nextInt(64));
    else if (k < 6) u = BigInteger.TEN.pow(r.nextInt(20)).multiply(BigInteger.valueOf(r.nextInt(1000)));
    else u = new BigInteger(r.nextInt(125), r);
    if (r.nextBoolean()) u = u.negate();
    int s = r.nextInt(8) == 0 ? r.nextInt(60) - 30 : r.nextInt(12) - 3;
    return new BigDecimal(u, s);
  }
  static model.BigDecimal m(BigDecimal x) { return new model.BigDecimal(new model.BigInteger(x.unscaledValue().toString()), x.scale()); }
  static model.MathContext mc(MathContext c) { return new model.MathContext(c.getPrecision(), model.RoundingMode.valueOf(c.getRoundingMode().ordinal())); }
  static String show(Object o) {
    if (o instanceof Object[]) { Object[] a = (Object[]) o; StringBuilder sb = new StringBuilder("["); for (Object x : a) sb.append(show(x)).append(","); return sb + "]"; }
    if (o instanceof BigDecimal) { BigDecimal d = (BigDecimal) o; return d.unscaledValue() + "e" + d.scale(); }
    if (o instanceof model.BigDecimal) { model.BigDecimal d = (model.BigDecimal) o; return d.unscaledValue() + "e" + d.scale(); }
    return String.valueOf(o);
  }
  static boolean wide(Object o) {
    try {
      if (o instanceof model.BigInteger) { java.lang.reflect.Field f = model.BigInteger.class.getDeclaredField("wide"); f.setAccessible(true); return f.getBoolean(o); }
      if (o instanceof model.BigDecimal) return wide(((model.BigDecimal) o).unscaledValue());
      if (o instanceof Object[]) { for (Object x : (Object[]) o) if (wide(x)) return true; }
    } catch (Exception e) { throw new RuntimeException(e); }
    return false;
  }
  static String run(Supplier<Object> s) {
    try { return show(s.get()); }
    catch (RuntimeException e) {
      if (e.getMessage() != null && e.getMessage().startsWith("Cannot execute")) return null;
      return e.getClass().getName() + ": " + e.getMessage();
    }
  }
  static void check(String op, Supplier<Object> want, Supplier<Object> got) {
    long[] c = per.computeIfAbsent(op, k -> new long[3]);
    Object g0;
    try { g0 = got.get(); } catch (RuntimeException e) { g0 = e; }
    final Object g = g0;
    if (wide(g)) { skipped++; c[2]++; return; }
    String w = run(want), h = g instanceof RuntimeException ? run(() -> { throw (RuntimeException) g; }) : show(g);
    if (h == null) { skipped++; c[2]++; return; }
    if (w.equals(h)) { ok++; c[0]++; } else { bad++; c[1]++; if (c[1] <= 4) System.out.println("BAD " + op + ": want " + w + " got " + h); }
  }
  public static void main(String[] a) {
    int n = Integer.parseInt(a[0]);
    RoundingMode[] modes = RoundingMode.values();
    for (int i = 0; i < n; i++) {
      BigDecimal x = rand(), y = rand();
      model.BigDecimal mx = m(x), my = m(y);
      RoundingMode rm = modes[r.nextInt(modes.length)];
      model.RoundingMode mrm = model.RoundingMode.valueOf(rm.ordinal());
      MathContext c = new MathContext(r.nextInt(25), rm);
      model.MathContext mcx = mc(c);
      int sc = r.nextInt(16) - 4, prec = r.nextInt(14), e = r.nextInt(6);
      check("add", () -> x.add(y), () -> mx.add(my));
      check("subtract", () -> x.subtract(y), () -> mx.subtract(my));
      check("multiply", () -> x.multiply(y), () -> mx.multiply(my));
      check("add mc", () -> x.add(y, c), () -> mx.add(my, mcx));
      check("multiply mc", () -> x.multiply(y, c), () -> mx.multiply(my, mcx));
      check("divide scale", () -> x.divide(y, sc, rm), () -> mx.divide(my, sc, mrm));
      check("divide mode", () -> x.divide(y, rm), () -> mx.divide(my, mrm));
      check("divide mc", () -> x.divide(y, c), () -> mx.divide(my, mcx));
      check("divide exact", () -> x.divide(y), () -> mx.divide(my));
      check("divideToIntegralValue", () -> x.divideToIntegralValue(y), () -> mx.divideToIntegralValue(my));
      check("divideToIntegralValue mc", () -> x.divideToIntegralValue(y, c), () -> mx.divideToIntegralValue(my, mcx));
      check("remainder", () -> x.remainder(y), () -> mx.remainder(my));
      check("setScale", () -> x.setScale(sc, rm), () -> mx.setScale(sc, mrm));
      check("round", () -> x.round(c), () -> mx.round(mcx));
      check("compareTo equals hash", () -> x.compareTo(y) + " " + x.equals(y) + " " + x.hashCode() + " " + x.signum() + " " + x.precision(), () -> mx.compareTo(my) + " " + mx.equals(my) + " " + mx.hashCode() + " " + mx.signum() + " " + mx.precision());
      check("toString", () -> x.toString() + " " + x.toPlainString() + " " + x.toEngineeringString(), () -> mx.toString() + " " + mx.toPlainString() + " " + mx.toEngineeringString());
      check("parse", () -> new BigDecimal(x.toString()), () -> new model.BigDecimal(x.toString()));
      check("parse eng", () -> new BigDecimal(x.toEngineeringString()), () -> new model.BigDecimal(x.toEngineeringString()));
      check("strip", () -> x.stripTrailingZeros(), () -> mx.stripTrailingZeros());
      check("movePoint", () -> x.movePointLeft(sc) + " " + x.movePointRight(sc) + " " + x.scaleByPowerOfTen(sc), () -> mx.movePointLeft(sc) + " " + mx.movePointRight(sc) + " " + mx.scaleByPowerOfTen(sc));
      check("pow", () -> x.pow(e), () -> mx.pow(e));
      check("toBigInteger long int", () -> x.toBigInteger() + " " + x.longValue() + " " + x.intValue(), () -> mx.toBigInteger() + " " + mx.longValue() + " " + mx.intValue());
      check("exact", () -> x.longValueExact() + " " + x.intValueExact(), () -> mx.longValueExact() + " " + mx.intValueExact());
      check("toBigIntegerExact", () -> x.toBigIntegerExact(), () -> mx.toBigIntegerExact());
      check("doubleValue", () -> x.doubleValue(), () -> mx.doubleValue());
      double d = r.nextInt(3) == 0 ? Double.longBitsToDouble(r.nextLong()) : (r.nextInt(2000000) - 1000000) / 64.0;
      if (!Double.isNaN(d) && !Double.isInfinite(d)) {
        check("new double", () -> new BigDecimal(d), () -> new model.BigDecimal(d));
      }
      for (char cv : new char[]{'e', 'f', 'g'}) {
        boolean alt = r.nextInt(4) == 0 && cv != 'g';
        int p = r.nextInt(3) == 0 ? -1 : prec;
        String spec = "%" + (alt ? "#" : "") + (p >= 0 ? "." + p : "") + cv;
        check("format %" + cv, () -> String.format(Locale.US, spec, x), () -> model.CProverMath.formatDecimal(mx, cv, p, alt));
      }
    }
    String[] ins = {"", "-", "+", ".", "1.", ".5", "1e", "1e+", "1e5", "1E-5", "1.2.3", "1e1.5", "--1", "+-1", "\u0661.\u0662", "1e99999999999", "1e2147483648", "1e-2147483648", "1e-2147483649", "0E+3", "0.000", "-0.00", "1_0", " 1", "12a", "1e+-3", "123456789012345678901234567890.5e-3", "1234567890123456789x", "12345678901234567x", "\uff11", "0x10", "NaN", "e5", "1e00000000000005", "1e0000000000012345678901", "\u0de6"};
    for (String s : ins) check("parse [" + s + "]", () -> new BigDecimal(s), () -> new model.BigDecimal(s));
    for (Map.Entry<String, long[]> en : per.entrySet()) if (!en.getKey().startsWith("parse [")) System.out.printf("%-26s ok %7d bad %5d unknown %6d%n", en.getKey(), en.getValue()[0], en.getValue()[1], en.getValue()[2]);
    System.out.println("TOTAL ok " + ok + " bad " + bad + " wide/unknown " + skipped);
  }
}
