       IDENTIFICATION DIVISION.
       PROGRAM-ID. REFMODCM.
      * IBM LR "Comparison of two alphanumeric operands": a reference
      * modification with a non-constant length contributes its first `len`
      * characters; positions at or beyond the run-time length are space
      * padding. So WS-SRC(1:3) compares equal to "HEL" (and to "HEL  ")
      * but not to "HELLO" (I8, comparison consumer). The length is held in
      * a data item, so the effective length is the run-time value.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-SRC  PIC X(5) VALUE 'HELLO'.
       01  WS-L3   PIC 9 VALUE 3.
       01  WS-L4   PIC 9 VALUE 4.
       01  WS-R    PIC 9(3) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF WS-SRC(1:WS-L3) = "HEL"
               ADD 1 TO WS-R
           END-IF.
           IF WS-SRC(1:WS-L3) = "HELLO"
               ADD 10 TO WS-R
           END-IF.
           IF WS-SRC(1:WS-L4) = "HELL"
               ADD 100 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 101.
           STOP RUN.
