import java.util.Random;
public class DiffVO { public static void main(String[] a) { Random r = new Random(3); long bad = 0, n = 0;
  for (int i = 0; i < 300000; i++) { double d = i % 3 == 0 ? Double.longBitsToDouble(r.nextLong()) : (r.nextInt() / 1000.0) * Math.pow(10, r.nextInt(40) - 20); if (Double.isNaN(d) || Double.isInfinite(d)) continue; n++;
    java.math.BigDecimal w = java.math.BigDecimal.valueOf(d); model.BigDecimal g;
    try { g = model.BigDecimal.valueOf(d); } catch (RuntimeException e) { if (e.getMessage() != null && e.getMessage().startsWith("Cannot")) continue; throw e; }
    if (!w.unscaledValue().toString().equals(g.unscaledValue().toString()) || w.scale() != g.scale()) { if (bad++ < 5) System.out.println("BAD " + d + " " + w + " " + g); } }
  System.out.println("valueOf(double): " + n + " bad " + bad); } }
