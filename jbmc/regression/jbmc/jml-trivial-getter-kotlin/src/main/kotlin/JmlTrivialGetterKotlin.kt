package experiment

// Two nested data classes mirror the AWS-Bastion shape:
//   ColumnAttributes { sensitivity: Sensitivity }
//   Sensitivity { isViewable: Boolean }
// JML can chain trivial getters: attrs.sensitivity.viewable
// resolves to a nested member_exprt against the backing fields.
data class Sensitivity(val viewable: Boolean)
data class ColumnAttributes(val sensitivity: Sensitivity)

class JmlTrivialGetterKotlin {
    //@ requires attrs != null;
    //@ requires attrs.getSensitivity().getViewable();
    fun validate(attrs: ColumnAttributes) {
        // body is irrelevant; the contract is what the test checks
    }
}
