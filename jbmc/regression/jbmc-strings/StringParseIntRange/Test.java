public class Test {
  public static void minPrint(int i) { if(i != Integer.MIN_VALUE) return; assert String.valueOf(i).equals("-2147483648"); }
  public static void minParse(String s) { if(s == null || !s.equals("-2147483648")) return; assert Integer.parseInt(s) == Integer.MIN_VALUE; }
  public static void minParseBad(String s) { if(s == null || !s.equals("-2147483648")) return; assert Integer.parseInt(s) != Integer.MIN_VALUE; }
  public static void longMinParseBad(String s) { if(s == null || !s.equals("-9223372036854775808")) return; assert Long.parseLong(s) != Long.MIN_VALUE; }
  public static void overflowThrows(String s) { if(s == null || !s.equals("2147483648")) return; try { Integer.parseInt(s); } catch(NumberFormatException e) { assert false; } }
  public static void underflowThrows(String s) { if(s == null || !s.equals("-2147483649")) return; try { Integer.parseInt(s); } catch(NumberFormatException e) { assert false; } }
  public static void longOverflowThrows(String s) { if(s == null || !s.equals("9223372036854775808")) return; try { Long.parseLong(s); } catch(NumberFormatException e) { assert false; } }
  public static void plusMax(String s) { if(s == null || !s.equals("+2147483647")) return; assert Integer.parseInt(s) == Integer.MAX_VALUE; }
}
