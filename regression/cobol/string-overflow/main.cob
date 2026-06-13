       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRING-OVERFLOW.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-OUT   PIC X(2).
       01  WS-FLAG  PIC 9 VALUE 5.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Whether STRING overflows is nondeterministic, so both phrases are
      *    reachable (IBM LR "STRING statement", ON OVERFLOW phrase).
           STRING 'ABCDEF' DELIMITED BY SIZE
               INTO WS-OUT
               ON OVERFLOW MOVE 1 TO WS-FLAG
               NOT ON OVERFLOW MOVE 2 TO WS-FLAG
           END-STRING.
      *    WS-FLAG is 1 (overflow) or 2 (no overflow).
           CALL "__CPROVER_assert" USING WS-FLAG > 0.
           CALL "__CPROVER_assert" USING WS-FLAG < 3.
           STOP RUN.
