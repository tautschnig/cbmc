package java.math;

import org.cprover.CProver;

/** Prototype P1: the value as a long, or "wide" (beyond the range of long),
 * where only the sign is known. */
public class BigInteger extends Number implements Comparable<BigInteger> {
    final long value;  // for a wide value, -1 or 1 (its sign)
    final boolean wide;

    BigInteger(long value, boolean wide) { this.value = value; this.wide = wide; }

    static BigInteger wide(int sign) { return new BigInteger(sign, true); }

    public static final BigInteger ZERO = new BigInteger(0, false);
    public static final BigInteger ONE = new BigInteger(1, false);
    public static final BigInteger TEN = new BigInteger(10, false);

    public static BigInteger valueOf(long v) { return new BigInteger(v, false); }

    public BigInteger(String s) {
        // up to 18 digits are a long; more digits are wide (or a long with
        // leading zeros, which this prototype does not handle)
        int n = s.length();
        int digits = n > 0 && (s.charAt(0) == '-' || s.charAt(0) == '+') ? n - 1 : n;
        if (digits <= 18) {
            value = Long.parseLong(s);
            wide = false;
        } else {
            value = s.charAt(0) == '-' ? -1 : 1;
            wide = true;
        }
    }

    public int signum() { return wide ? (int) value : Long.signum(value); }

    public BigInteger negate() {
        if (wide) return wide(-(int) value);
        if (value == Long.MIN_VALUE) return wide(1);
        return valueOf(-value);
    }

    public BigInteger add(BigInteger o) {
        if (!wide && !o.wide) {
            long r = value + o.value;
            if (((value ^ r) & (o.value ^ r)) < 0) return wide(value < 0 ? -1 : 1);
            return valueOf(r);
        }
        return any();
    }

    public BigInteger subtract(BigInteger o) { return add(o.negate()); }

    public BigInteger multiply(BigInteger o) {
        int sign = signum() * o.signum();
        if (!wide && !o.wide) {
            long hi = multiplyHigh(value, o.value);
            long lo = value * o.value;
            if ((hi == 0 && lo >= 0) || (hi == -1 && lo < 0)) return valueOf(lo);
            return wide(sign);
        }
        if (sign == 0) return ZERO;
        return wide(sign);
    }

    /** Math.multiplyHigh of Java 9 */
    static long multiplyHigh(long x, long y) {
        long x1 = x >> 32, x2 = x & 0xFFFFFFFFL;
        long y1 = y >> 32, y2 = y & 0xFFFFFFFFL;
        long z2 = x2 * y2;
        long t = x1 * y2 + (z2 >>> 32);
        long z1 = t & 0xFFFFFFFFL;
        long z0 = t >> 32;
        z1 += x2 * y1;
        return x1 * y1 + z0 + (z1 >> 32);
    }

    /** any value */
    static BigInteger any() {
        boolean w = CProver.nondetBoolean();
        long v = CProver.nondetLong();
        if (w) CProver.assume(v == 1 || v == -1);
        return new BigInteger(v, w);
    }

    public int compareTo(BigInteger o) {
        if (!wide && !o.wide) return Long.compare(value, o.value);
        int s = signum(), t = o.signum();
        if (wide && !o.wide) return s;
        if (!wide && o.wide) return -t;
        if (s != t) return s < t ? -1 : 1;
        int r = CProver.nondetInt();
        CProver.assume(r >= -1 && r <= 1);
        return r;
    }

    public boolean equals(Object x) {
        if (!(x instanceof BigInteger)) return false;
        BigInteger o = (BigInteger) x;
        if (!wide && !o.wide) return value == o.value;
        if (wide != o.wide || value != o.value) return false;
        return CProver.nondetBoolean();
    }

    public int hashCode() { return wide ? CProver.nondetInt() : (int) (value ^ (value >>> 32)); }

    public String toString() {
        if (!wide) return Long.toString(value);
        return CProver.nondetWithoutNull("");
    }

    public long longValue() { return wide ? CProver.nondetLong() : value; }
    public int intValue() { return (int) longValue(); }
    public float floatValue() { return (float) longValue(); }
    public double doubleValue() { return wide ? CProver.nondetDouble() : (double) value; }
}
