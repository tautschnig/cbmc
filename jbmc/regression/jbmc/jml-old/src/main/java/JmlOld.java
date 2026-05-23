public final class JmlOld {

    //@ requires x >= 0;
    //@ ensures \result == \old(x) + 1;
    static int increment(int x) {
        return x + 1;
    }
}
