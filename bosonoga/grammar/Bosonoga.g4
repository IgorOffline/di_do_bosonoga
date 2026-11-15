grammar Bosonoga;

program
    : BOSONOGA_MAIN block EOF
    ;

block
    : DI statement+ DO
    ;

statement
    : INFO value+                                  # infoStatement
    | VAR NAME IS value (SHIFT_OR value)*          # varStatement
    | IF condition block                           # ifStatement
    | RETURN status=(EXIT_SUCCESS | EXIT_FAILURE)  # returnStatement
    ;

condition
    : comparison (AND comparison)*
    ;

comparison
    : value SHIFT_HAS value
    ;

value
    : I32_LITERAL  # i32Value
    | SHIFT_BIT    # shiftValue
    | NAME         # nameValue
    ;

BOSONOGA_MAIN : 'bosonoga_main' ;
DI            : 'di' ;
DO            : 'do' ;
INFO          : 'info' ;
VAR           : 'var' ;
IS            : 'is' ;
IF            : 'if' ;
AND           : 'and' ;
RETURN        : 'return' ;
EXIT_SUCCESS  : 'exit_success' ;
EXIT_FAILURE  : 'exit_failure' ;
SHIFT_OR      : 'shift_or' ;
SHIFT_HAS     : 'shift_has' ;

SHIFT_BIT
    : 'shift_' DIGIT+
    ;

I32_LITERAL
    : '-'? DIGIT+ '_i32'
    ;

NAME
    : [A-Za-z_] [A-Za-z0-9_]*
    ;

LINE_COMMENT
    : '//' ~[\r\n]* -> skip
    ;

WHITESPACE
    : [ \t\r\n]+ -> skip
    ;

fragment DIGIT
    : [0-9]
    ;
