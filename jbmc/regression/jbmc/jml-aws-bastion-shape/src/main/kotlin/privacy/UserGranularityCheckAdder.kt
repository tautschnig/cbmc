package privacy

// AWS-Bastion-shape end-to-end smoke test for the JBMC JML
// pipeline. Exercises:
//   * trivial bean-getter recognition (Kotlin val accessors)
//   * chained getter access in JML clauses
//     (attrs.getSensitivity().getViewable())
//   * signals_only and typed signals (T e) ...
//   * requires / ensures gated by exception state
//   * assignable \nothing
//
// Mirrors the AWS-Bastion `validateNonViewableColumns` privacy
// gate, but on a single column rather than a Map of columns
// (since java.util.Map model isn't done — see #5 in the
// outstanding work). The privacy logic is identical: refuse
// the column if its sensitivity is non-viewable.

data class Sensitivity(val viewable: Boolean)
data class ColumnAttributes(val sensitivity: Sensitivity)

class InvalidInputException(msg: String) : RuntimeException(msg)

class UserGranularityCheckAdder {

    /**
     * Throw InvalidInputException iff the column's
     * sensitivity is non-viewable.
     *
     * Mirrors the per-column body of AWS-Bastion's
     * validateNonViewableColumns — the actual production
     * Kotlin maps over a Map<ColumnVertex, ColumnAttributes>
     * and applies this same predicate to each entry.
     */
    //@ requires attrs != null;
    //@ requires attrs.getSensitivity() != null;
    //@ assignable \nothing;
    //@ ensures attrs.getSensitivity().getViewable();
    //@ signals_only InvalidInputException;
    //@ signals (InvalidInputException e) e != null;
    fun validate(attrs: ColumnAttributes) {
        if(!attrs.sensitivity.viewable)
            throw InvalidInputException("non-viewable column")
    }
}
