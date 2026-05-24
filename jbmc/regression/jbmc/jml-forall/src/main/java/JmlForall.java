public final class JmlForall {

    //@ requires n >= 0 && n <= 10;
    //@ requires (\forall int i; 0 <= i && i < n; i + n >= 0);
    //@ ensures \result == n;
    static int identity(int n) {
        return n;
    }
}
