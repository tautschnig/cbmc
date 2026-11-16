package p;
import java.util.Random;
public class Main {
  public static int VERSION = Integer.getInteger("v", 17);
  static String gen(Random r) {
    String[] parts = {"", "+", "-", "0", "1", "9", "12", "007", ".", "e", "E", "-", "+", "5", "x", "0x", "p", "P", "f", "d", "D", "NaN", "Infinity", " ", "\t", "a", "F", "1234567890123456789", "e309", "e-400", "_", "\u0663", "..", "ee"};
    StringBuilder b = new StringBuilder();
    int n = 1 + r.nextInt(5);
    for (int i = 0; i < n; i++) b.append(parts[r.nextInt(parts.length)]);
    return b.toString();
  }
  public static void main(String[] a) {
    Random r = new Random(1);
    int bad = 0, exactN = 0, total = 0;
    for (int i = 0; i < 500000; i++) {
      String s = gen(r);
      total++;
      String jr, mr;
      boolean anyD = false;
      try { jr = "v" + Double.doubleToRawLongBits(Double.parseDouble(s)); } catch (NumberFormatException e) { jr = "E" + e.getMessage(); }
      try { double v = CProverFloatParse.parseDouble(s); if (Double.isNaN(v) && Double.doubleToRawLongBits(v) != Double.doubleToRawLongBits(Double.NaN)) { anyD = true; mr = "any"; } else if (Double.doubleToRawLongBits(Math.abs(v)) == 0x7ff0000000000001L) { anyD = true; mr = "any"; } else mr = "v" + Double.doubleToRawLongBits(v); } catch (NumberFormatException e) { mr = "E" + e.getMessage(); }
      if (anyD) { if (jr.startsWith("E")) { if (bad++ < 400) System.out.println("BAD any but JDK throws: [" + s + "] " + jr); } continue; }
      if (mr.startsWith("v")) exactN++;
      if (!jr.equals(mr)) { if (bad++ < 400) System.out.println("BAD [" + s + "] jdk=" + jr + " model=" + mr); }
      // float
      String jf, mf; boolean anyF = false;
      try { jf = "v" + Float.floatToRawIntBits(Float.parseFloat(s)); } catch (NumberFormatException e) { jf = "E" + e.getMessage(); }
      try { float v = CProverFloatParse.parseFloat(s); if (Float.floatToRawIntBits(Math.abs(v)) == 0x7f800001) { anyF = true; mf = "any"; } else mf = "v" + Float.floatToRawIntBits(v); } catch (NumberFormatException e) { mf = "E" + e.getMessage(); }
      if (anyF) { if (jf.startsWith("E") && bad++ < 10) System.out.println("BAD fany [" + s + "] " + jf); continue; }
      if (!jf.equals(mf)) { if (bad++ < 400) System.out.println("BADF [" + s + "] jdk=" + jf + " model=" + mf); }
    }
    System.out.println("total " + total + " exact " + exactN + " bad " + bad);
  }
}
