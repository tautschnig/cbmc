import org.cprover.LibraryLoop;

public class Test {
  public static void library() { assert LibraryLoop.sum() == 1225 && LibraryLoop.sumDown() == 420; }
  public static void libraryBound() { assert LibraryLoop.sumTo(50) == 1225; }
  public static void user() { int s = 0; for (int i = 0; i < 50; i++) s += i; assert s == 1225; }
}
