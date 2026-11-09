public class Main {

  public void constantIndexOf(String arg) {
    String s1 = "abcabc";
    // An empty needle is found at the end of the string, any other is not.
    if(arg != null && !arg.isEmpty())
      assert s1.indexOf(arg, 10) == -1;
  }
}
