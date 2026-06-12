       IDENTIFICATION DIVISION.
       PROGRAM-ID. COND-SUBSCRIPT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TBL.
           05  WS-ENT OCCURS 3 TIMES.
               10  WS-FLG  PIC X.
                   88  ENT-OK   VALUE 'Y'.
       01  WS-R    PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    A condition-name whose conditional variable is a table element is
      *    referenced with a subscript (IBM LR "Condition-name").
           MOVE 'Y' TO WS-FLG(2).
           IF ENT-OK(2)
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
