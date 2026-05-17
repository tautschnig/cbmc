import static org.strata.jverify.JVerify.*;

public final class F12ModularTest {

  static int counter = 0;

  static int callee(int x) {
    precondition(x >= 0);
    precondition(x < 1000);
    postcondition(true);
    assigns(counter);
    counter = counter + 1;
    return x + 1;
  }

  static int caller() {
    precondition(true);
    int v = callee(7);
    assert v == v;
    return v;
  }
}
