public class StringStrip
{
  public static void main(String[] args)
  {
    String s = "\u001C a \u001C";
    // Java 11 String.strip family: removes Character.isWhitespace characters
    // (ASCII range: 0x09..0x0d, 0x1c..0x1f, 0x20) from the given end(s).
    assert s.strip().equals("a");
    assert s.stripLeading().equals("a \u001C");
    assert s.stripTrailing().equals("\u001C a");
  }

  public static void stripKeeps()
  {
    // \u001B (ESC) is NOT Character.isWhitespace, so strip keeps it --
    // but trim (c <= 0x20) removes it. This pins the semantic difference.
    String t = "\u001Ba\u001B";
    assert t.strip().equals("\u001Ba\u001B");
    assert t.trim().equals("a");
  }
}
