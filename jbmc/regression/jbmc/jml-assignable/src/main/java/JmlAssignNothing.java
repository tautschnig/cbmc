public class JmlAssignNothing {
    public int x;

    //@ assignable \nothing;
    public int readOnly() {
        return this.x;          // read; assignable should pass
    }

    //@ assignable \nothing;
    public void mutateBad() {
        this.x = 42;            // write; assignable should FAIL
    }
}
