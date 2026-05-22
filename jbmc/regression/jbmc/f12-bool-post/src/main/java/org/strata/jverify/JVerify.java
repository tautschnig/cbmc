package org.strata.jverify;

public final class JVerify {
  private JVerify() {}

  public static void precondition(boolean c) {}
  public static void postcondition(boolean c) {}
  public static void assigns(Object... targets) {}
  public static void check(boolean c) {}

  // Lambda-form postcondition that binds the return value. The
  // postcondition lambda is captured by JBMC's contract front-end
  // and substituted at call sites under --modular.
  public interface IntPredicate { boolean test(int ret); }
  public interface BooleanPredicate { boolean test(boolean ret); }
  public interface LongPredicate { boolean test(long ret); }

  public static void postcondition(IntPredicate p) {}
  public static void postcondition(BooleanPredicate p) {}
  public static void postcondition(LongPredicate p) {}
}
