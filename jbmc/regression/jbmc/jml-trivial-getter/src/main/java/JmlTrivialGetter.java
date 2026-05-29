public class JmlTrivialGetter {
    private final int x;
    private final boolean ready;

    public JmlTrivialGetter(int x, boolean ready) {
        this.x = x;
        this.ready = ready;
    }

    // Trivial getters: bytecode is `aload_0 ; getfield ; <return>`,
    // recognised by the JML resolver and rewritten to direct field
    // access at JML clause-evaluation time.
    public int getX()      { return this.x; }
    public boolean isReady() { return this.ready; }

    //@ requires this.getX() >= 0;
    //@ requires this.isReady();
    //@ ensures \result == this.getX();
    public int echo() {
        return this.x;
    }
}
