import java.util.MissingFormatArgumentException;

public class Test {
  public static void missing(int i) {
    try { String.format("%s %s", i); assert false; }
    catch(MissingFormatArgumentException e) { }
  }
  public static void missingIndexed(int i) {
    try { String.format("%2$s", i); assert false; }
    catch(MissingFormatArgumentException e) { }
  }
  public static void missingRelative(int i) {
    try { String.format("%<s", i); assert false; }
    catch(MissingFormatArgumentException e) { }
  }
  public static void enough(int i) {
    if(i != 1) return;
    assert String.format("%d %<d", i).equals("1 1");
    assert String.format("%d", i, i).equals("1");
    assert String.format("%s %<s", "a").equals("a a");
    assert String.format("%%%n").length() == 2;
  }
  public static void catchesAsIllegalArgument(int i) {
    try { String.format("%d %d", i); assert false; }
    catch(IllegalArgumentException e) { }
  }
  public static void symbolicFormat(String f) {
    if(f == null) return;
    // the format string may use more arguments than given
    String.format(f, 1);
  }
}
