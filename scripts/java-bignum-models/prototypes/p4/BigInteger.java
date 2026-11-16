package java.math;

import org.cprover.CProver;

/** Prototype P4: a sign and a magnitude of N 32-bit limbs (little endian,
 * fixed length, so loops have constant bounds), exact up to 32 N bits, or
 * "wide" beyond, where only the sign is known. */
public class BigInteger extends Number implements Comparable<BigInteger> {
    static final int N = 8;
    final int signum;
    final int[] mag; // length N
    final boolean wide;

    BigInteger(int signum, int[] mag, boolean wide) { this.signum = signum; this.mag = mag; this.wide = wide; }

    static BigInteger wide(int sign) { return new BigInteger(sign, new int[N], true); }

    static BigInteger of(int sign, int[] mag) {
        boolean zero = true;
        for (int i = 0; i < N; i++) if (mag[i] != 0) zero = false;
        return new BigInteger(zero ? 0 : sign, mag, false);
    }

    public static final BigInteger ZERO = valueOf(0);
    public static final BigInteger ONE = valueOf(1);
    public static final BigInteger TEN = valueOf(10);

    public static BigInteger valueOf(long v) {
        long m = v < 0 ? -v : v;
        int[] mag = new int[N];
        mag[0] = (int) m;
        mag[1] = (int) (m >>> 32);
        return new BigInteger(v < 0 ? -1 : v == 0 ? 0 : 1, mag, false);
    }

    boolean fitsLong() {
        if (wide) return false;
        for (int i = 2; i < N; i++) if (mag[i] != 0) return false;
        return mag[1] >= 0 || (signum < 0 && mag[1] == Integer.MIN_VALUE && mag[0] == 0);
    }

    public BigInteger(String s) {
        int n = s.length();
        int start = n > 0 && (s.charAt(0) == '-' || s.charAt(0) == '+') ? 1 : 0;
        int sign = n > 0 && s.charAt(0) == '-' ? -1 : 1;
        if (n - start <= 18) {
            BigInteger v = valueOf(Long.parseLong(s));
            signum = v.signum; mag = v.mag; wide = false;
            return;
        }
        // chunks of 18 digits from the left, N * 32 bits hold N * 9.6 digits
        if (n - start > 18 * ((N * 32 * 3 / 10) / 18 + 1)) {
            signum = sign; mag = new int[N]; wide = true;
            return;
        }
        int[] m = new int[N];
        boolean over = false;
        int first = start + (n - start) % 18;
        int end = first == start ? start + 18 : first;
        int pos = start;
        for (int k = 0; k < (N * 32 * 3 / 10) / 18 + 1; k++) {
            if (pos >= n) break;
            long chunk = Long.parseLong(s.substring(pos, end));
            // m = (m * 10^9 + chunk / 10^9) * 10^9 + chunk % 10^9 (a chunk of
            // fewer digits has a smaller factor: 10^len)
            int len = end - pos;
            long f1 = len > 9 ? 1000000000L : 1;
            long f2 = 1;
            for (int i = 0; i < 9; i++) if (i < (len > 9 ? 9 : len)) f2 *= 10;
            if (len > 9) { f1 = 1; for (int i = 0; i < 9; i++) if (i < len - 9) f1 *= 10; }
            long[] parts = {chunk / f2, chunk % f2};
            long[] factors = {f1, f2};
            for (int step = 0; step < 2; step++) {
                long carry = parts[step];
                for (int i = 0; i < N; i++) {
                    long t = (m[i] & 0xFFFFFFFFL) * factors[step] + carry;
                    m[i] = (int) t;
                    carry = t >>> 32;
                }
                if (carry != 0) over = true;
            }
            pos = end;
            end = end + 18;
        }
        BigInteger v = over ? wide(sign) : of(sign, m);
        signum = v.signum; mag = v.mag; wide = v.wide;
    }

    static int compareMag(int[] a, int[] b) {
        for (int i = N - 1; i >= 0; i--) {
            if (a[i] != b[i]) return Integer.compareUnsigned(a[i], b[i]) < 0 ? -1 : 1;
        }
        return 0;
    }

    public int signum() { return signum; }

    public BigInteger negate() { return new BigInteger(-signum, mag, wide); }

