public class BadInputException extends RuntimeException {
    private final int badValue;
    public BadInputException(int v) { super("bad: " + v); this.badValue = v; }
    public int getBadValue() { return this.badValue; }
}
