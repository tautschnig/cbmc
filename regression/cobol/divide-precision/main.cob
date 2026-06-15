       IDENTIFICATION DIVISION.
       PROGRAM-ID. DIVIDE-PRECISION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9(3)V99.
       01  WS-Q  PIC 9(3).
       01  WS-R  PIC 9(3).
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "DIVIDE statement": the quotient is developed with the
      *    receiver's decimal places (a guard digit for ROUNDED), so
      *    1000 / 3 ROUNDED = 333.33 (not 333.00).
           DIVIDE 1000 BY 3 GIVING WS-A ROUNDED.
           CALL "__CPROVER_assert" USING WS-A = 333.33.
      *    REMAINDER uses the truncated integer quotient: 17 / 5 = 3 rem 2.
           DIVIDE 17 BY 5 GIVING WS-Q REMAINDER WS-R.
           CALL "__CPROVER_assert" USING WS-Q = 3.
           CALL "__CPROVER_assert" USING WS-R = 2.
           STOP RUN.
