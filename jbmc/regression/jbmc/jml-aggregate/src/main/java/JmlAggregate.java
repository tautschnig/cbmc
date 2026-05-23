public final class JmlAggregate {

    //@ requires n == 10;
    //@ ensures \result == (\sum int i; 0 <= i && i < 5; i);
    static int sumLT(int n) {
        return 10;
    }

    //@ requires n == 10;
    //@ ensures \result == (\sum int i; i >= 0 && i <= 4; i);
    static int sumGE(int n) {
        return 10;
    }

    //@ requires n == 5;
    //@ ensures \result == (\sum int i; 0 <= i && i < 5; 1);
    static int countOnes(int n) {
        return 5;
    }
}
