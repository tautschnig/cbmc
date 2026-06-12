       IDENTIFICATION DIVISION.
       PROGRAM-ID. ABBREV-COND.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(4) COMP VALUE 2.
       01  WS-F  PIC X       VALUE 'B'.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Subject and operator implied: WS-N = 1 OR = 2 OR = 3.
           IF WS-N = 1 OR 2 OR 3
               ADD 1 TO WS-R
           END-IF.
      *    Operator given, subject implied: WS-N = 5 OR WS-N > 0.
           IF WS-N = 5 OR > 0
               ADD 1 TO WS-R
           END-IF.
      *    Alphanumeric abbreviated: WS-F = 'A' OR WS-F = 'B'.
           IF WS-F = 'A' OR 'B'
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
