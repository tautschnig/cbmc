public class JmlAssignList {
    public int x;
    public int y;

    //@ assignable this.x;
    public void touchX() {
        this.x = 42;            // OK — listed
    }

    //@ assignable this.x;
    public void touchYBad() {
        this.y = 42;            // FAIL — not listed
    }
}
