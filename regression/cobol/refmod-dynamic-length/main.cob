       IDENTIFICATION DIVISION.
       PROGRAM-ID. REFMODYN.
      * IBM LR "Reference modification": a reference modification with a
      * non-constant length selects `len` characters at run time. A group
      * MOVE of such a sender copies `len` characters and space-fills the
      * receiver. The length is held in a data item (a non-constant
      * expression), so the copy count tracks the run-time value rather than
      * the over-approximated item size (I8). See
      * doc/architectural/cobol-precision-and-gaps-plan.md section 6.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-SRC   PIC X(5) VALUE 'HELLO'.
       01  WS-L3    PIC 9 VALUE 3.
       01  WS-L4    PIC 9 VALUE 4.
       01  WS-D3    PIC X(5) VALUE SPACES.
       01  WS-D4    PIC X(5) VALUE SPACES.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE WS-SRC(1:WS-L3) TO WS-D3.
           MOVE WS-SRC(1:WS-L4) TO WS-D4.
           IF WS-D3 = "HEL  " AND WS-D4 = "HELL "
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
