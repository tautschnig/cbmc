package org.strata.jverify;

public final class JVerify {
  private JVerify() {}

  public static void precondition(boolean c) {}
  public static void postcondition(boolean c) {}
  public static void assigns(Object... targets) {}
  public static void check(boolean c) {}
  public static void assume(boolean c) {}

  // old() overloads — stub bodies throw so they're never
  // executed at runtime; JBMC's contract front-end rewrites
  // them to history_exprt before symex.
  public static int old(int value) {
    throw new ContractException();
  }
  public static <T> T old(T value) {
    throw new ContractException();
  }

  // Lambda-form postcondition overloads.
  public interface IntPredicate { boolean test(int ret); }
  public interface BooleanPredicate { boolean test(boolean ret); }

  public static void postcondition(IntPredicate p) {}
  public static void postcondition(BooleanPredicate p) {}
}

class ContractException extends RuntimeException {}
