       IDENTIFICATION DIVISION.
       PROGRAM-ID. DIVSE.
      * Division by zero WITH an ON SIZE ERROR phrase: per IBM LR "SIZE
      * ERROR phrases" a zero divisor raises the size-error condition, the
      * quotient is not stored and the imperative runs. So this is well
      * defined (no fault): the division-by-zero condition is folded into the
      * size-error test rather than asserted.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9(4) VALUE 100.
       01  WS-Z  PIC 9    VALUE 0.
       01  WS-R  PIC 9(4) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           DIVIDE WS-A BY WS-Z GIVING WS-R
               ON SIZE ERROR MOVE 9 TO WS-R
           END-DIVIDE.
           STOP RUN.
