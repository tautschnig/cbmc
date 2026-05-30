import java.util.HashMap;

public class JmlMapLibrary {
    // Both JML clauses use Map library calls.
    // The recogniser inlines them to direct reads of the
    // axiomatic _sz array.

    //@ requires m != null;
    //@ requires m.size() >= 0;
    //@ ensures \result == m.size();
    public static int sizeOf(HashMap<Integer, Integer> m) {
        return m.size();
    }

    //@ requires m != null;
    //@ requires m.isEmpty();
    //@ ensures \result == 0;
    public static int sizeOfEmpty(HashMap<Integer, Integer> m) {
        return m.size();
    }

    public static int useIt() {
        HashMap<Integer, Integer> empty = new HashMap<>();
        // empty is freshly allocated; the axiomatic-collections
        // <init> sets _sz[empty] = 0, so empty.isEmpty() holds
        // at this call site.
        return sizeOfEmpty(empty);
    }
}
