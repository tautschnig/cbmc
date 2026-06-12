       IDENTIFICATION DIVISION.
       PROGRAM-ID. EVAL-ALNUM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-CODE  PIC X(2) VALUE '00'.
       01  WS-FLAG  PIC X    VALUE 'A'.
       01  WS-N     PIC 9(4) COMP VALUE 7.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    EVALUATE over an alphanumeric subject.
           EVALUATE WS-CODE
               WHEN '00'
                   MOVE 1 TO WS-R
               WHEN '10'
                   MOVE 2 TO WS-R
               WHEN OTHER
                   MOVE 9 TO WS-R
           END-EVALUATE.
           CALL "__CPROVER_assert" USING WS-R = 1.
      *    Parenthesised condition mixing alphanumeric and numeric tests.
           IF (WS-FLAG = 'A' OR WS-FLAG = 'B') AND WS-N > 5
               MOVE 1 TO WS-R
           ELSE
               MOVE 0 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
