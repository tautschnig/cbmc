       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-GROUP-MOVE-PAD.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-SRC.
           05  FILLER PIC X(2) VALUE 'AB'.
       01  WS-DST.
           05  FILLER PIC X(4) VALUE 'ZZZZ'.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "MOVE statement" (group/alphanumeric): the receiver is filled
      *    to its full length, space-padding on the right, so WS-DST = 'AB  '.
      *    The group MOVE copies only min(sizes) bytes with no padding.
           MOVE WS-SRC TO WS-DST.
           CALL "__CPROVER_assert" USING WS-DST = 'AB  '.
           STOP RUN.
