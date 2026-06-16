       IDENTIFICATION DIVISION.
       PROGRAM-ID. FLEXP.
      * Floating-point literal in E notation (IBM LR "Floating-point
      * literal"): 1.5E3 = 1500.0. Moved to a COMP-2 item and compared.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-D  USAGE COMP-2.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           MOVE 1.5E3 TO WS-D.
           IF WS-D = 1500
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
