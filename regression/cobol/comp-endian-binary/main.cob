       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-COMP-ENDIAN.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-GRP.
           05  WS-C  PIC 9(4) COMP VALUE 1.
           05  WS-R  REDEFINES WS-C PIC X(2).
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "USAGE BINARY": a COMP halfword is big-endian on z/Architecture,
      *    so the value 1 is bytes 0x00 0x01. The frontend stores binary
      *    little-endian, so the first byte is 0x01.
           IF WS-R(1:1) = X'00' AND WS-R(2:1) = X'01'
               CONTINUE
           ELSE
               CALL "__CPROVER_assert" USING 0 = 1
           END-IF.
           STOP RUN.
