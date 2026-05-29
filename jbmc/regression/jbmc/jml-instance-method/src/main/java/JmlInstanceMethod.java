public final class JmlInstanceMethod {

    //@ requires lo <= hi;
    //@ ensures \result >= lo && \result <= hi;
    //@ ensures (x >= lo && x <= hi) ==> \result == x;
    public int clamp(int x, int lo, int hi) {
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }
}
