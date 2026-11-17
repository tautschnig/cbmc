public class Test {
  public static void latin() {
    assert Character.digit('1', 10) == 1;
    assert Character.digit('x', 10) == -1;
    assert Character.digit('f', 16) == 15;
    assert Character.digit('F', 16) == 15;
    assert Character.digit('9', 8) == -1;
    assert Character.digit('z', 36) == 35;
    assert Character.digit('1', 1) == -1;
    assert Character.digit('1', 37) == -1;
    assert Character.digit('\uff15', 10) == 5;
    assert Character.digit('\uff41', 16) == 10;
    assert Character.digit((int) 'B', 12) == 11;
  }
  public static void otherDigit() {
    int d = Character.digit('\u0663', 10);
    assert d >= -1 && d <= 9;
  }
  // ARABIC-INDIC DIGIT THREE is 3 (JBMC proved it was not)
  public static void otherDigitMay3() {
    assert Character.digit('\u0663', 10) != 3;
  }
  public static void forDigit() {
    assert Character.forDigit(-1, 7) == '\0';
    assert Character.forDigit(10, 10) == '\0';
    assert Character.forDigit(11, 16) == 'b';
    assert Character.forDigit(5, 37) == '\0';
  }
  public static void roundTrip(int d, int r) {
    char c = Character.forDigit(d, r);
    if (r >= 2 && r <= 36 && d >= 0 && d < r)
      assert Character.digit(c, r) == d;
    else
      assert c == 0;
  }
  public static void main(String[] a) {
    latin(); otherDigit(); forDigit(); roundTrip(5, 8); roundTrip(9, 8);
    System.out.println("ok " + Character.digit('\u0663', 10));
  }
}
