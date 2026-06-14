       IDENTIFICATION DIVISION.
       PROGRAM-ID. EXIT-PARA-SECTION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9 VALUE 0.
       01  WS-B  PIC 9 VALUE 0.
       01  WS-C  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM WORK-SECTION.
      *    EXIT PARAGRAPH skips the rest of W1 (WS-A stays 1); EXIT SECTION
      *    skips the rest of the section, so W2's second MOVE and all of W3 do
      *    not run (WS-B stays 1, WS-C stays 0). See IBM LR "EXIT statement".
           CALL "__CPROVER_assert" USING WS-A = 1.
           CALL "__CPROVER_assert" USING WS-B = 1.
           CALL "__CPROVER_assert" USING WS-C = 0.
           STOP RUN.
       WORK-SECTION SECTION.
       W1.
           MOVE 1 TO WS-A.
           EXIT PARAGRAPH.
           MOVE 9 TO WS-A.
       W2.
           MOVE 1 TO WS-B.
           EXIT SECTION.
           MOVE 9 TO WS-B.
       W3.
           MOVE 9 TO WS-C.
       DONE-SECTION SECTION.
       D1.
           CONTINUE.
