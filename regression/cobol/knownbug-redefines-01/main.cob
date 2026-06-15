       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-REDEFINES-01.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9(3) VALUE 0.
       01  WS-B  REDEFINES WS-A PIC 9(3).
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "REDEFINES clause": WS-B shares WS-A's storage, so writing
      *    WS-A must be visible through WS-B. The frontend models a 01-level
      *    REDEFINES as a separate record symbol (not aliased), so WS-B stays 0.
           MOVE 123 TO WS-A.
           CALL "__CPROVER_assert" USING WS-B = 123.
           STOP RUN.
