       IDENTIFICATION DIVISION.
       PROGRAM-ID. EDSIGN.
      * Sign and CR/DB numeric editing (IBM LR "PICTURE clause" editing): a
      * leading '+' shows '+'/'-' by sign; a trailing 'CR' shows when the
      * value is negative and two spaces otherwise.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC S9(4)V99 VALUE -12.34.
       01  WS-P  PIC S9(4)V99 VALUE 12.34.
       01  WS-E1 PIC +9999.99.
       01  WS-E2 PIC 9999.99CR.
       01  WS-E3 PIC 9999.99CR.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           MOVE WS-A TO WS-E1.
           IF WS-E1 = '-0012.34'
               ADD 1 TO WS-R
           END-IF.
           MOVE WS-A TO WS-E2.
           IF WS-E2 = '0012.34CR'
               ADD 1 TO WS-R
           END-IF.
           MOVE WS-P TO WS-E3.
           IF WS-E3 = '0012.34  '
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
