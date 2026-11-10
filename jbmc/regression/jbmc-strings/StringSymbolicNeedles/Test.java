public class Test {
  public static void notContains(String s, String t) {
    if(s == null || t == null || t.length() == 0) return;
    if(!s.contains(t)) assert !s.equals(t); }
  public static void notContainsIndex(String s, String t) {
    if(s == null || t == null || t.length() < 2) return;
    if(!s.contains(t)) assert s.indexOf(t) < 0; }
  public static void notContainsLastIndex(String s, String t) {
    if(s == null || t == null || t.length() < 2) return;
    if(!s.contains(t)) assert s.lastIndexOf(t) < 0; }
  public static void twoNeedles(String s, String t, String u) {
    if(s == null || t == null || u == null) return;
    if(s.contains(t) && t.contains(u)) assert s.contains(u); }
  public static void prefixContained(String s, String t) {
    if(s == null || t == null || t.isEmpty() || !s.startsWith(t)) return;
    assert s.contains(t); }
  public static void notContainsBad(String s, String t) {
    if(s == null || t == null || t.length() < 2) return;
    if(!s.contains(t)) assert s.length() < t.length(); }
}
