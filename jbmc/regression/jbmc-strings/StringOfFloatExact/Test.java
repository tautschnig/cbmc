public class Test {
  public static void neg(float f) { if(f != -1.5f) return; assert String.valueOf(f).equals("-1.5"); }
  public static void negBad(float f) { if(f != -1.5f) return; assert !String.valueOf(f).equals("-1.5"); }
  public static void big(float f) { if(f != 123456.7f) return; assert String.valueOf(f).equals("123456.7"); }
  public static void bigBad(float f) { if(f != 123456.7f) return; assert !String.valueOf(f).startsWith("1"); }
  public static void third(float f) { if(f != 1f/3) return; assert String.valueOf(f).equals("0.33333334"); }
  public static void thirdBad(float f) { if(f != 1f/3) return; assert !String.valueOf(f).equals("0.33333334"); }
  public static void sci(float f) { if(f != 1e30f) return; assert String.valueOf(f).equals("1.0E30"); }
  public static void sciBad(float f) { if(f != 1e30f) return; assert !String.valueOf(f).startsWith("1.0E"); }
  public static void negSci(float f) { if(f != -1.5e10f) return; assert String.valueOf(f).equals("-1.5E10"); }
  public static void dbl(double d) { if(d != 1.0/3) return; assert String.valueOf(d).equals("0.3333333333333333"); }
  public static void dblBad(double d) { if(d != 1.0/3) return; assert !String.valueOf(d).equals("0.3333333333333333"); }
  public static void dblSmall(double d) { if(d != 0.001) return; assert String.valueOf(d).equals("0.001"); }
}
