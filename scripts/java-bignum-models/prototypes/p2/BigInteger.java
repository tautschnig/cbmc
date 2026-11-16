package java.math;

import org.cprover.CProver;

/** Prototype P2: the value as a 128-bit two's complement (hi, lo), or
 * "wide" (beyond), where only the sign is known (in hi). */
public class BigInteger extends Number implements Comparable<BigInteger> {
    final long hi, lo;
    final boolean wide;

    BigInteger(long hi, long lo, boolean wide) { this.hi = hi; this.lo = lo; this.wide = wide; }

    static BigInteger wide(int sign) { return new BigInteger(sign, 0, true); }

    public static final BigInteger ZERO = new BigInteger(0, 0, false);
    public static final BigInteger ONE = new BigInteger(0, 1, false);
    public static final BigInteger TEN = new BigInteger(0, 10, false);

    public static BigInteger valueOf(long v) { return new BigInteger(v >> 63, v, false); }

    static final long E18 = 1000000000000000000L;

    public BigInteger(String s) {
        int n = s.length();
        boolean neg = n > 0 && s.charAt(0) == '-';
        int start = n > 0 && (s.charAt(0) == '-' || s.charAt(0) == '+') ? 1 : 0;
        int digits = n - start;
        if (digits <= 18) {
            long v = Long.parseLong(s);
            hi = v >> 63; lo = v; wide = false;
        } else if (digits <= 36) {
            long head = Long.parseLong(s.substring(start, n - 18));
            long tail = Long.parseLong(s.substring(n - 18, n));
            // head * 10^18 + tail, both nonnegative, below 2^127
            long h = multiplyHighUnsigned(head, E18);
            long l = head * E18;
            long l2 = l + tail;
            if (Long.compareUnsigned(l2, l) < 0) h++;
            if (neg) { // negate
                l2 = ~l2 + 1; h = ~h + (l2 == 0 ? 1 : 0);
            }
            hi = h; lo = l2; wide = false;
        } else {
            hi = neg ? -1 : 1; lo = 0; wide = true;
        }
    }

    static long multiplyHighUnsigned(long x, long y) {
        long x1 = x >>> 32, x2 = x & 0xFFFFFFFFL, y1 = y >>> 32, y2 = y & 0xFFFFFFFFL;
        long z2 = x2 * y2;
        long t = x1 * y2 + (z2 >>> 32);
        long z1 = (t & 0xFFFFFFFFL) + x2 * y1;
        return x1 * y1 + (t >>> 32) + (z1 >>> 32);
    }

    public int signum() {
        if (wide) return (int) hi;
        if (hi < 0) return -1;
        return hi == 0 && lo == 0 ? 0 : 1;
    }

    boolean fitsLong() { return !wide && hi == (lo >> 63); }

    public BigInteger negate() {
        if (wide) return wide(-(int) hi);
        long l = ~lo + 1;
        long h = ~hi + (l == 0 ? 1 : 0);
        if (hi == Long.MIN_VALUE && lo == 0) return wide(1);
        return new BigInteger(h, l, false);
    }

    public BigInteger add(BigInteger o) {
        if (!wide && !o.wide) {
            long l = lo + o.lo;
            long h = hi + o.hi + (Long.compareUnsigned(l, lo) < 0 ? 1 : 0);
            if (((hi ^ h) & (o.hi ^ h)) < 0) return wide(hi < 0 ? -1 : 1);
            return new BigInteger(h, l, false);
        }
        return any();
    }

    public BigInteger subtract(BigInteger o) { return add(o.negate()); }

    public BigInteger multiply(BigInteger o) {
        int sign = signum() * o.signum();
        if (sign == 0) return ZERO;
        if (wide || o.wide) return wide(sign);
        BigInteger a = signum() < 0 ? negateRaw() : this;
        BigInteger b = o.signum() < 0 ? o.negateRaw() : o;
        // unsigned 128 x 128 by 32-bit limbs
        long[] x = {a.lo & 0xFFFFFFFFL, a.lo >>> 32, a.hi & 0xFFFFFFFFL, a.hi >>> 32};
        long[] y = {b.lo & 0xFFFFFFFFL, b.lo >>> 32, b.hi & 0xFFFFFFFFL, b.hi >>> 32};
        long[] z = new long[8];
        for (int i = 0; i < 4; i++) {
            long carry = 0;
            for (int j = 0; j < 4; j++) {
                // below 2^64: (2^32-1)^2 + 2 (2^32-1)
                long t = x[i] * y[j] + z[i + j] + carry;
                z[i + j] = t & 0xFFFFFFFFL;
                carry = t >>> 32;
            }
            z[i + 4] = carry;
        }
        if ((z[4] | z[5] | z[6] | z[7]) != 0) return wide(sign);
        long l = z[0] | (z[1] << 32), h = z[2] | (z[3] << 32);
        if (h < 0) {
            if (sign < 0 && h == Long.MIN_VALUE && l == 0) return new BigInteger(h, l, false);
            return wide(sign);
        }
        BigInteger r = new BigInteger(h, l, false);
        return sign < 0 ? r.negateRaw() : r;
    }

    BigInteger negateRaw() {
        long l = ~lo + 1;
        return new BigInteger(~hi + (l == 0 ? 1 : 0), l, false);
    }

    static BigInteger any() {
        boolean w = CProver.nondetBoolean();
        long h = CProver.nondetLong(), l = CProver.nondetLong();
        if (w) CProver.assume((h == 1 || h == -1) && l == 0);
        return new BigInteger(h, l, w);
    }

    public int compareTo(BigInteger o) {
        if (!wide && !o.wide) {
            if (hi != o.hi) return hi < o.hi ? -1 : 1;
            return Long.compareUnsigned(lo, o.lo) < 0 ? -1 : lo == o.lo ? 0 : 1;
        }
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
        if (!wide && !o.wide) return hi == o.hi && lo == o.lo;
        if (wide != o.wide || hi != o.hi) return false;
        return CProver.nondetBoolean();
    }

    public int hashCode() { return CProver.nondetInt(); }

    public String toString() {
        if (wide) return CProver.nondetWithoutNull("");
        if (fitsLong()) return Long.toString(lo);
        boolean neg = hi < 0;
        BigInteger m = neg ? negateRaw() : this;
        // unsigned 128 bits by 10^9, five chunks of nine digits
        long[] limb = {m.hi >>> 32, m.hi & 0xFFFFFFFFL, m.lo >>> 32, m.lo & 0xFFFFFFFFL};
        long[] chunk = new long[5];
        for (int k = 0; k < 5; k++) {
            long rem = 0;
            for (int i = 0; i < 4; i++) {
                long cur = (rem << 32) | limb[i];
                limb[i] = cur / 1000000000L;
                rem = cur % 1000000000L;
            }
            chunk[k] = rem;
        }
        // the value is beyond a long, so at least 19 digits: chunks 4..0
        String s = "";
        boolean started = false;
        for (int k = 4; k >= 0; k--) {
            if (started) {
                String c = Long.toString(chunk[k]);
                s = s + "000000000".substring(c.length()) + c;
            } else if (chunk[k] != 0) {
                s = Long.toString(chunk[k]);
                started = true;
            }
        }
        return neg ? "-" + s : s;
    }

    public long longValue() { return wide ? CProver.nondetLong() : lo; }
    public int intValue() { return (int) longValue(); }
    public float floatValue() { return (float) doubleValue(); }
    public double doubleValue() { return wide || !fitsLong() ? CProver.nondetDouble() : (double) lo; }
}
