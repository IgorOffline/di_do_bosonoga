grammar Bosonoga;

// The generator checks that there is one bosonoga_main, as its errors name what
// a program needs.
program
    : (constantDeclaration | functionDefinition | topValDeclaration | mainSection)* EOF
    ;

// The generator checks that it is an array, as in the C bosonoga.
topValDeclaration
    : VAL NAME IS initializer
    ;

// The generator checks what follows, as in the C bosonoga, a parameter has its
// type at the end of its name, and a function must say what it returns.
functionDefinition
    : DEF name=NAME parameters+=NAME* (RET results+=NUMBER+ | RET_VOID)? block
    ;

constantDeclaration
    : CONST NAME IS (value | arrayLength)
    ;

mainSection
    : BOSONOGA_MAIN block
    ;

block
    : DI statement+ DO
    ;

statement
    : INFO value+                                                  # infoStatement
    | declaration=(VAR | VAL) NAME IS initializer                  # declareStatement
    | SET NAME setter                                              # setStatement
    | IF condition block (ELSE IF condition block)* (ELSE block)?  # ifStatement
    | WHILE condition block                                        # whileStatement
    | LOOP_COUNT (WITH NAME)? block                                # countStatement
    | BREAK                                                        # breakStatement
    | CONTINUE                                                     # continueStatement
    | SLEEP_MS value                                               # sleepStatement
    | CALL callee=NAME arguments? (WITH RET binding+ block)?       # callStatement
    | UNSET NAME                                                   # unsetStatement
    | NIHIL                                                        # nihilStatement
    | RETURN (status=(EXIT_SUCCESS | EXIT_FAILURE) | value*)       # returnStatement
    ;

// A value that a call returns, taken for the block of the call.
binding
    : NUMBER AS NAME
    ;

initializer
    : operation                                          # operationInitializer
    | callee=NAME arguments? WITH RET result=NUMBER      # callInitializer
    | NEW type=NAME OF value+                            # newInitializer
    | ARRAY types+=NUMBER+ (WITH_LEN_COL value | rows)   # arrayInitializer
    | arrayLength                                        # lengthInitializer
    | value (SHIFT_OR value)*                            # valueInitializer
    ;

setter
    : TO callee=NAME arguments? WITH RET result=NUMBER                               # callSetter
    | TO arrayLength                                                               # lengthSetter
    | AT index=value TO value+                                                     # rowSetter
    | TO value+                                                                    # toSetter
    | accumulation=(TO_ADD | TO_SUBTRACT | TO_MULTIPLY | TO_DIVIDE | TO_MOD) value+  # accumulationSetter
    | operation                                                                    # operationSetter
    ;

// The values of a function after of, or the index of an array after at.
arguments
    : OF value+
    | AT value+
    ;

rows
    : DI row+ DO
    ;

row
    : ROW value*
    ;

// The number of rows, len_col, or of the values of each row, len_row, of an array.
arrayLength
    : count=(LEN_COL | LEN_ROW) NAME
    ;

// A conversion or a math function, each with one or two values.
operation
    : function=(TO_I32 | TO_I64 | TO_U32 | TO_U64 | TO_F32 | TO_F64
        | MATH_TO_ABS | MATH_TO_MIN | MATH_TO_MAX
        | MATH_TO_SIN | MATH_TO_COS | MATH_TO_TAN
        | MATH_TO_ASIN | MATH_TO_ACOS | MATH_TO_ATAN2
        | MATH_TO_SQRT | MATH_TO_POW | MATH_TO_HYPOT
        | MATH_TO_FLOOR | MATH_TO_CEIL | MATH_TO_ROUND
        | MATH_TO_LOG | MATH_TO_LOG10 | MATH_TO_LOG1P
        | MATH_TO_EXP | MATH_TO_EXPM1) value+
    ;

// As in the C bosonoga, and binds tighter than or.
condition
    : conjunction (OR conjunction)*
    ;

conjunction
    : comparison (AND comparison)*
    ;

// A bool is a condition without a comparison too.
comparison
    : NOT? value (compare=(GT | LT | GE | LE | EQ | NOT_EQ
        | EQ_3 | EQ_6 | NOT_EQ_3 | NOT_EQ_6 | SHIFT_HAS) value)?
    ;

value
    : NUMBER                    # numberValue
    | SHIFT_BIT                 # shiftValue
    | (TRUE_BOOL | FALSE_BOOL)  # boolValue
    | STRING                    # stringValue
    | NAME                      # nameValue
    ;

