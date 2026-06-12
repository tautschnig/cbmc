       IDENTIFICATION DIVISION.
       PROGRAM-ID. QUAL-NAME.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  REC-A.
           05  AMT     PIC 9(4) COMP VALUE 10.
       01  REC-B.
           05  AMT     PIC 9(4) COMP VALUE 20.
       01  WS-R        PIC 9(4) COMP.
       PROCEDURE DIVISION.
       MAIN-PARA.
           COMPUTE WS-R = AMT OF REC-A + AMT IN REC-B.
           CALL "__CPROVER_assert" USING WS-R = 30.
           CALL "__CPROVER_assert" USING AMT OF REC-A = 10.
           CALL "__CPROVER_assert" USING AMT OF REC-B = 20.
           STOP RUN.
