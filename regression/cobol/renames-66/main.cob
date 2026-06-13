       IDENTIFICATION DIVISION.
       PROGRAM-ID. RENAMES-66.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-REC.
           05  WS-A  PIC X(2) VALUE 'AB'.
           05  WS-B  PIC X(2) VALUE 'CD'.
           05  WS-C  PIC X(2) VALUE 'EF'.
      *    WS-AB aliases the storage of WS-A through WS-B (IBM LR "RENAMES
      *    clause").
       66  WS-AB RENAMES WS-A THRU WS-B.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF WS-AB = 'ABCD'
               MOVE 1 TO WS-R
           END-IF.
      *    Writing through the alias is visible in the renamed items.
           MOVE 'WXYZ' TO WS-AB.
           IF WS-A = 'WX' AND WS-B = 'YZ' AND WS-C = 'EF'
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 2.
           STOP RUN.
