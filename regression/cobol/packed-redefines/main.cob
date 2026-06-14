       IDENTIFICATION DIVISION.
       PROGRAM-ID. PACKED-REDEFINES.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-GRP.
           05  WS-NUM  PIC 9(3) COMP-3 VALUE 123.
           05  WS-RED  REDEFINES WS-NUM PIC X(2).
       01  WS-GRP2.
           05  WS-SNUM PIC S9(3) COMP-3 VALUE -45.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    A COMP-3 field aliased by alphanumeric uses faithful packed-decimal
      *    bytes (IBM LR "USAGE PACKED-DECIMAL"): 123 in 2 bytes is 0x12 0x3F
      *    (two digits per byte; last nibble 0xF = unsigned sign).
           IF WS-RED(1:1) = X'12' AND WS-RED(2:1) = X'3F'
               CONTINUE
           ELSE
               CALL "__CPROVER_assert" USING 0 = 1
           END-IF.
           CALL "__CPROVER_assert" USING WS-NUM = 123.
           ADD 100 TO WS-NUM.
           CALL "__CPROVER_assert" USING WS-NUM = 223.
      *    Signed packed: -45 round-trips through the value domain.
           CALL "__CPROVER_assert" USING WS-SNUM = -45.
           STOP RUN.
