package experiment

// Kotlin data class: properties are private final fields with
// public getX() accessors. The bytecode shape is the AWS-Bastion
// idiom — JML wants to read `attrs.viewable` directly even
// though `viewable` is private; JBMC operates on bytecode where
// access modifiers don't gate analysis.
data class ColumnAttributes(val viewable: Boolean, val joinable: Boolean)

class JmlKotlinDataClass {
    //@ requires attrs != null;
    //@ requires attrs.viewable;
    //@ ensures \result == attrs.viewable;
    fun isViewable(attrs: ColumnAttributes): Boolean {
        return attrs.viewable
    }
}
