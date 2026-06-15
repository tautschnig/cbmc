       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRING-DELIMITED.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-NAME  PIC X(8) VALUE 'JOHN    '.
       01  WS-A     PIC X(6) VALUE 'AB::CD'.
       01  WS-R1    PIC X(10) VALUE SPACES.
       01  WS-R2    PIC X(6)  VALUE SPACES.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "STRING statement": each sender is transferred up to its
      *    DELIMITED BY delimiter. 'JOHN    ' DELIMITED BY ' ' is 'JOHN';
      *    'AB::CD' DELIMITED BY '::' is 'AB'.
           STRING WS-NAME DELIMITED BY ' '
                  '-X' DELIMITED BY SIZE
                  INTO WS-R1.
           STRING WS-A DELIMITED BY '::' INTO WS-R2.
           CALL "__CPROVER_assert" USING WS-R1 = 'JOHN-X    '.
           CALL "__CPROVER_assert" USING WS-R2 = 'AB    '.
           STOP RUN.
