package org.cprover;

// a class of the library (org.cprover), whose counted loops JBMC unwinds
// fully whatever --unwind says
public class LibraryLoop {
  public static int sum() {
    int s = 0;
    for (int i = 0; i < 50; i++) {
      s += i;
    }
    return s;
  }

  public static int sumDown() {
    int s = 0;
    for (int i = 40; i >= 0; i -= 2) {
      s += i;
    }
    return s;
  }

  // not counted: the bound is an argument
  public static int sumTo(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
      s += i;
    }
    return s;
  }
}
