public class JmlSignalsOnly {

    /**
     * The method may throw IllegalArgumentException for negative
     * inputs; signals_only declares this is the only exception
     * type permitted to escape, and JBMC verifies that no other
     * type can leak.
     */
    //@ signals_only IllegalArgumentException;
    public static int safeSqrt(int x) {
        if (x < 0) {
            throw new IllegalArgumentException("negative");
        }
        return (int) Math.sqrt(x);
    }
}
