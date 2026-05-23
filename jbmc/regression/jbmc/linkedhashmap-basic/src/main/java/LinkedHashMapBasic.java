import java.util.LinkedHashMap;
import java.util.Map;

public class LinkedHashMapBasic {

    public static int testBasicOps() {
        LinkedHashMap<Integer, Integer> m = new LinkedHashMap<>();
        assert m.size() == 0;
        assert m.isEmpty();

        m.put(1, 100);
        m.put(2, 200);
        assert m.size() == 2;
        assert !m.isEmpty();

        Integer v = m.get(1);
        assert v != null && v == 100;

        boolean has = m.containsKey(2);
        assert has;

        return m.size();
    }
}
