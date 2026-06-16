       IDENTIFICATION DIVISION.
       PROGRAM-ID. COMP1CMP.
      * COMP-1 short floating point and a float comparison (IBM LR "USAGE
      * clause"; "Relation condition"). 1.0 / 4 = 0.25, which lies strictly
      * between 0 and 1.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S  USAGE COMP-1.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           MOVE 1 TO WS-S.
           COMPUTE WS-S = WS-S / 4.
           IF WS-S < 1 AND WS-S > 0
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
