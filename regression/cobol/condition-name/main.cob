       IDENTIFICATION DIVISION.
       PROGRAM-ID. COND-NAME.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S    PIC 9 VALUE 1.
           88  IS-ONE   VALUE 1.
           88  IS-TWO   VALUE 2.
       01  WS-R    PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF IS-ONE
               MOVE 1 TO WS-R
           ELSE
               MOVE 0 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
