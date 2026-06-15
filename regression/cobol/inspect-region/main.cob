       IDENTIFICATION DIVISION.
       PROGRAM-ID. INSPECT-REGION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S    PIC X(9) VALUE 'AB=CADCAE'.
       01  WS-T    PIC X(9) VALUE 'XX.YY.ZZ.'.
       01  WS-R    PIC X(7) VALUE 'A,B=C,D'.
       01  WS-CA   PIC 9(2) VALUE 0.
       01  WS-CB   PIC 9(2) VALUE 0.
       01  WS-OK   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "INSPECT statement" BEFORE/AFTER INITIAL: the operation is
      *    limited to the region before/after the first delimiter occurrence.
      *    In 'AB=CADCAE', after '=' there are two 'A's. In 'XX.YY.ZZ.', two
      *    characters precede the first '.'. REPLACING ',' after '=' touches
      *    only the comma in 'C,D'.
           INSPECT WS-S TALLYING WS-CA FOR ALL 'A' AFTER INITIAL '='.
           INSPECT WS-T TALLYING WS-CB
               FOR CHARACTERS BEFORE INITIAL '.'.
           INSPECT WS-R REPLACING ALL ',' BY ';' AFTER INITIAL '='.
           IF WS-CA = 2 AND WS-CB = 2 AND WS-R = 'A,B=C;D'
               MOVE 1 TO WS-OK
           END-IF.
           CALL "__CPROVER_assert" USING WS-OK = 1.
           STOP RUN.
