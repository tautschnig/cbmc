class JmlInheritedFieldBase {
    protected int counter;
}

public final class JmlInheritedField extends JmlInheritedFieldBase {

    //@ requires counter >= 0;
    //@ ensures counter == \old(counter) + 1;
    void bump() {
        counter = counter + 1;
    }
}
