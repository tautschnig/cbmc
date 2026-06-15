       IDENTIFICATION DIVISION.
       PROGRAM-ID. INSPECT-TALLYING.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S    PIC X(5) VALUE 'AABAA'.
       01  WS-ALL  PIC 9(2) VALUE 0.
       01  WS-LEAD PIC 9(2) VALUE 0.
       01  WS-CHRS PIC 9(2) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "INSPECT statement", TALLYING: counting over the inspected
      *    item's bytes is exact for these common forms. In 'AABAA':
      *    ALL 'A' = 4, LEADING 'A' = 2, CHARACTERS = 5.
           INSPECT WS-S TALLYING WS-ALL FOR ALL 'A'.
           INSPECT WS-S TALLYING WS-LEAD FOR LEADING 'A'.
           INSPECT WS-S TALLYING WS-CHRS FOR CHARACTERS.
           CALL "__CPROVER_assert" USING WS-ALL = 4.
           CALL "__CPROVER_assert" USING WS-LEAD = 2.
           CALL "__CPROVER_assert" USING WS-CHRS = 5.
           STOP RUN.
