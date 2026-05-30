import java.util.HashMap;

public class JmlMapStringKey {
    //@ requires m != null;
    //@ requires k != null;
    //@ requires m.containsKey(k);
    //@ ensures \result != null;
    public static Object getExisting(HashMap<String, Object> m, String k) {
        return m.get(k);
    }

    public static Object useIt() {
        HashMap<String, Object> m = new HashMap<>();
        String k = "the-key";
        m.put(k, "value");
        return getExisting(m, k);
    }
}
