public class Q {
  public static void posAll(int x, int y) { if (x >= 0 && y >= 0 && ("" + x).equals("" + y)) assert x == y; }
  public static void negAll(int x, int y) { if (x < 0 && y < 0 && ("" + x).equals("" + y)) assert x == y; }
  public static void pos6(int x, int y) { if (x >= 0 && y >= 0 && x < 1000000 && y < 1000000 && ("" + x).equals("" + y)) assert x == y; }
  public static void pos9(int x, int y) { if (x >= 0 && y >= 0 && x < 1000000000 && y < 1000000000 && ("" + x).equals("" + y)) assert x == y; }
  public static void long6(long x, long y) { if (x >= 0 && y >= 0 && x < 1000000 && y < 1000000 && ("" + x).equals("" + y)) assert x == y; }
  public static void prefix(int x, int y) { if (x >= 0 && y >= 0 && ("" + x).equals("1" + y)) assert x != y; }
}
