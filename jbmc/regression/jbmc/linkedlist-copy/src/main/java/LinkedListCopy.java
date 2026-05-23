import java.util.LinkedList;

public class LinkedListCopy {

    public static int testCopyCtor() {
        LinkedList<Integer> src = new LinkedList<>();
        src.add(10);
        src.add(20);
        src.add(30);
        assert src.size() == 3;

        // Pre-fix: LinkedList(Collection c) ignored c and
        // produced a list with nondet size. After the fix, the
        // new list preserves size and elements when source is a
        // LinkedList (parallel-array model).
        LinkedList<Integer> dst = new LinkedList<>(src);
        assert dst.size() == 3;

        Integer e0 = dst.get(0);
        assert e0 != null && e0 == 10;
        Integer e1 = dst.get(1);
        assert e1 != null && e1 == 20;
        Integer e2 = dst.get(2);
        assert e2 != null && e2 == 30;

        return dst.size();
    }
}
