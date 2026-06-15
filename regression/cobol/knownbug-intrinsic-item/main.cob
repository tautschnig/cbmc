       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-INTRINSIC-ITEM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC X(3) VALUE 'abc'.
       01  WS-U  PIC X(3) VALUE SPACES.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "Intrinsic functions" (UPPER-CASE): the upper-case of 'abc'
      *    is 'ABC'. The frontend computes UPPER-CASE only on string literals;
      *    on an item argument the result is nondeterministic.
           MOVE FUNCTION UPPER-CASE(WS-A) TO WS-U.
           CALL "__CPROVER_assert" USING WS-U = 'ABC'.
           STOP RUN.
