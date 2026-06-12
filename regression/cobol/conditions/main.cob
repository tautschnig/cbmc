       IDENTIFICATION DIVISION.
       PROGRAM-ID. CONDITIONS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC S9(4) COMP VALUE 5.
       01  WS-B  PIC S9(4) COMP VALUE 5.
       01  WS-C  PIC S9(4) COMP VALUE -3.
       01  WS-X  PIC X(4) VALUE '12AB'.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Relational operators written as words, with IS and NOT.
           IF WS-A IS EQUAL TO WS-B
               ADD 1 TO WS-R
           END-IF.
           IF WS-A NOT = WS-C
               ADD 1 TO WS-R
           END-IF.
           IF WS-A IS GREATER THAN WS-C
               ADD 1 TO WS-R
           END-IF.
      *    Sign conditions (exact on the algebraic value).
           IF WS-C IS NEGATIVE
               ADD 1 TO WS-R
           END-IF.
           IF WS-A IS POSITIVE
               ADD 1 TO WS-R
           END-IF.
      *    Class condition is nondeterministic; both branches have no
      *    effect on WS-R, so the assertion below holds regardless.
           IF WS-X IS NUMERIC
               CONTINUE
           ELSE
               CONTINUE
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 5.
           STOP RUN.
