      ******************************************************************
      * DCLGEN-style member: a DECLARE TABLE directive followed by the
      * host-variable structure. Resolved via the .dcl extension.
      ******************************************************************
           EXEC SQL DECLARE MYSCHEMA.MYTABLE TABLE
           ( FOO                            DECIMAL(1) NOT NULL,
             BAR                            CHAR(3) NOT NULL
           ) END-EXEC.
       01  DCLMYREC.
           10  DCL-FOO   PIC 9.
           10  DCL-BAR   PIC X(3).
