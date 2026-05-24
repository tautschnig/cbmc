public final class JmlExists {

    //@ requires n >= 1 && n <= 10;
    //@ ensures (\exists int i; 0 <= i && i < n; i == 0);
    static int hasZero(int n) {
        return n;
    }
}
