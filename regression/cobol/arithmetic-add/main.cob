       IDENTIFICATION DIVISION.
       PROGRAM-ID. ADD-TEST.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A    PIC 9(4) VALUE 2.
       01  WS-B    PIC 9(4) VALUE 3.
       01  WS-C    PIC 9(4).
       PROCEDURE DIVISION.
       MAIN-PARA.
           ADD WS-A TO WS-B GIVING WS-C.
           CALL "__CPROVER_assert" USING WS-C = 5.
           STOP RUN.
