       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUM-ALNUM-CMP.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(4) COMP VALUE 5.
       01  WS-A  PIC X(4).
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Comparing a numeric item with an alphanumeric one is modelled as
      *    nondeterministic, so both branches are reachable.
           IF WS-N = WS-A
               MOVE 1 TO WS-R
           ELSE
               MOVE 2 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R > 0.
           STOP RUN.
