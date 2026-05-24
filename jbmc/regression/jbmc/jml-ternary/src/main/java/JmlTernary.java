public final class JmlTernary {

    //@ requires x >= 0;
    //@ ensures \result == (x > 10 ? x : 10);
    static int clampLow10(int x) {
        return x > 10 ? x : 10;
    }
}
