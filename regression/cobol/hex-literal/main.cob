       IDENTIFICATION DIVISION.
       PROGRAM-ID. HEX-LITERAL.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
      *    X'4142' is the two bytes 0x41 0x42, i.e. "AB" (IBM LR
      *    "Hexadecimal-alphanumeric literals").
       01  WS-H   PIC X(2) VALUE X'4142'.
       01  WS-R   PIC 9    VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF WS-H = "AB"
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
