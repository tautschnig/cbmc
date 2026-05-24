public final class JmlBasic {

    //@ requires x >= 0;
    //@ ensures \result >= x;
    static int identity(int x) {
        return x;
    }
}
