       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUM-ALNUM-COMPARE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(3) VALUE 123.
       01  WS-A  PIC X(3) VALUE '123'.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "Comparison of numeric and nonnumeric operands": the numeric
      *    operand is compared by its zoned display representation, so the
      *    unsigned integer 123 equals the characters '123'.
           IF WS-N = WS-A
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
