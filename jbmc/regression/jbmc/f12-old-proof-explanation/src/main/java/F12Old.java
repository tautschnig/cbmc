import static org.strata.jverify.JVerify.*;

public final class F12Old {

    static int counter = 0;

    static void bump() {
        precondition(counter >= 0 && counter < 1000);
        assigns(counter);
        counter = counter + 1;
        postcondition(counter == old(counter) + 1);
    }

    static int caller(int initial) {
        precondition(initial >= 0 && initial < 100);
        precondition(counter == initial);
        bump();
        bump();
        check(counter == initial + 2);
        return counter;
    }
}
