import java.util.LinkedHashMap;

public class LinkedHashMapCopy {

    public static int testCopyCtor() {
        LinkedHashMap<Integer, Integer> src = new LinkedHashMap<>();
        src.put(1, 100);
        src.put(2, 200);
        src.put(3, 300);
        assert src.size() == 3;

        // The copy constructor used to silently drop contents
        // (silent semantic regression). After the fix, the new
        // map preserves size and elements.
        LinkedHashMap<Integer, Integer> dst = new LinkedHashMap<>(src);
        assert dst.size() == 3;

        Integer v1 = dst.get(1);
        assert v1 != null && v1 == 100;
        Integer v2 = dst.get(2);
        assert v2 != null && v2 == 200;
        Integer v3 = dst.get(3);
        assert v3 != null && v3 == 300;

        return dst.size();
    }
}
