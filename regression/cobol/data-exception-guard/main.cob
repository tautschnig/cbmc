       IDENTIFICATION DIVISION.
       PROGRAM-ID. DATAEXCG.
      * The IS NUMERIC guard discharges the data-exception check: inside the
      * THEN branch the content is known valid, so the arithmetic use is safe.
      * The class condition's own operand is not subjected to the check (that
      * test is precisely the validation). IBM LR "Class condition".
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-G.
           05  WS-N  PIC 9(3).
           05  WS-A  REDEFINES WS-N PIC X(3).
       01  WS-R  PIC 9(5) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE "12A" TO WS-A.
           IF WS-N IS NUMERIC
               COMPUTE WS-R = WS-N + 1
           END-IF.
           STOP RUN.
