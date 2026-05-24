public final class JmlThisField {

    private int counter;

    //@ requires this.counter >= 0;
    //@ ensures this.counter == \old(this.counter) + 1;
    void bump() {
        this.counter = this.counter + 1;
    }
}
