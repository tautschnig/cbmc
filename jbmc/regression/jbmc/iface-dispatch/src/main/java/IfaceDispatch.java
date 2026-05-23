interface Greeter { int greet(); }
class Hello implements Greeter { public int greet() { return 1; } }
class Bye implements Greeter { public int greet() { return 2; } }

public class IfaceDispatch {
    static int test(Greeter g) {
        if (g == null) return -1;
        int r = g.greet();
        assert r == 1 || r == 2;
        return r;
    }
}
