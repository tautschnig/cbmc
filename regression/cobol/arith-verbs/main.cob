       IDENTIFICATION DIVISION.
       PROGRAM-ID. ARITH-VERBS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A    PIC 9(4) VALUE 20.
       01  WS-B    PIC 9(4) VALUE 4.
       01  WS-C    PIC 9(4).
       01  WS-D    PIC 9(4).
       01  WS-E    PIC 9(4).
       PROCEDURE DIVISION.
       MAIN-PARA.
           SUBTRACT WS-B FROM WS-A GIVING WS-C.
           MULTIPLY WS-A BY WS-B GIVING WS-D.
           DIVIDE WS-B INTO WS-A GIVING WS-E.
           CALL "__CPROVER_assert" USING WS-C = 16.
           CALL "__CPROVER_assert" USING WS-D = 80.
           CALL "__CPROVER_assert" USING WS-E = 5.
           STOP RUN.
