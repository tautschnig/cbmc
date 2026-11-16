public class Test {
  static String charAtMessage() {
    try {
      new StringBuilder("ab").charAt(5);
    } catch (StringIndexOutOfBoundsException e) {
      return e.getMessage();
    }
    return "no exception";
  }
  // charAt out of range throws (it returned any character)
  public static void charAtThrows() {
    assert !charAtMessage().equals("no exception");
  }
  // the message of each version
  public static void charAtAny() {
    String m = charAtMessage();
    assert m.equals("String index out of range: 5") || m.equals("index 5,length 2")
        || m.equals("index 5, length 2")
        || m.equals("Index 5 out of bounds for length 2");
  }
  public static void charAtNot8() {
    assert charAtMessage().equals("String index out of range: 5");
  }
  public static void charAt8() {
    assert charAtMessage().equals("String index out of range: 5");
  }
  public static void charAt11() {
    assert charAtMessage().equals("index 5,length 2");
  }
  public static void charAt17() {
    assert charAtMessage().equals("index 5, length 2");
  }
  public static void charAt21() {
    assert charAtMessage().equals("Index 5 out of bounds for length 2");
  }
  public static void substringThrows() {
    try {
      new StringBuffer("abc").substring(2, 1);
      assert false;
    } catch (StringIndexOutOfBoundsException e) {
    }
  }
  // StringBuffer.append(boolean), replace and reverse changed nothing
  public static void bufferValues() {
    StringBuffer b = new StringBuffer("a");
    b.append(true).append(7L);
    assert b.toString().equals("atrue7");
    b.replace(1, 5, "XY");
    assert b.toString().equals("aXY7");
    assert b.reverse().toString().equals("7YXa");
  }
  public static void reverseSurrogates() {
    StringBuilder b = new StringBuilder("a\ud83d\ude00b");
    assert b.reverse().toString().equals("b\ud83d\ude00a");
  }
  public static void insertObject() {
    StringBuilder b = new StringBuilder("ab");
    b.insert(1, (Object) Integer.valueOf(42));
    assert b.toString().equals("a42b");
  }
  public static void negativeCapacity() {
    try {
      new StringBuilder(-4);
      assert false;
    } catch (NegativeArraySizeException e) {
    }
  }
  public static void invalidCodePoint() {
    try {
      new StringBuilder().appendCodePoint(-1);
      assert false;
    } catch (IllegalArgumentException e) {
      assert e.getMessage() == null
          || e.getMessage().equals("Not a valid Unicode code point: 0xFFFFFFFF");
    }
  }
  // (Character, as used above) negative ints and those beyond the last code
  // point are not valid code points
  public static void codePoints() {
    assert !Character.isValidCodePoint(-1);
    assert !Character.isSupplementaryCodePoint(0x110000);
    assert Character.isSupplementaryCodePoint(0x10000);
  }
}
