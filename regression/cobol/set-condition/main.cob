       IDENTIFICATION DIVISION.
       PROGRAM-ID. SET-COND.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-FLAG  PIC X VALUE 'N'.
           88  FLAG-ON    VALUE 'Y'.
           88  FLAG-OFF   VALUE 'N'.
       01  WS-IDX   PIC 9(4) VALUE 0.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           SET FLAG-ON TO TRUE.
           SET WS-IDX TO 3.
           IF FLAG-ON
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           CALL "__CPROVER_assert" USING WS-IDX = 3.
           STOP RUN.