BOSONOGA_MAIN : 'bosonoga_main' ;
DI            : 'di' ;
DO            : 'do' ;
INFO          : 'info' ;
VAR           : 'var' ;
VAL           : 'val' ;
IS            : 'is' ;
SET           : 'set' ;
TO            : 'to' ;
IF            : 'if' ;
ELSE          : 'else' ;
AND           : 'and' ;
OR            : 'or' ;
NOT           : 'not' ;
NIHIL         : 'nihil' ;
RETURN        : 'return' ;
CONST         : 'const' ;
WHILE         : 'while' ;
WITH          : 'with' ;
BREAK         : 'break' ;
CONTINUE      : 'continue' ;
SLEEP_MS      : 'sleep_ms' ;
TRUE_BOOL     : 'true_bool' ;
FALSE_BOOL    : 'false_bool' ;
DEF           : 'def' ;
RET           : 'ret' ;
RET_VOID      : 'ret_void' ;
CALL          : 'call' ;
OF            : 'of' ;
AS            : 'as' ;
NEW           : 'new' ;
UNSET         : 'unset' ;
ARRAY         : 'array' ;
ROW           : 'row' ;
AT            : 'at' ;
WITH_LEN_COL  : 'with_len_col' ;
LEN_COL       : 'len_col' ;
LEN_ROW       : 'len_row' ;
EXIT_SUCCESS  : 'exit_success' ;
EXIT_FAILURE  : 'exit_failure' ;

TO_ADD      : 'to_add' ;
TO_SUBTRACT : 'to_subtract' ;
TO_MULTIPLY : 'to_multiply' ;
TO_DIVIDE   : 'to_divide' ;
TO_MOD      : 'to_mod' ;

TO_I32 : 'to_i32' ;
TO_I64 : 'to_i64' ;
TO_U32 : 'to_u32' ;
TO_U64 : 'to_u64' ;
TO_F32 : 'to_f32' ;
TO_F64 : 'to_f64' ;

MATH_TO_ABS   : 'math_to_abs' ;
MATH_TO_MIN   : 'math_to_min' ;
MATH_TO_MAX   : 'math_to_max' ;
MATH_TO_SIN   : 'math_to_sin' ;
MATH_TO_COS   : 'math_to_cos' ;
MATH_TO_TAN   : 'math_to_tan' ;
MATH_TO_ASIN  : 'math_to_asin' ;
MATH_TO_ACOS  : 'math_to_acos' ;
MATH_TO_ATAN2 : 'math_to_atan2' ;
MATH_TO_SQRT  : 'math_to_sqrt' ;
MATH_TO_POW   : 'math_to_pow' ;
MATH_TO_HYPOT : 'math_to_hypot' ;
MATH_TO_FLOOR : 'math_to_floor' ;
MATH_TO_CEIL  : 'math_to_ceil' ;
MATH_TO_ROUND : 'math_to_round' ;
MATH_TO_LOG   : 'math_to_log' ;
MATH_TO_LOG10 : 'math_to_log10' ;
MATH_TO_LOG1P : 'math_to_log1p' ;
MATH_TO_EXP   : 'math_to_exp' ;
MATH_TO_EXPM1 : 'math_to_expm1' ;

GT       : 'gt' ;
LT       : 'lt' ;
GE       : 'ge' ;
LE       : 'le' ;
EQ       : 'eq' ;
NOT_EQ   : 'not_eq' ;
EQ_3     : 'eq_3' ;
EQ_6     : 'eq_6' ;
NOT_EQ_3 : 'not_eq_3' ;
NOT_EQ_6 : 'not_eq_6' ;

SHIFT_OR  : 'shift_or' ;
SHIFT_HAS : 'shift_has' ;

SHIFT_BIT
    : 'shift_' DIGIT+
    ;

// A loop of a number of iterations, as in to_3.
LOOP_COUNT
    : 'to_' DIGIT+
    ;

// The generator checks the text and the suffix, so that its errors are those of
// the C bosonoga.
STRING
    : '"' ~["\r\n]* '"' [A-Za-z0-9_]*
    ;

// The suffix is checked by the generator, so that its errors can name the types.
NUMBER
    : '-'? DIGIT+ ('d' DIGIT+)? ('_' [A-Za-z0-9_]*)?
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
