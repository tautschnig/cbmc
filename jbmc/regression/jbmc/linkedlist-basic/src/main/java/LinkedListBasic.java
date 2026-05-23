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

        // addFirst was previously broken because the model used
        // System.arraycopy with overlapping ranges; replaced with
        // element-wise shift in REVIEW-PRE-PUSH medium item 4.
        list.addFirst(5);
        assert list.size() == 4;
        Integer newFirst = list.getFirst();
        assert newFirst != null && newFirst == 5;
        Integer secondNow = list.get(1);
        assert secondNow != null && secondNow == 10;

        // removeFirst was similarly broken; verify it now works.
        Integer removed = list.removeFirst();
        assert removed != null && removed == 5;
        assert list.size() == 3;
        Integer headAfterRemove = list.getFirst();
        assert headAfterRemove != null && headAfterRemove == 10;

        return list.size();
    }
}
