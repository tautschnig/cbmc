       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRING-CONCAT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-OUT  PIC X(4) VALUE SPACES.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "STRING statement": concatenating 'AB' and 'CD' DELIMITED BY
      *    SIZE yields 'ABCD'.
           STRING 'AB' 'CD' DELIMITED BY SIZE INTO WS-OUT.
           CALL "__CPROVER_assert" USING WS-OUT = 'ABCD'.
           STOP RUN.
