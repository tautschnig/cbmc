import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintStream;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Calls each method of an API list (see api.py) on generated arguments and
 * prints the result, so that the outputs of several JDKs can be compared
 * (compare.py). The arguments of a case depend only on the method and the
 * case number, never on the JDK: the generator is java.util.Random, whose
 * algorithm the specification fixes, seeded by String.hashCode (also fixed).
 *
 * Results are printed so that they don't depend on what is being compared:
 * doubles and floats as their bits (Double.toString changed in Java 19),
 * strings with escapes, exceptions as their class and message, identity
 * hashes masked.
 *
 * Usage: java Probe API_FILE CASES_PER_METHOD OUT_FILE [CLASS_PREFIX]
 * Compile with --release 8.
 */
public class Probe {
  static final String[] STRINGS = {
    "", "a", "ab", "abc", "aBc", "A", " a ", "\t", " ", "  x  ", "0", "1", "-1",
    "+1", "-0", "007", "123", "2147483647", "2147483648", "-2147483648",
    "9223372036854775807", "9223372036854775808", "0x1F", "#10", "0X7f",
    "1e5", "1.5", ".5", "5.", "1e", "1.0e-3", "NaN", "Infinity", "-Infinity",
    "0x1p3", "1_000", "1f", "1d", "true", "TRUE", "yes", "\u0663",
    "\uff11\uff12", "\u00e9", "\u00df", "\u0130", "\u0131", "i", "I",
    "\u01c5", "\ufb00", "\u1e9e", "\u03a3", "\u03c3\u03c2", "\ud83d\ude00",
    "\ud83d", "\ude00", "a\ud83d\ude00b", "\u2028", "\u00a0", "\u2007",
    "\u202f", "\u3000", "\u001c", "\u0085", "\u180e", "\u200b", "\ufeff",
    "\u0870", "\u0c5d", "\ua7c0", "\ud83e\udee8", "\u1f88", "\u0149",
    "\u0390", "x\u0307", "\u212a", "\u2126", "\u10d0", "\u1c90", "\u13a0",
    "\uab70", "a.b", "a,b,,", ",a", "a::b", "aaa", "abab", "a b  c", "\\",
    "$", "$1", "\\$", "${g}", "a$0b", "(", "[", "*", "\n", "\r\n", "\r",
  };

  static final String[] REGEX_ATOMS = {
    "a", "b", ".", "\\d", "\\D", "\\w", "\\W", "\\s", "\\S", "\\b", "\\B",
    "\\p{L}", "\\p{Lu}", "\\p{Ll}", "\\p{IsAlphabetic}", "\\p{javaLowerCase}",
    "\\p{Alpha}", "\\p{Punct}", "\\p{Space}", "\\p{InGreek}", "\\p{IsLatin}",
    "\\h", "\\v", "\\R", "\\X", "\\b{g}", "[a-c]", "[^a]", "[a&&[b]]",
    "[a-z&&[^b]]", "[\\w&&\\d]", "^", "$", "\\A", "\\z", "\\Z", "\\G",
    "(?i)", "(?u)", "(?U)", "(?i:a)", "(?<g>a)", "\\k<g>", "\\1", "(a)",
    "(?:ab)", "(?=a)", "(?!a)", "(?<=a)", "(?<!a)", "\\Q.\\E", "\\x41",
    "\\u00e9", "\\x{1F600}", "\\N{LATIN SMALL LETTER A}", "\\0101", "\\cA",
    "[[a]b]", "[a[b]]", "a|b", "|", "\\", "[", "(", "a{2}", "\\p{Foo}",
    "[\\p{L}&&[^a]]", "\u00e9", "\u0130", "[\u00e0-\u00ff]", "\\p{Cn}",
    "\\p{IsEmoji}", "\\p{javaWhitespace}", "\\p{Lt}", "[a-]", "[-a]",
  };

