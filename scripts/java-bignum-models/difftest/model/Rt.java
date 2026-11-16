package model;
/** CProverRuntime.mayRunOn of the running JVM. */
public final class Rt {
  static final int V = Integer.parseInt(System.getProperty("java.specification.version").replace("1.", ""));
  public static boolean mayRunOn(int first, int last) { return V >= first && (last == 0 || V <= last); }
}
