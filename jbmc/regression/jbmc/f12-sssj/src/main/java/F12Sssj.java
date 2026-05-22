import static org.strata.jverify.JVerify.*;
import java.util.ArrayList;
import java.util.Map;
import org.cprover.CProverMapEntry;

/**
 * Regression test for the full SSSJ verification shape:
 * ArrayList + iterator + sealed-pattern-match + reduce.
 */
public final class F12Sssj {

    sealed interface Constraint permits Regular, Prefix, Suffix {}
    record Regular(int lb, int total) implements Constraint {}
    record Prefix(int len) implements Constraint {}
    record Suffix(int len) implements Constraint {}

    sealed interface Atom permits PredAtom, EqAtom {}
    record PredAtom(int id, Constraint c) implements Atom {}
    record EqAtom(int id, int bound) implements Atom {}

    static int atomBound(Atom atom) {
        precondition(atom != null);
        return switch (atom) {
            case PredAtom p -> {
                assume(p.c() != null);
                yield switch (p.c()) {
                    case Regular r -> r.lb() >= 0 ? r.lb() : r.total();
                    case Prefix pf -> pf.len();
                    case Suffix sf -> sf.len();
                };
            }
            case EqAtom eq -> eq.bound();
        };
    }

    static int calcMin(ArrayList<Map.Entry<Atom, Boolean>> atoms) {
        precondition(atoms != null);
        precondition(atoms.size() < 4);
        int min = Integer.MAX_VALUE;
        for (Map.Entry<Atom, Boolean> entry : atoms) {
            assume(entry != null);
            Atom atom = entry.getKey();
            assume(atom != null);
            if (atom instanceof PredAtom p) {
                assume(p.c() != null);
            }
            int b = atomBound(atom);
            if (b >= 0) min = Math.min(min, b);
        }
        return min == Integer.MAX_VALUE ? 1 : min;
    }

    static int driver(int a, int b, int c) {
        precondition(a >= 0 && a < 100);
        precondition(b >= 0 && b < 100);
        precondition(c >= 0 && c < 100);

        ArrayList<Map.Entry<Atom, Boolean>> atoms = new ArrayList<>();
        atoms.add(new CProverMapEntry<>(new PredAtom(1, new Regular(a, a+10)), Boolean.TRUE));
        atoms.add(new CProverMapEntry<>(new PredAtom(2, new Prefix(b)), Boolean.TRUE));
        atoms.add(new CProverMapEntry<>(new PredAtom(3, new Suffix(c)), Boolean.TRUE));

        int result = calcMin(atoms);
        int expected = Math.min(a, Math.min(b, c));
        check(result == expected);
        return result;
    }
}
