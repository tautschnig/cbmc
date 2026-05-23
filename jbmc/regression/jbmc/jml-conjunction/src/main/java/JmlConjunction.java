public final class JmlConjunction {

    //@ requires x >= 0 && x <= 100;
    //@ requires y >= 0 && y <= 100;
    //@ ensures (x > 0 && y > 0) ==> \result > 0;
    //@ ensures (x == 0 || y == 0) ==> \result == 0;
    static int product(int x, int y) {
        return x * y;
    }
}
