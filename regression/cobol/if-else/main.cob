       IDENTIFICATION DIVISION.
       PROGRAM-ID. IF-ELSE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N    PIC 9(4) VALUE 7.
       01  WS-R    PIC 9(4).
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF WS-N > 5
               MOVE 1 TO WS-R
           ELSE
               MOVE 0 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
