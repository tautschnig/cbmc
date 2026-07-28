import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class PatternMatcher
{
  // All assertions verified against a real JVM (java -ea).
  public static void queries()
  {
    Pattern p = Pattern.compile("[a-z]+[0-9]+");
    assert p.matcher("ab12").matches();
    assert !p.matcher("12ab").matches();
    // lookingAt: anchored at the start only (JLS).
    Matcher m = p.matcher("ab12xyz");
    assert m.lookingAt();
    assert !m.matches();
    // find: unanchored; matches()/lookingAt() stay precise after find()
    // (position-independent per the JLS).
    Matcher m2 = p.matcher("__ab12__");
    assert m2.find();
    assert !m2.lookingAt();
    // static form + reset(CharSequence) restores precision.
    assert Pattern.matches("a.c", "abc");
    assert m2.reset("zz9").find();
  }

  public static void secondFind()
  {
    // JLS: successive find() calls advance through the input; the position
    // is not modelled, so a SECOND find() is a sound nondet (this true
    // property must stay unproven -- and so must its negation).
    Pattern p = Pattern.compile("[0-9]");
    Matcher m = p.matcher("1a2");
    assert m.find();
    assert m.find();
  }
}
