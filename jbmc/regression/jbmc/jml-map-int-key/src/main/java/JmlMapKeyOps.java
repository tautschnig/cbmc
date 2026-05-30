import java.util.HashMap;

public class JmlMapKeyOps {
    //@ requires m != null;
    //@ requires m.containsKey(k);
    //@ ensures \result != null;
    public static Object getExisting(HashMap<Integer, Object> m, int k) {
        return m.get(k);
    }

    public static Object useIt() {
        HashMap<Integer, Object> m = new HashMap<>();
        m.put(42, "hello");
        return getExisting(m, 42);
    }
}
