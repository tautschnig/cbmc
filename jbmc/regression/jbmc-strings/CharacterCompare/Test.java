public class Test {
  // Character.compare(x, y) is x - y (it was its sign)
  public static void difference() {
    assert Character.compare((char) 30561, (char) 931) == 29630;
  }
  public static void differenceBad() {
    assert Character.compare((char) 30561, (char) 931) == 1;
  }
  public static void symbolic(char x, char y) {
    int r = Character.compare(x, y);
    assert r == x - y;
  }
}
