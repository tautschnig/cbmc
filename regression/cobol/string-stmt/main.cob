       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRING-STMT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A    PIC X(3) VALUE "AAA".
       01  WS-B    PIC X(3) VALUE "BBB".
       01  WS-OUT  PIC X(6).
       01  WS-R    PIC 9    VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    STRING concatenates the sending fields into WS-OUT (IBM LR "STRING
      *    statement"); the receiver's exact content is not modelled.
           STRING WS-A DELIMITED BY SIZE
                  WS-B DELIMITED BY SIZE
             INTO WS-OUT
           END-STRING.
           MOVE 7 TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 7.
           STOP RUN.
