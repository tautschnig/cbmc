       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRDYN.
      * IBM LR "STRING statement": a sending operand that is a reference
      * modification with a non-constant length contributes exactly that
      * many characters. STRING WS-A(1:3) ("HEL") then "X" into WS-Y gives
      * "HELX" left-justified, the rest of WS-Y unchanged (here SPACES) (I8).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A   PIC X(5) VALUE 'HELLO'.
       01  WS-L   PIC 9 VALUE 3.
       01  WS-Y   PIC X(6) VALUE SPACES.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           STRING WS-A(1:WS-L) DELIMITED BY SIZE
                  "X" DELIMITED BY SIZE INTO WS-Y.
           IF WS-Y = "HELX  "
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
