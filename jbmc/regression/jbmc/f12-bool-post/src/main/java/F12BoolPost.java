import static org.strata.jverify.JVerify.*;

public final class F12BoolPost {

    static boolean leaf(int x) {
        precondition(x >= 0);
        // Boolean-return postcondition with stack-temp diamond
        // body. Pre-fix, this was silently dropped at modular
        // capture (target_returns.size() == 1, RHS is
        // cast($stack_tmp2, c_bool[8]); $stack_tmp2 has two
        // definitions guarded by an IF; resolve_stack_temps's
        // find_definition bailed on crossed_branch).
        postcondition((boolean ret) -> ret == (x > 0));
        return x > 0;
    }

    static int caller(int x) {
        precondition(x >= 1);
        boolean r = leaf(x);
        check(r);
        return x;
    }
}
