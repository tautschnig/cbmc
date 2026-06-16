       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUMALNX.
      * IBM LR "Comparison of numeric and nonnumeric operands": the numeric
      * operand is compared as though moved to an alphanumeric item, i.e. by
      * its magnitude digit string (no decimal point, sign dropped). This is
      * now exact for COMP (binary), scaled, and signed operands, not only
      * unsigned scale-0 DISPLAY.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-C   PIC 9(4) COMP VALUE 1234.
       01  WS-S   PIC 9(2)V99 VALUE 12.34.
       01  WS-G   PIC S9(4) VALUE -12.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF WS-C = "1234"
               ADD 1 TO WS-R
           END-IF.
           IF WS-S = "1234"
               ADD 1 TO WS-R
           END-IF.
           IF WS-G = "0012"
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
