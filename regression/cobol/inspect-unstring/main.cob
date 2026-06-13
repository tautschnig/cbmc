       IDENTIFICATION DIVISION.
       PROGRAM-ID. INSPECT-UNSTRING.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TXT  PIC X(10) VALUE 'AAA,BBB,CC'.
       01  WS-CT   PIC 9(2)  VALUE 0.
       01  WS-A    PIC X(3).
       01  WS-B    PIC X(3).
       01  WS-C    PIC X(3).
       01  WS-R    PIC 9     VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    These character-level verbs parse and lower; their written items
      *    are over-approximated (IBM LR "INSPECT"/"UNSTRING statement").
           INSPECT WS-TXT TALLYING WS-CT FOR ALL 'A'.
           INSPECT WS-TXT REPLACING ALL 'A' BY 'X'.
           UNSTRING WS-TXT DELIMITED BY ','
               INTO WS-A WS-B WS-C
           END-UNSTRING.
           MOVE 5 TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 5.
           STOP RUN.
