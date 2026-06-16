       IDENTIFICATION DIVISION.
       PROGRAM-ID. DIVBAD.
      * Division by zero with no ON SIZE ERROR phrase: a zero divisor is a
      * runtime fault (z/OS S0CB / decimal-divide), so cobol:division-by-zero
      * must report the violation. IBM LR "COMPUTE statement" / "SIZE ERROR
      * phrases".
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9(4) VALUE 100.
       01  WS-Z  PIC 9    VALUE 0.
       01  WS-R  PIC 9(4).
       PROCEDURE DIVISION.
       MAIN-PARA.
           COMPUTE WS-R = WS-A / WS-Z.
           STOP RUN.
