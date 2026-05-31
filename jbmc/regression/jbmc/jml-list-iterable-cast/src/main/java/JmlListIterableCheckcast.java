import java.util.ArrayList;
import java.util.List;
import java.lang.Iterable;

// Minimum test for the JDK-known-subtype hierarchy fix.
// Without the fix, the (Iterable) checkcast on a List value
// fails because java.util.List's loaded stub doesn't declare
// Iterable as a parent.
public class JmlListIterableCheckcast {
    public static Iterable<Object> asIterable(List<Object> l) {
        return l;       // implicit (Iterable) checkcast
    }

    public static Iterable<Object> useIt() {
        ArrayList<Object> l = new ArrayList<>();
        return asIterable(l);
    }
}
