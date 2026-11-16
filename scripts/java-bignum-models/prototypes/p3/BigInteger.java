package java.math;

import org.cprover.CProver;

/** Prototype P3: exact, a sign and a magnitude of 32-bit limbs (little
 * endian, without leading zero limbs), as OpenJDK but with schoolbook
 * algorithms and no caches. */
public class BigInteger extends Number implements Comparable<BigInteger> {
    final int signum;
    final int[] mag;

    BigInteger(int signum, int[] mag) { this.signum = mag.length == 0 ? 0 : signum; this.mag = mag; }

    public static final BigInteger ZERO = new BigInteger(0, new int[0]);
    public static final BigInteger ONE = valueOf(1);
    public static final BigInteger TEN = valueOf(10);

    public static BigInteger valueOf(long v) {
        int sign = v < 0 ? -1 : v == 0 ? 0 : 1;
        long m = v < 0 ? -v : v; // (unsigned for MIN_VALUE)
        int lo = (int) m, hi = (int) (m >>> 32);
        int[] mag = hi != 0 ? new int[] {lo, hi} : lo != 0 ? new int[] {lo} : new int[0];
        return new BigInteger(sign, mag);
    }

    static int[] trim(int[] a) {
        int n = a.length;
        while (n > 0 && a[n - 1] == 0) n--;
        if (n == a.length) return a;
        int[] r = new int[n];
        for (int i = 0; i < n; i++) r[i] = a[i];
        return r;
    }

    public BigInteger(String s) {
        int n = s.length();
        int start = n > 0 && (s.charAt(0) == '-' || s.charAt(0) == '+') ? 1 : 0;
        if (start == n) throw new NumberFormatException();
        int[] m = new int[0];
        for (int i = start; i < n; i++) {
            int d = Character.digit(s.charAt(i), 10);
            if (d < 0) throw new NumberFormatException();
            m = mulAdd(m, 10, d);
        }
        m = trim(m);
        signum = m.length == 0 ? 0 : s.charAt(0) == '-' ? -1 : 1;
        mag = m;
    }

    /** a * f + d for small f, d */
    static int[] mulAdd(int[] a, int f, int d) {
        int[] r = new int[a.length + 1];
        long carry = d;
        for (int i = 0; i < a.length; i++) {
            long t = (a[i] & 0xFFFFFFFFL) * f + carry;
            r[i] = (int) t;
            carry = t >>> 32;
        }
        r[a.length] = (int) carry;
        return trim(r);
    }

    static int compareMag(int[] a, int[] b) {
        if (a.length != b.length) return a.length < b.length ? -1 : 1;
        for (int i = a.length - 1; i >= 0; i--) {
            if (a[i] != b[i]) return Integer.compareUnsigned(a[i], b[i]) < 0 ? -1 : 1;
        }
        return 0;
    }

    static int[] addMag(int[] a, int[] b) {
        int n = Math.max(a.length, b.length);
        int[] r = new int[n + 1];
        long carry = 0;
        for (int i = 0; i < n; i++) {
            long t = (i < a.length ? a[i] & 0xFFFFFFFFL : 0) + (i < b.length ? b[i] & 0xFFFFFFFFL : 0) + carry;
            r[i] = (int) t;
            carry = t >>> 32;
        }
        r[n] = (int) carry;
        return trim(r);
    }

    /** a - b for a >= b */
    static int[] subMag(int[] a, int[] b) {
        int[] r = new int[a.length];
        long borrow = 0;
        for (int i = 0; i < a.length; i++) {
            long t = (a[i] & 0xFFFFFFFFL) - (i < b.length ? b[i] & 0xFFFFFFFFL : 0) - borrow;
            r[i] = (int) t;
            borrow = t < 0 ? 1 : 0;
        }
        return trim(r);
    }

    public int signum() { return signum; }
    public BigInteger negate() { return new BigInteger(-signum, mag); }

    public BigInteger add(BigInteger o) {
        if (o.signum == 0) return this;
        if (signum == 0) return o;
        if (signum == o.signum) return new BigInteger(signum, addMag(mag, o.mag));
        int c = compareMag(mag, o.mag);
        if (c == 0) return ZERO;
        return c > 0 ? new BigInteger(signum, subMag(mag, o.mag)) : new BigInteger(o.signum, subMag(o.mag, mag));
    }

    public BigInteger subtract(BigInteger o) { return add(o.negate()); }

    public BigInteger multiply(BigInteger o) {
        if (signum == 0 || o.signum == 0) return ZERO;
        int[] r = new int[mag.length + o.mag.length];
        for (int i = 0; i < mag.length; i++) {
            long carry = 0;
            for (int j = 0; j < o.mag.length; j++) {
                long t = (mag[i] & 0xFFFFFFFFL) * (o.mag[j] & 0xFFFFFFFFL) + (r[i + j] & 0xFFFFFFFFL) + carry;
                r[i + j] = (int) t;
                carry = t >>> 32;
            }
            r[i + o.mag.length] = (int) carry;
        }
        return new BigInteger(signum * o.signum, trim(r));
    }

    public int compareTo(BigInteger o) {
        if (signum != o.signum) return signum < o.signum ? -1 : 1;
        int c = compareMag(mag, o.mag);
        return signum < 0 ? -c : c;
    }

    public boolean equals(Object x) {
        if (!(x instanceof BigInteger)) return false;
        BigInteger o = (BigInteger) x;
        return signum == o.signum && compareMag(mag, o.mag) == 0;
    }

    public int hashCode() {
        int h = 0;
        for (int i = mag.length - 1; i >= 0; i--) h = 31 * h + mag[i];
        return h * signum;
    }

    public String toString() {
        if (mag.length <= 2 && (mag.length < 2 || mag[1] >= 0)) return Long.toString(longValue());
        // chunks of nine digits by repeated division by 10^9
        int[] m = mag;
        String s = "";
        while (m.length != 0) {
            int[] q = new int[m.length];
            long rem = 0;
            for (int i = m.length - 1; i >= 0; i--) {
                long cur = (rem << 32) | (m[i] & 0xFFFFFFFFL);
                q[i] = (int) (cur / 1000000000L);
                rem = cur % 1000000000L;
            }
            m = trim(q);
            String c = Long.toString(rem);
            s = m.length != 0 ? "000000000".substring(c.length()) + c + s : c + s;
        }
        return signum < 0 ? "-" + s : s;
    }

    public long longValue() {
        long v = 0;
        if (mag.length > 0) v = mag[0] & 0xFFFFFFFFL;
        if (mag.length > 1) v |= ((long) mag[1]) << 32;
        return signum < 0 ? -v : v;
    }
    public int intValue() { return (int) longValue(); }
    public float floatValue() { return (float) doubleValue(); }
    public double doubleValue() {
        double d = 0;
        for (int i = mag.length - 1; i >= 0; i--) d = d * 4294967296.0 + (mag[i] & 0xFFFFFFFFL);
        return signum * d;
    }
}
