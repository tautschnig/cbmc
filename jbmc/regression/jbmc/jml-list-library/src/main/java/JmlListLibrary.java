import java.util.ArrayList;

public class JmlListLibrary {
    //@ requires l != null;
    //@ ensures \result == l.size();
    public static int sizeOf(ArrayList<Object> l) {
        return l.size();
    }

    //@ requires l != null;
    //@ requires l.isEmpty();
    //@ ensures \result == 0;
    public static int sizeOfEmpty(ArrayList<Object> l) {
        return l.size();
    }

    public static int useEmpty() {
        ArrayList<Object> l = new ArrayList<>();
        return sizeOfEmpty(l);
    }

    public static int useAdd() {
        ArrayList<Object> l = new ArrayList<>();
        l.add("hello");
        return l.size();   // expect 1
    }
}
