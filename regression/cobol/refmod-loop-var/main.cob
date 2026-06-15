       IDENTIFICATION DIVISION.
       PROGRAM-ID. REFLOOP.
      * The UNTIL condition reference-modifies WS-S by the loop variable
      * WS-IDX, which ranges over 5..1 (always in 1..5). The refmod-range
      * check must be evaluated with WS-IDX's per-iteration value (inside
      * the loop), not its pre-loop value, so it holds. This mirrors the
      * AWS CardDemo "scan trailing blanks" idiom (e.g. COADM01C) and
      * exercises the loop-condition check placement
      * (doc/architectural/cobol-runtime-checks.md).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S    PIC X(5) VALUE 'HELLO'.
       01  WS-IDX  PIC 9.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM VARYING WS-IDX FROM 5 BY -1
                   UNTIL WS-S(WS-IDX:1) NOT = SPACE OR WS-IDX = 1
           END-PERFORM.
           STOP RUN.
