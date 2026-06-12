       IDENTIFICATION DIVISION.
       PROGRAM-ID. FD-VARYING.
       DATA DIVISION.
       FILE SECTION.
      *    A file description with a RECORD IS VARYING ... FROM n TO m
      *    DEPENDING clause: the numeric tokens in the clause must not be
      *    parsed as level numbers (IBM LR "File description entry").
       FD  MY-FILE
           RECORDING MODE IS V
           RECORD IS VARYING IN SIZE
           FROM 10 TO 80 DEPENDING ON WS-LEN.
       01  MY-REC                  PIC X(80).
       WORKING-STORAGE SECTION.
       01  WS-LEN                  PIC 9(4) COMP.
       01  WS-R                    PIC 9    VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           ADD 8 TO ZERO GIVING WS-R.
           CALL "__CPROVER_assert" USING WS-R = 8.
           STOP RUN.