  static final String[] QUANTIFIERS = {
    "", "", "", "*", "+", "?", "*?", "+?", "??", "*+", "{0,1}", "{2,}", "{1}",
  };

  static final String[] REPLACEMENTS = {
    "", "x", "$0", "$1", "$2", "${g}", "\\$", "\\\\", "$", "\\", "a$0b",
    "$10", "${", "$g", "\\x",
  };

  static final int[] INTS = {
    0, 1, 2, 3, 4, 5, 7, 8, 10, 16, 36, 37, -1, -2, 100, 255, 256, 0x7f,
    0xff, 0xD800, 0xDBFF, 0xDC00, 0xDFFF, 0xFFFF, 0x10000, 0x1F600,
    0x10FFFF, 0x110000, Integer.MIN_VALUE, Integer.MAX_VALUE,
    Integer.MIN_VALUE + 1, Integer.MAX_VALUE - 1, 65, 97, 0x130, 0x131,
    0x3a3, 0x1c5, 0xdf, 0x660, 0xff10, 0x870, 0xa7c0, 0x1fae8, 0xe0001,
  };

  static final long[] LONGS = {
    0, 1, -1, 2, 10, 16, 36, 37, 255, 1L << 31, 1L << 32, -(1L << 31),
    Long.MIN_VALUE, Long.MAX_VALUE, Long.MIN_VALUE + 1, 1000000007L,
    Integer.MAX_VALUE, Integer.MIN_VALUE, 0xFFFFFFFFL,
  };

  static final double[] DOUBLES = {
    0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, -2.5, 0.1, 1e-3, 1e7, 1e21, 1e-7,
    123456789.0, 2e23, 5e-324, Double.MIN_NORMAL, Double.MAX_VALUE,
    Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY,
    Math.PI, Math.E, 0x1p52, 0x1p53 + 1, 1.0E23, 9.007199254740993E15,
    Long.MAX_VALUE, Integer.MAX_VALUE + 0.5, -0.49999999999999994,
    0.49999999999999994, 4.35, 2.0E-3, 1.0E-5,
  };

  static final Object[] LOCALES = {
    Locale.ROOT, Locale.ENGLISH, Locale.US, new Locale("tr"),
    new Locale("az"), new Locale("lt"), Locale.GERMAN, new Locale("el"),
    Locale.forLanguageTag("ar-EG"), Locale.forLanguageTag("hi-IN"),
    Locale.FRANCE, null,
  };

  final Random rng;
  /** whether the current method takes a regex as its first String */
  boolean regexFirst;
  /** whether the second String is a replacement */
  boolean replacementSecond;
  int stringIndex;

  Probe(long seed) {
    rng = new Random(seed);
  }

  char genChar() {
    switch (rng.nextInt(6)) {
      case 0:
        return (char) ('a' + rng.nextInt(26));
      case 1:
        return (char) (rng.nextInt(0x80));
      case 2:
        return (char) (rng.nextInt(0x10000));
      case 3:
        return (char) INTS[rng.nextInt(INTS.length)];
      default:
        String s = STRINGS[rng.nextInt(STRINGS.length)];
        return s.isEmpty() ? 'x' : s.charAt(rng.nextInt(s.length()));
    }
  }

  String genString() {
    switch (rng.nextInt(8)) {
      case 0:
      case 1:
      case 2:
        return STRINGS[rng.nextInt(STRINGS.length)];
      case 3: {
        StringBuilder b = new StringBuilder();
        int n = rng.nextInt(6);
        for (int i = 0; i < n; i++) b.append(genChar());
        return b.toString();
      }
      case 4:
        return STRINGS[rng.nextInt(STRINGS.length)]
            + STRINGS[rng.nextInt(STRINGS.length)];
      case 5:
        return Integer.toString(rng.nextInt(2000) - 1000, 2 + rng.nextInt(35));
      case 6: {
        // a decimal number (not by Double.toString, which changed in 19)
        String m = (rng.nextBoolean() ? "-" : "") + rng.nextInt(1000) + "."
            + rng.nextInt(100);
        return rng.nextBoolean() ? m : m + "E" + (rng.nextInt(40) - 20);
      }
      default:
        return rng.nextInt(4) == 0 ? null : "a" + genChar() + "b";
    }
  }

