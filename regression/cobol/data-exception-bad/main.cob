       IDENTIFICATION DIVISION.
       PROGRAM-ID. DATAEXC.
      * IBM LR "Class condition" / z/OS data exception (S0C7): using a
      * zoned DISPLAY item that holds a non-digit byte (here loaded via a
      * REDEFINES alphanumeric alias) in an arithmetic operation abends.
      * With --cobol-data-exception-check the cobol:numeric property flags it.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-G.
           05  WS-N  PIC 9(3).
           05  WS-A  REDEFINES WS-N PIC X(3).
       01  WS-R  PIC 9(5).
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE "12A" TO WS-A.
           COMPUTE WS-R = WS-N + 1.
           STOP RUN.
