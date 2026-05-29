public class JmlSignalsTyped {
    //@ requires x >= 0;
    //@ ensures \result == x * 2;
    //@ signals_only BadInputException;
    //@ signals (BadInputException e) e.getBadValue() == x;
    public static int doubleOrThrow(int x) {
        if(x > 1000) throw new BadInputException(x);
        return x * 2;
    }
}