  String genRegex() {
    StringBuilder b = new StringBuilder();
    int n = 1 + rng.nextInt(3);
    for (int i = 0; i < n; i++) {
      if (rng.nextInt(6) == 0) b.append('(');
      b.append(REGEX_ATOMS[rng.nextInt(REGEX_ATOMS.length)]);
      b.append(QUANTIFIERS[rng.nextInt(QUANTIFIERS.length)]);
      if (b.indexOf("(") >= 0 && rng.nextInt(3) == 0) b.append(')');
    }
    return b.toString();
  }

  int genInt() {
    switch (rng.nextInt(5)) {
      case 0:
      case 1:
        return INTS[rng.nextInt(INTS.length)];
      case 2:
        return rng.nextInt(12) - 2;
      case 3:
        return rng.nextInt(0x30000);
      default:
        return rng.nextInt();
    }
  }

  long genLong() {
    switch (rng.nextInt(4)) {
      case 0:
      case 1:
        return LONGS[rng.nextInt(LONGS.length)];
      case 2:
        return rng.nextInt(40) - 5;
      default:
        return rng.nextLong();
    }
  }

  double genDouble() {
    switch (rng.nextInt(4)) {
      case 0:
      case 1:
        return DOUBLES[rng.nextInt(DOUBLES.length)];
      case 2:
        return (rng.nextInt(20001) - 10000) / Math.pow(10, rng.nextInt(8));
      default:
        return Double.longBitsToDouble(rng.nextLong());
    }
  }

  Object genObject() {
    switch (rng.nextInt(8)) {
      case 0:
        return null;
      case 1:
        return Integer.valueOf(genInt());
      case 2:
        return Character.valueOf(genChar());
      case 3:
        return Double.valueOf(genDouble());
      case 4:
        return Long.valueOf(genLong());
      case 5:
        return Boolean.valueOf(rng.nextBoolean());
      case 6:
        return new StringBuilder(String.valueOf(genString()));
      default:
        return genString();
    }
  }

  Object gen(Class<?> t) throws Exception {
    if (t == int.class) return genInt();
    if (t == long.class) return genLong();
    if (t == short.class) return (short) genInt();
    if (t == byte.class) return (byte) genInt();
    if (t == char.class) return genChar();
    if (t == boolean.class) return rng.nextBoolean();
    if (t == double.class) return genDouble();
    if (t == float.class) {
      return rng.nextInt(3) == 0 ? Float.intBitsToFloat(rng.nextInt())
                                 : (float) genDouble();
    }
    if (t == String.class) {
      int k = stringIndex++;
      if (regexFirst && k == 0) return genRegex();
      if (replacementSecond && k == 1) {
        return REPLACEMENTS[rng.nextInt(REPLACEMENTS.length)];
      }
      return genString();
    }
    if (t == CharSequence.class) {
      String s = genString();
      return s == null || rng.nextBoolean() ? s : new StringBuilder(s);
    }
    if (t == Object.class) return genObject();
    if (t == StringBuilder.class) {
      String s = genString();
      return s == null ? null : new StringBuilder(s);
    }
    if (t == StringBuffer.class) {
      String s = genString();
      return s == null ? null : new StringBuffer(s);
    }
    if (t == char[].class) {
      String s = genString();
      return s == null ? null : s.toCharArray();
    }
    if (t == int[].class) {
      int n = rng.nextInt(4);
      int[] a = new int[n];
      for (int i = 0; i < n; i++) a[i] = genInt();
      return a;
    }
    if (t == byte[].class) {
      int n = rng.nextInt(5);
      byte[] a = new byte[n];
      for (int i = 0; i < n; i++) a[i] = (byte) genInt();
      return rng.nextInt(10) == 0 ? null : a;
    }
    if (t == Object[].class) {
      int n = rng.nextInt(4);
      Object[] a = new Object[n];
      for (int i = 0; i < n; i++) a[i] = genObject();
      return a;
    }
    if (t == CharSequence[].class) {
      int n = rng.nextInt(4);
      CharSequence[] a = new CharSequence[n];
      for (int i = 0; i < n; i++) a[i] = genString();
      return a;
    }
    if (t == Locale.class) return LOCALES[rng.nextInt(LOCALES.length)];
    if (t == Charset.class) {
      String[] names = {"UTF-8", "ISO-8859-1", "US-ASCII", "UTF-16"};
      return Charset.forName(names[rng.nextInt(names.length)]);
    }
    if (t == Integer.class) return rng.nextInt(8) == 0 ? null : genInt();
    if (t == Long.class) return rng.nextInt(8) == 0 ? null : genLong();
    if (t == Short.class) return (short) genInt();
    if (t == Byte.class) return (byte) genInt();
    if (t == Character.class) return genChar();
    if (t == Boolean.class) return rng.nextBoolean();
    if (t == Double.class) return genDouble();
    if (t == Float.class) return (float) genDouble();
    if (t == Throwable.class) {
      return rng.nextBoolean() ? null : new RuntimeException("cause");
    }
    if (t == Pattern.class) return Pattern.compile("a|b");
    throw new UnsupportedOperationException(t.getName());
  }

