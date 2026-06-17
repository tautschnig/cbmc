       IDENTIFICATION DIVISION.
       PROGRAM-ID. EDFLOAT.
      * Floating insertion editing (IBM LR "PICTURE clause", floating
      * insertion characters): a run of floating '$' or '-' provides one
      * fewer digit position than its length, and the symbol floats to just
      * left of the first significant digit (a floating '-' shows a space
      * when the value is non-negative).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9(3)V99 VALUE 12.34.
       01  WS-B  PIC 9(3)V99 VALUE 1.00.
       01  WS-N  PIC S9(3) VALUE -12.
       01  WS-E1 PIC $$$$.99.
       01  WS-E2 PIC $$$$.99.
       01  WS-E3 PIC ----9.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           MOVE WS-A TO WS-E1.
           IF WS-E1 = ' $12.34'
               ADD 1 TO WS-R
           END-IF.
           MOVE WS-B TO WS-E2.
           IF WS-E2 = '  $1.00'
               ADD 1 TO WS-R
           END-IF.
           MOVE WS-N TO WS-E3.
           IF WS-E3 = '  -12'
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
