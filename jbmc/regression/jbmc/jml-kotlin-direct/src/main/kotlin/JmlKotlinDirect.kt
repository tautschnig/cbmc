package experiment

class JmlKotlinDirect {
    //@ requires lo <= hi;
    //@ ensures \result >= lo && \result <= hi;
    //@ ensures (x >= lo && x <= hi) ==> \result == x;
    fun clamp(x: Int, lo: Int, hi: Int): Int {
        if (x < lo) return lo
        if (x > hi) return hi
        return x
    }
}