  /** A receiver for an instance method of {@code c}. */
  Object receiver(Class<?> c) throws Exception {
    if (c == String.class) {
      String s = genString();
      return s == null ? "" : s;
    }
    if (c == StringBuilder.class) return new StringBuilder(nonNull());
    if (c == StringBuffer.class) return new StringBuffer(nonNull());
    if (c == Integer.class) return genInt();
    if (c == Long.class) return genLong();
    if (c == Short.class) return (short) genInt();
    if (c == Byte.class) return (byte) genInt();
    if (c == Character.class) return genChar();
    if (c == Boolean.class) return rng.nextBoolean();
    if (c == Double.class) return genDouble();
    if (c == Float.class) return (float) genDouble();
    if (c == Random.class) return new Random(genLong());
    if (c == Pattern.class) {
      return Pattern.compile(genRegex(), rng.nextInt(4) == 0 ? rng.nextInt(512) : 0);
    }
    if (c == Matcher.class) {
      Matcher m = Pattern.compile(genRegex()).matcher(nonNull());
      if (rng.nextBoolean()) m.find();
      return m;
    }
    if (c == Number.class) {
      return rng.nextBoolean() ? (Object) genInt() : (Object) genDouble();
    }
    if (Throwable.class.isAssignableFrom(c)) {
      // a constructor of the models (the same list on every JDK)
      List<String> ctors = CTORS.get(c.getName());
      if (ctors == null) return null;
      String[] ps = ctors.get(rng.nextInt(ctors.size())).split(",", -1);
      if (ps.length == 1 && ps[0].isEmpty()) ps = new String[0];
      Class<?>[] types = new Class<?>[ps.length];
      for (int i = 0; i < ps.length; i++) types[i] = type(ps[i]);
      Constructor<?> k;
      try {
        k = c.getConstructor(types);
      } catch (NoSuchMethodException e) {
        throw new UnsupportedOperationException("absent constructor");
      }
      Object[] a = new Object[types.length];
      for (int i = 0; i < a.length; i++) a[i] = gen(k.getParameterTypes()[i]);
      try {
        return k.newInstance(a);
      } catch (InvocationTargetException e) {
        throw (Exception) e.getCause();
      }
    }
    return null;
  }

  String nonNull() {
    String s = genString();
    return s == null ? "" : s;
  }

