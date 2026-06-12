       IDENTIFICATION DIVISION.
       PROGRAM-ID. ALNUM-COMPARE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-NAME    PIC X(10) VALUE 'ALICE'.
       01  WS-FLAG    PIC X      VALUE 'Y'.
       01  WS-EMPTY   PIC X(5).
       01  WS-R       PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Equal with right space-padding: 'ALICE' = 'ALICE     '.
           IF WS-NAME = 'ALICE'
               ADD 1 TO WS-R
           END-IF.
      *    Single character equality.
           IF WS-FLAG = 'Y'
               ADD 1 TO WS-R
           END-IF.
      *    Figurative SPACES compare (WS-EMPTY has no VALUE -> nondet),
      *    constrained so the branch is taken.
           MOVE SPACES TO WS-EMPTY
           IF WS-EMPTY = SPACES
               ADD 1 TO WS-R
           END-IF.
      *    Lexicographic ordering by collating sequence.
           IF 'ABC' < 'ABD'
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 4.
           STOP RUN.
