       IDENTIFICATION DIVISION.
       PROGRAM-ID. ROUNDED-PHRASE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A    PIC 9V999 VALUE 1.005.
       01  WS-RND  PIC 9V99  VALUE 0.
       01  WS-TRC  PIC 9V99  VALUE 0.
       01  WS-R    PIC 9     VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    ROUNDED rounds the discarded fraction (1.005 -> 1.01); without it
      *    the result truncates (1.00). See IBM LR "ROUNDED phrase".
           ADD WS-A TO 0 GIVING WS-RND ROUNDED.
           ADD WS-A TO 0 GIVING WS-TRC.
           IF WS-RND = 1.01 AND WS-TRC = 1.00
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