  static String esc(String s) {
    if (s == null) return "null";
    StringBuilder b = new StringBuilder("\"");
    for (int i = 0; i < s.length(); i++) {
      char c = s.charAt(i);
      if (c >= 0x20 && c < 0x7f && c != '\\' && c != '"') b.append(c);
      else b.append(String.format("\\u%04x", (int) c));
    }
    return b.append('"').toString();
  }

  static String render(Object o) {
    if (o == null) return "null";
    if (o instanceof String) return esc((String) o);
    if (o instanceof Double) {
      return "d" + Long.toHexString(Double.doubleToRawLongBits((Double) o));
    }
    if (o instanceof Float) {
      return "f" + Integer.toHexString(Float.floatToRawIntBits((Float) o));
    }
    if (o instanceof Character) return "c" + Integer.toHexString((Character) o);
    if (o instanceof Number || o instanceof Boolean) return o.toString();
    if (o instanceof CharSequence) return "sb" + esc(o.toString());
    if (o instanceof char[]) return "C" + esc(new String((char[]) o));
    if (o instanceof int[]) return "I" + java.util.Arrays.toString((int[]) o);
    if (o instanceof byte[]) return "B" + java.util.Arrays.toString((byte[]) o);
    if (o instanceof Object[]) {
      StringBuilder b = new StringBuilder("[");
      for (Object e : (Object[]) o) b.append(render(e)).append(',');
      return b.append(']').toString();
    }
    if (o instanceof OutOfMemoryError) {
      // (whether an allocation fits depends on the heap, not the version)
      return "OOM";
    }
    if (o instanceof Throwable) {
      Throwable t = (Throwable) o;
      return "T" + t.getClass().getName() + ":" + esc(t.getMessage())
          + (t.getCause() == null ? "" : " cause=" + render(t.getCause()));
    }
    if (o instanceof Pattern) {
      return "P" + esc(o.toString()) + "/" + ((Pattern) o).flags();
    }
    if (o instanceof Matcher) {
      Matcher m = (Matcher) o;
      String state;
      try {
        state = m.start() + "-" + m.end() + esc(m.group());
      } catch (IllegalStateException e) {
        state = "nomatch";
      }
      return "M" + state + "/" + m.groupCount();
    }
    if (o instanceof Random) return "R" + ((Random) o).nextLong();
    if (o instanceof java.util.stream.IntStream) {
      return "S" + java.util.Arrays.toString(((java.util.stream.IntStream) o).toArray());
    }
    if (o instanceof java.util.stream.Stream) {
      StringBuilder b = new StringBuilder("S[");
      ((java.util.stream.Stream<?>) o).forEach(e -> b.append(render(e)).append(','));
      return b.append(']').toString();
    }
    if (o instanceof java.util.Optional) {
      java.util.Optional<?> p = (java.util.Optional<?>) o;
      return p.isPresent() ? "O" + render(p.get()) : "O-";
    }
    return "?" + o.getClass().getName() + ":"
        + esc(o.toString().replaceAll("@[0-9a-f]+", "@H"));
  }

  /** A Java string literal of {@code s}. */
  static String strLit(String s) {
    StringBuilder b = new StringBuilder("\"");
    for (int i = 0; i < s.length(); i++) {
      char c = s.charAt(i);
      if (c == '\n') b.append("\\n");
      else if (c == '\r') b.append("\\r");
      else if (c == '"') b.append("\\\"");
      else if (c == '\\') b.append("\\\\");
      else if (c >= 0x20 && c < 0x7f) b.append(c);
      else b.append(String.format("\\u%04x", (int) c));
    }
    return b.append('"').toString();
  }

