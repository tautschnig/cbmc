       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-REFMOD-NUM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-NUM   PIC 9(6) VALUE 123456.
       01  WS-PART  PIC X(2).
       01  WS-R     PIC 9    VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Reference modification of a numeric item yields an alphanumeric
      *    result (IBM LR "Reference modification"); it must be accepted as a
      *    MOVE source rather than treated as a numeric expression.
           MOVE WS-NUM(1:2) TO WS-PART.
           MOVE 5 TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 5.
           STOP RUN.
