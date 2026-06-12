       IDENTIFICATION DIVISION.
       PROGRAM-ID. REDEF-TEST.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-REC.
           05  WS-A    PIC 9(4) COMP.
           05  WS-B    REDEFINES WS-A PIC 9(4) COMP.
           05  WS-C    PIC 9(4) COMP.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 1234 TO WS-A.
           MOVE 7 TO WS-C.
           CALL "__CPROVER_assert" USING WS-B = 1234.
           CALL "__CPROVER_assert" USING WS-C = 7.
           STOP RUN.