  /**
   * A Java expression of the value {@code o}, or "?" where there is none
   * (a Locale, a Pattern, ...).
   */
  static String lit(Object o) {
    if (o == null) return "null";
    if (o instanceof String) return strLit((String) o);
    if (o instanceof Integer) return "Integer.valueOf(" + o + ")";
    if (o instanceof Long) return "Long.valueOf(" + o + "L)";
    if (o instanceof Short) return "Short.valueOf((short) " + o + ")";
    if (o instanceof Byte) return "Byte.valueOf((byte) " + o + ")";
    if (o instanceof Boolean) return "Boolean.valueOf(" + o + ")";
    if (o instanceof Character) {
      return "Character.valueOf((char) " + (int) (Character) o + ")";
    }
    if (o instanceof Double) {
      double d = (Double) o;
      return "Double.valueOf(" + (Double.isNaN(d) ? "Double.NaN"
          : Double.isInfinite(d) ? (d > 0 ? "Double.POSITIVE_INFINITY"
                                          : "Double.NEGATIVE_INFINITY")
          : Double.toHexString(d)) + ")";
    }
    if (o instanceof Float) {
      float d = (Float) o;
      return "Float.valueOf(" + (Float.isNaN(d) ? "Float.NaN"
          : Float.isInfinite(d) ? (d > 0 ? "Float.POSITIVE_INFINITY"
                                         : "Float.NEGATIVE_INFINITY")
          : Float.toHexString(d) + "f") + ")";
    }
    if (o instanceof StringBuilder) {
      return "new StringBuilder(" + strLit(o.toString()) + ")";
    }
    if (o instanceof StringBuffer) {
      return "new StringBuffer(" + strLit(o.toString()) + ")";
    }
    if (o instanceof char[]) {
      StringBuilder b = new StringBuilder("new char[] {");
      for (char c : (char[]) o) b.append("(char) ").append((int) c).append(',');
      return b.append('}').toString();
    }
    if (o instanceof int[]) {
      StringBuilder b = new StringBuilder("new int[] {");
      for (int c : (int[]) o) b.append(c).append(',');
      return b.append('}').toString();
    }
    if (o instanceof byte[]) {
      StringBuilder b = new StringBuilder("new byte[] {");
      for (byte c : (byte[]) o) b.append(c).append(',');
      return b.append('}').toString();
    }
    if (o instanceof Object[]) {
      StringBuilder b = new StringBuilder("new Object[] {");
      for (Object c : (Object[]) o) b.append(lit(c)).append(',');
      return b.append('}').toString();
    }
    if (o instanceof RuntimeException && "cause".equals(((Throwable) o).getMessage())) {
      return "new RuntimeException(\"cause\")";
    }
    return "?";
  }

  static Class<?> type(String name) throws ClassNotFoundException {
    switch (name) {
      case "int": return int.class;
      case "long": return long.class;
      case "short": return short.class;
      case "byte": return byte.class;
      case "char": return char.class;
      case "boolean": return boolean.class;
      case "double": return double.class;
      case "float": return float.class;
      default:
        if (name.endsWith("[]")) {
          return java.lang.reflect.Array.newInstance(
              type(name.substring(0, name.length() - 2)), 0).getClass();
        }
        return Class.forName(name);
    }
  }

  static final java.util.Set<String> SKIP = new java.util.HashSet<>(
      java.util.Arrays.asList("hashCode", "wait", "notify", "notifyAll",
          "getClass", "printStackTrace", "fillInStackTrace", "getStackTrace",
          "setStackTrace", "intern", "describeConstable",
          "resolveConstantDesc", "addSuppressed", "getSuppressed", "doubles",
          "ints", "longs", "initCause", "random"));

  /** The parameters of the constructors of each class in the API list. */
  static final java.util.Map<String, List<String>> CTORS =
      new java.util.HashMap<>();

