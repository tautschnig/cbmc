       IDENTIFICATION DIVISION.
       PROGRAM-ID. UNSIGNED-SIGN.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-U  PIC 9(3) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "MOVE statement" + "PICTURE clause" (symbol 9): an unsigned
      *    receiver stores the ABSOLUTE value, so WS-U must be 5.
           MOVE -5 TO WS-U.
           CALL "__CPROVER_assert" USING WS-U = 5.
           STOP RUN.
