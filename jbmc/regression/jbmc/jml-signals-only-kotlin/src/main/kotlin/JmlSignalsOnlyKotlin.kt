package experiment

class InvalidInputException(msg: String) : RuntimeException(msg)

class JmlSignalsOnlyKotlin {
    //@ requires queryGraphValid;
    //@ signals_only InvalidInputException;
    fun validate(queryGraphValid: Boolean, foundIssue: Boolean) {
        if (!queryGraphValid) {
            throw InvalidInputException("graph invalid")
        }
        if (foundIssue) {
            throw InvalidInputException("issue found")
        }
    }
}