  public static void main(String[] args) throws Exception {
    int cases = Integer.parseInt(args[1]);
    String prefix = args.length > 3 ? args[3] : "";
    PrintStream out = new PrintStream(args[2], "UTF-8");
    List<String> lines = new ArrayList<>();
    BufferedReader in = new BufferedReader(new FileReader(args[0]));
    for (String l; (l = in.readLine()) != null; ) {
      lines.add(l);
      String[] f = l.split("\\|", -1);
      if (f[1].equals("ctor")) {
        CTORS.computeIfAbsent(f[0], x -> new ArrayList<>()).add(f[3]);
      }
    }
    for (String line : lines) {
      String[] f = line.split("\\|", -1);
      if (!f[0].startsWith(prefix) || SKIP.contains(f[2])) continue;
      String key = f[0] + "." + f[2] + "(" + f[3] + ")";
      Class<?> c;
      Class<?>[] params;
      Member member;
      try {
        c = Class.forName(f[0]);
        String[] ps = f[3].isEmpty() ? new String[0] : f[3].split(",");
        params = new Class<?>[ps.length];
        for (int i = 0; i < ps.length; i++) params[i] = type(ps[i]);
        member = f[1].equals("ctor") ? c.getConstructor(params)
                                     : c.getMethod(f[2], params);
      } catch (ClassNotFoundException | NoSuchMethodException e) {
        out.println(key + "|ABSENT");
        continue;
      }
      if (member instanceof Method
          && Modifier.isStatic(member.getModifiers()) != f[1].equals("static")) {
        continue;
      }
      for (int k = 0; k < cases; k++) {
        out.println(key + "|" + k + "|" + runCase(key, k, c, params, member, f));
      }
      out.flush();
    }
    out.close();
  }

  static String runCase(String key, int k, Class<?> c, Class<?>[] params,
      Member member, String[] f) {
    Probe p = new Probe(key.hashCode() * 1000003L + k);
    String name = f[2];
    p.regexFirst = (c == Pattern.class && f[1].equals("static"))
        || (c == String.class && (name.equals("matches")
            || name.equals("replaceAll") || name.equals("replaceFirst")
            || name.equals("split")));
    p.replacementSecond = name.startsWith("replace") && !name.equals("replace");
    StringBuilder desc = new StringBuilder();
    // the receiver and the arguments as Java expressions (see lit)
    StringBuilder jdesc = new StringBuilder();
    Object recv = null;
    Object[] actual = new Object[params.length];
    try {
      if (f[1].equals("instance")) {
        recv = p.receiver(c);
        if (recv == null) return "NORECEIVER";
        desc.append(render(recv));
        jdesc.append(lit(recv));
      }
      if (c == Matcher.class
          && (name.startsWith("replace") || name.startsWith("append"))) {
        // (the String of these is a replacement, see gen)
        p.stringIndex = 1;
        p.replacementSecond = true;
      }
      for (int i = 0; i < params.length; i++) {
        actual[i] = p.gen(params[i]);
        desc.append(' ').append(render(actual[i]));
        jdesc.append('\u0001').append(
            actual[i] == null ? "(" + params[i].getCanonicalName() + ") null"
                              : lit(actual[i]));
      }
    } catch (UnsupportedOperationException e) {
      return "UNSUPPORTED " + e.getMessage();
    } catch (Exception e) {
      return "GENFAIL " + render(e);
    }
    String result;
    try {
      Object r = member instanceof Constructor
          ? ((Constructor<?>) member).newInstance(actual)
          : ((Method) member).invoke(recv, actual);
      result = f[1].equals("ctor") || !f[4].equals("void") ? render(r) : "void";
      if (recv instanceof CharSequence && !(recv instanceof String)) {
        result += " recv=" + render(recv);
      }
      if (recv instanceof Matcher) result += " recv=" + render(recv);
      for (Object a : actual) {
        if (a instanceof char[] || a instanceof CharSequence && !(a instanceof String)) {
          result += " arg=" + render(a);
        }
      }
    } catch (InvocationTargetException e) {
      result = render(e.getCause());
    } catch (Throwable e) {
      result = "FAIL " + render(e);
    }
    return desc + " => " + result + "\tJ" + jdesc;
  }
}
