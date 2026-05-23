public final class JmlResult {

    //@ requires lo <= hi;
    //@ ensures \result >= lo;
    //@ ensures \result <= hi;
    static int clampLow(int x, int lo, int hi) {
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }
}
