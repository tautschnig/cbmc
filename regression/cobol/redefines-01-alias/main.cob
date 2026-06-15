       IDENTIFICATION DIVISION.
       PROGRAM-ID. REDEFINES-01-ALIAS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9(3) VALUE 0.
       01  WS-B  REDEFINES WS-A PIC 9(3).
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "REDEFINES clause": WS-B shares WS-A's storage, so writing
      *    WS-A is visible through WS-B.
           MOVE 123 TO WS-A.
           CALL "__CPROVER_assert" USING WS-B = 123.
           STOP RUN.
