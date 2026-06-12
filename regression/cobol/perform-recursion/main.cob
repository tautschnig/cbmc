       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORM-RECURSION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM 100-A.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
       100-A.
      *    100-A performs itself: a recursive PERFORM. The re-entrant call is
      *    pruned (assume false), so the program lowers and is bounded rather
      *    than rejected. The guard below is false here, so WS-R stays 1.
           MOVE 1 TO WS-R.
           IF WS-R > 5
               PERFORM 100-A
           END-IF.
