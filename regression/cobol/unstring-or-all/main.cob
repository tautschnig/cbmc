       IDENTIFICATION DIVISION.
       PROGRAM-ID. UNSTRING-OR-ALL.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A   PIC X(8) VALUE 'AB,CD;EF'.
       01  WS-B   PIC X(10) VALUE 'AB   CD  E '.
       01  WS-1   PIC X(3) VALUE SPACES.
       01  WS-2   PIC X(3) VALUE SPACES.
       01  WS-3   PIC X(3) VALUE SPACES.
       01  WS-4   PIC X(3) VALUE SPACES.
       01  WS-OK  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "UNSTRING statement": OR gives alternative delimiters (the
      *    field ends at the first of any), ALL collapses consecutive
      *    delimiters into one. 'AB,CD;EF' splits on ',' or ';' into AB/CD/EF;
      *    'AB   CD  E ' splits on ALL ' ' into AB/CD (runs of spaces collapse).
           UNSTRING WS-A DELIMITED BY ',' OR ';' INTO WS-1 WS-2 WS-3.
           UNSTRING WS-B DELIMITED BY ALL ' ' INTO WS-4 WS-2.
           IF WS-1 = 'AB ' AND WS-2 = 'CD ' AND WS-3 = 'EF '
              AND WS-4 = 'AB '
               MOVE 1 TO WS-OK
           END-IF.
           CALL "__CPROVER_assert" USING WS-OK = 1.
           STOP RUN.
