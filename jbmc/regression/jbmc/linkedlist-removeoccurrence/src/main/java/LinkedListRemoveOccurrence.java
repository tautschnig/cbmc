import java.util.LinkedList;
import java.util.Iterator;

public class LinkedListRemoveOccurrence {

    public static int testRemoveFirstOccurrence() {
        LinkedList<Integer> list = new LinkedList<>();
        list.add(10);
        list.add(20);
        list.add(30);
        assert list.size() == 3;

        boolean found = list.removeFirstOccurrence(20);
        assert found;
        assert list.size() == 2;
        Integer e0 = list.get(0);
        Integer e1 = list.get(1);
        assert e0 != null && e0 == 10;
        assert e1 != null && e1 == 30;

        boolean notFound = list.removeFirstOccurrence(999);
        assert !notFound;
        assert list.size() == 2;

        return list.size();
    }

    public static int testDescendingIterator() {
        LinkedList<Integer> list = new LinkedList<>();
        list.add(10);
        list.add(20);
        list.add(30);

        Iterator<Integer> rev = list.descendingIterator();
        assert rev.hasNext();
        Integer first = rev.next();
        assert first != null && first == 30;
        Integer second = rev.next();
        assert second != null && second == 20;
        Integer third = rev.next();
        assert third != null && third == 10;
        assert !rev.hasNext();

        // Source list should be unchanged.
        assert list.size() == 3;

        return list.size();
    }
}
