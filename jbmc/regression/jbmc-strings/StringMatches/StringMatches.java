public class StringMatches
{
  // Constant subject + pattern: decided exactly (SAT constant-fold or SMT
  // str.in_re).
  public static void constant()
  {
    String s = "ab12";
    assert s.matches("[a-z]+[0-9]+");
    assert !s.matches("[0-9]+");
  }

  // Java-only construct (possessive quantifier): must be a sound nondet --
  // the (true-in-Java) property is not proved, and neither is its negation.
  public static void possessive()
  {
    String s = "aaa";
    assert s.matches("a*+");
  }

  // Java-dialect semantics (java.util.regex, differing from Python's re):
  // '.' excludes \r; \s excludes \u001C-\u001F.
  public static void dialect()
  {
    String s = "a\rb";
    assert !s.matches("a.b");
    String w = "\u001C";
    assert !w.matches("\\s");
    assert " ".matches("\\s");
  }

  // Symbolic subject constrained by the regex (SMT2 back-ends only): the
  // regex [0-9]+ implies nonemptiness.
  public static void symbolic(String s)
  {
    if(s != null && s.matches("[0-9]+"))
    {
      assert s.length() > 0;
    }
  }
}
