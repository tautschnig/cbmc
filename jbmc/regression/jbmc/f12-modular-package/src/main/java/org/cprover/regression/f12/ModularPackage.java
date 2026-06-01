package org.cprover.regression.f12;

import static org.strata.jverify.JVerify.*;

/**
 * Modular contract substitution test where the annotated
 * callee's JVM signature contains '/' (from
 * `Lorg/cprover/regression/f12/Box;`).
 *
 * Before the dfcc.cpp fix, JBMC's
 * `parse_function_contract_pair` would split the callee's
 * symbol ID on '/' and reject it as
 *   "Invalid function-contract mapping
 *    Reason: couldn't parse '<symbol id>'"
 * aborting the run with EXIT=6 before any verification
 * happened.
 *
 * With the fix (the new pre-parsed `dfcc()` overload that
 * the Java integration calls), the symbol IDs flow into
 * DFCC's transformation pipeline opaquely and modular
 * contract substitution succeeds.
 */
public final class ModularPackage {

  static int counter = 0;

  static int callee(Box box) {
    precondition(box != null);
    precondition(box.value >= 0);
    precondition(box.value < 1000);
    postcondition(true);
    assigns(counter);
    counter = counter + 1;
    return box.value + 1;
  }

  static int caller() {
    precondition(true);
    Box b = new Box(7);
    int v = callee(b);
    assert v == v;
    return v;
  }
}
