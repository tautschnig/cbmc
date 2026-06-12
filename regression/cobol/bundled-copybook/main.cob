       IDENTIFICATION DIVISION.
       PROGRAM-ID. BUNDLED-COPYBOOK.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
      *    None of these copybooks are on the search path; they come from the
      *    bundled copybook library (cobol_copybooks.h) via the COPY fallback.
      *    CMQODV is a field group included under a program-provided 01;
      *    CMQV and DFHBMSCA are self-contained.
       01  MQ-OBJECT-DESCRIPTOR.
           COPY CMQODV.
       COPY CMQV.
       COPY DFHBMSCA.
       01  WS-ATTR  PIC X.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    MQOT-Q is the bundled MQ constant 1; MQOD-OBJECTTYPE is a field of
      *    the bundled MQOD structure; DFHRED is a bundled BMS attribute byte.
           MOVE MQOT-Q  TO MQOD-OBJECTTYPE.
           MOVE DFHRED  TO WS-ATTR.
           IF MQOD-OBJECTTYPE = 1
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
