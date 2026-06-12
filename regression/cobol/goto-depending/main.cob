       IDENTIFICATION DIVISION.
       PROGRAM-ID. GOTO-DEPENDING.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-SEL  PIC 9 VALUE 2.
       01  WS-R    PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    GO TO ... DEPENDING ON transfers to the WS-SEL-th label (IBM LR
      *    "GO TO statement", format 3).
           GO TO P1 P2 P3 DEPENDING ON WS-SEL.
           MOVE 9 TO WS-R.
           GO TO DONE-PARA.
       P1.
           MOVE 1 TO WS-R.
           GO TO DONE-PARA.
       P2.
           MOVE 2 TO WS-R.
           GO TO DONE-PARA.
       P3.
           MOVE 3 TO WS-R.
       DONE-PARA.
           CALL "__CPROVER_assert" USING WS-R = 2.
           STOP RUN.
