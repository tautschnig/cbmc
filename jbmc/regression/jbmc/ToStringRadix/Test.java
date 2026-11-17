public class Test {
  public static void radix() {
    assert Integer.toString(255, 99).equals("255");
    assert Integer.toString(255, 1).equals("255");
    assert Long.toString(-255L, 37).equals("-255");
    assert Integer.toUnsignedString(-1, 99).equals("4294967295");
  }
  public static void main(String[] a) {
    radix();
    System.out.println("ok");
  }
}
