       IDENTIFICATION DIVISION.
       PROGRAM-ID. ALNUM-COND.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-FLAG  PIC X VALUE 'Y'.
           88  IS-YES   VALUE 'Y'.
           88  IS-NO    VALUE 'N'.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF IS-YES
               MOVE 1 TO WS-R
           ELSE
               MOVE 0 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
