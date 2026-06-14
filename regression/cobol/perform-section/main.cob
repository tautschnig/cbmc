       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORM-SECTION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 9 VALUE 0.
       01  WS-B  PIC 9 VALUE 0.
       01  WS-C  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    PERFORM of a section-name runs every paragraph of the section, not
      *    just the section header's own statements (IBM LR "PERFORM
      *    statement"). WORK-SECTION contains WORK-A and WORK-B, so both run;
      *    OTHER-SECTION's WS-C must stay 0.
           PERFORM WORK-SECTION.
           CALL "__CPROVER_assert" USING WS-A = 1.
           CALL "__CPROVER_assert" USING WS-B = 1.
           CALL "__CPROVER_assert" USING WS-C = 0.
           STOP RUN.
       WORK-SECTION SECTION.
       WORK-A.
           MOVE 1 TO WS-A.
       WORK-B.
           MOVE 1 TO WS-B.
       OTHER-SECTION SECTION.
       OTHER-A.
           MOVE 1 TO WS-C.
