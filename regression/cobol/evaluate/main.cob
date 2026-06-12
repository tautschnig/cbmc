       IDENTIFICATION DIVISION.
       PROGRAM-ID. EVALUATE-TEST.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-G    PIC 9 VALUE 2.
       01  WS-R    PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           EVALUATE WS-G
               WHEN 1
                   MOVE 1 TO WS-R
               WHEN 2
                   MOVE 2 TO WS-R
               WHEN OTHER
                   MOVE 9 TO WS-R
           END-EVALUATE.
           CALL "__CPROVER_assert" USING WS-R = 2.
           STOP RUN.