    public BigInteger add(BigInteger o) {
        if (wide || o.wide) return any();
        if (o.signum == 0) return this;
        if (signum == 0) return o;
        int[] r = new int[N];
        if (signum == o.signum) {
            long carry = 0;
            for (int i = 0; i < N; i++) {
                long t = (mag[i] & 0xFFFFFFFFL) + (o.mag[i] & 0xFFFFFFFFL) + carry;
                r[i] = (int) t;
                carry = t >>> 32;
            }
            return carry != 0 ? wide(signum) : of(signum, r);
        }
        int c = compareMag(mag, o.mag);
        int[] a = c >= 0 ? mag : o.mag, b = c >= 0 ? o.mag : mag;
        long borrow = 0;
        for (int i = 0; i < N; i++) {
            long t = (a[i] & 0xFFFFFFFFL) - (b[i] & 0xFFFFFFFFL) - borrow;
            r[i] = (int) t;
            borrow = t < 0 ? 1 : 0;
        }
        return of(c >= 0 ? signum : o.signum, r);
    }

    public BigInteger subtract(BigInteger o) { return add(o.negate()); }

    public BigInteger multiply(BigInteger o) {
        int sign = signum * o.signum;
        if (sign == 0) return valueOf(0);
        if (wide || o.wide) return wide(sign);
        int[] r = new int[N];
        boolean over = false;
        for (int i = 0; i < N; i++) {
            long carry = 0;
            for (int j = 0; j < N; j++) {
                long t = (mag[i] & 0xFFFFFFFFL) * (o.mag[j] & 0xFFFFFFFFL) + carry;
                if (i + j < N) {
                    t += r[i + j] & 0xFFFFFFFFL;
                    r[i + j] = (int) t;
                } else if ((int) t != 0) {
                    over = true;
                }
                carry = t >>> 32;
            }
            if (carry != 0) over = true;
        }
        return over ? wide(sign) : of(sign, r);
    }

    static BigInteger any() {
        int s = CProver.nondetInt();
        CProver.assume(s >= -1 && s <= 1);
        boolean w = CProver.nondetBoolean();
        int[] m = new int[N];
        for (int i = 0; i < N; i++) m[i] = w ? 0 : CProver.nondetInt();
        BigInteger r = new BigInteger(s, m, w);
        if (w) CProver.assume(s != 0);
        else CProver.assume(of(s, m).signum == s);
        return r;
    }

    public int compareTo(BigInteger o) {
        if (!wide && !o.wide) {
            if (signum != o.signum) return signum < o.signum ? -1 : 1;
            int c = compareMag(mag, o.mag);
            return signum < 0 ? -c : c;
        }
        if (wide && !o.wide) return signum;
        if (!wide && o.wide) return -o.signum;
        if (signum != o.signum) return signum < o.signum ? -1 : 1;
        int r = CProver.nondetInt();
        CProver.assume(r >= -1 && r <= 1);
        return r;
    }

    public boolean equals(Object x) {
        if (!(x instanceof BigInteger)) return false;
        BigInteger o = (BigInteger) x;
        if (!wide && !o.wide) return signum == o.signum && compareMag(mag, o.mag) == 0;
        if (wide != o.wide || signum != o.signum) return false;
        return CProver.nondetBoolean();
    }

    public int hashCode() { return CProver.nondetInt(); }

    public String toString() {
        if (wide) return CProver.nondetWithoutNull("");
        if (fitsLong()) return Long.toString(longValue());
        int[] m = new int[N];
        for (int i = 0; i < N; i++) m[i] = mag[i];
        // chunks of nine digits: 32 N bits have at most 9.64 N / 9 + 1
        String s = "";
        boolean more = true;
        for (int k = 0; k < (N * 32 * 3 / 10) / 9 + 2; k++) {
            if (!more) break;
            long rem = 0;
            more = false;
            for (int i = N - 1; i >= 0; i--) {
                long cur = (rem << 32) | (m[i] & 0xFFFFFFFFL);
                m[i] = (int) (cur / 1000000000L);
                rem = cur % 1000000000L;
                if (m[i] != 0) more = true;
            }
            String c = Long.toString(rem);
            s = more ? "000000000".substring(c.length()) + c + s : c + s;
        }
        return signum < 0 ? "-" + s : s;
    }

    public long longValue() {
        if (wide) return CProver.nondetLong();
        long v = (mag[0] & 0xFFFFFFFFL) | ((long) mag[1] << 32);
        return signum < 0 ? -v : v;
    }
    public int intValue() { return (int) longValue(); }
    public float floatValue() { return (float) doubleValue(); }
    public double doubleValue() { return fitsLong() ? (double) longValue() : CProver.nondetDouble(); }
}
