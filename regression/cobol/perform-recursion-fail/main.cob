       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORM-RECURSION-FAIL.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N    PIC 9(4) VALUE 5.
       01  WS-ACC  PIC 9(9) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Negative case: the real accumulated value is 5, so asserting 999
      *    must FAIL. This guards against a regression to the old behaviour,
      *    where a recursive PERFORM was pruned with assume(false) and made
      *    every post-recursion assertion pass vacuously.
           PERFORM COUNT-DOWN.
           CALL "__CPROVER_assert" USING WS-ACC = 999.
           STOP RUN.
       COUNT-DOWN.
           IF WS-N > 0
               ADD 1 TO WS-ACC
               SUBTRACT 1 FROM WS-N
               PERFORM COUNT-DOWN
           END-IF.
