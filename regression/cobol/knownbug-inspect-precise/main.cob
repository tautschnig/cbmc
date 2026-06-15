       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-INSPECT-PRECISE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S    PIC X(5) VALUE 'AABAA'.
       01  WS-CNT  PIC 9(2) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "INSPECT statement": counting 'A' in 'AABAA' gives 4.
      *    The frontend havocs the TALLYING counter.
           INSPECT WS-S TALLYING WS-CNT FOR ALL 'A'.
           CALL "__CPROVER_assert" USING WS-CNT = 4.
           STOP RUN.
