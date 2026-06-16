       IDENTIFICATION DIVISION.
       PROGRAM-ID. COMP2AR.
      * COMP-2 floating-point arithmetic and conversion (IBM LR "USAGE
      * clause"; "MOVE statement" fixed<->float conversion). A fixed literal
      * 7 moves to the float as 7.0; 7.0 / 2 = 3.5 in IEEE double; moving the
      * float to a fixed receiver truncates toward zero -> 3.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-D  USAGE COMP-2.
       01  WS-R  PIC 9(4).
       PROCEDURE DIVISION.
       MAIN.
           MOVE 7 TO WS-D.
           COMPUTE WS-D = WS-D / 2.
           MOVE WS-D TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
