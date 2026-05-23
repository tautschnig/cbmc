import java.util.LinkedList;

public class LinkedListBasic {

    public static int testBasicOps() {
        LinkedList<Integer> list = new LinkedList<>();
        assert list.size() == 0;
        assert list.isEmpty();

        list.add(10);
        list.add(20);
        list.add(30);
        assert list.size() == 3;
        assert !list.isEmpty();

        Integer first = list.getFirst();
        assert first != null && first == 10;

        Integer last = list.getLast();
        assert last != null && last == 30;

        Integer mid = list.get(1);
        assert mid != null && mid == 20;

        return list.size();
    }
}
