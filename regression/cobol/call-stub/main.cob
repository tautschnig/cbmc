       IDENTIFICATION DIVISION.
       PROGRAM-ID. CALL-STUB.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9(4) COMP VALUE 5.
       01  WS-B  PIC 9(4) COMP VALUE 5.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    A separately-compiled program: the BY REFERENCE argument WS-A may
      *    be modified (havoced), but WS-B is not passed and keeps its value.
           CALL 'SUBPROG' USING WS-A.
           CALL "__CPROVER_assert" USING WS-B = 5.
           STOP RUN.
