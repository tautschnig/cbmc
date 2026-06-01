package org.cprover.regression.f12;

/**
 * Reference type used as a parameter to expose '/' inside the
 * JVM type descriptor of {@link ModularPackage#callee(Box)}.
 * The annotated method's symbol ID becomes
 *   `java::org.cprover.regression.f12.ModularPackage.callee:(Lorg/cprover/regression/f12/Box;)I`
 * which contains four '/' characters in
 * `Lorg/cprover/regression/f12/Box;` — the exact shape that
 * tripped parse_function_contract_pair.
 */
public final class Box {
  int value;

  Box(int v) {
    this.value = v;
  }
}
