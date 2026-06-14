import static org.strata.jverify.JVerify.*;

// Mirrors f12-modular but declares the callee's write frame with the
// pre-existing JVerify `modifies` clause instead of `assigns`. JBMC's
// java_bytecode_contracts pass maps `modifies(target)` onto the same
// c_assigns write-frame, unwrapping autoboxing so a primitive field
// (`modifies(counter)`) resolves to the field lvalue, enabling modular
// substitution at the call site.
public final class F12ModifiesTest {

  static int counter = 0;

  static int callee(int x) {
    precondition(x >= 0);
    precondition(x < 1000);
    postcondition(true);
    modifies(counter);
    counter = counter + 1;
    return x + 1;
  }

  static int caller() {
    precondition(true);
    int v = callee(7);
    assert v == v;
    return v;
  }
}
