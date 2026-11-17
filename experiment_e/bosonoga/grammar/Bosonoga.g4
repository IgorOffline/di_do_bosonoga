grammar Bosonoga;

// ImportingTokenSource puts FILE_START/FILE_END around a module's items, and
// RAW_START before those of a raw import, whose end is transparent. CUT and
// VALUE_CUT are where Generator cuts a program at its syntax error, so that
// the errors before it come first, as the C bosonoga finds them.
tokens { FILE_START, FILE_END, RAW_START, CUT, VALUE_CUT }

// The generator checks that there is one bosonoga_main, and that
// bosonoga_global is before it, as their errors name what a program needs.
program
    : item* EOF
    ;

// Other is what cannot start an item, for the error of the C bosonoga.
item
    : constantDeclaration
    | functionDefinition
    | topValDeclaration
    | globalSection
    | mainSection
    | gfxSection
    | fileItem
    | pubItem
    | CUT
    | other=~(CONST | DEF | VAL | BOSONOGA_GLOBAL | BOSONOGA_MAIN | MOD | IO_RAW_IMPORT | PUB
        | GFX_INIT | GFX_FRAME | GFX_CLEANUP | GFX_KEY_DOWN | GFX_MAIN | FILE_START | FILE_END | CUT)
    ;

// Without FILE_START or RAW_START, the file is not read; the generator says why.
fileItem
    : keyword=(MOD | IO_RAW_IMPORT) value? (FILE_START item* FILE_END | RAW_START)?
    ;

pubItem
    : PUB (constantDeclaration | functionDefinition | topValDeclaration)?
    ;

// The generator checks that it is an array, as in the C bosonoga.
topValDeclaration
    : VAL NAME IS initializer
    ;

// The generator checks what follows, as in the C bosonoga, a parameter has its
// type at the end of its name, and a function must say what it returns.
functionDefinition
    : DEF name=NAME parameters+=(NAME | RESULT_NAME)* (RET results+=(NUMBER | INDEXED_TYPE)+ | RET_VOID)? block
    ;

constantDeclaration
    : CONST NAME IS (value | arrayLength)
    ;

// It runs before bosonoga_main, which sees the variables at its top.
globalSection
    : BOSONOGA_GLOBAL block
    ;

mainSection
    : BOSONOGA_MAIN block
    ;

gfxSection
    : section=(GFX_INIT | GFX_FRAME | GFX_CLEANUP | GFX_KEY_DOWN | GFX_MAIN) block
    ;

block
@init { BlockNesting.check(this); }
    : DI statement+ DO
    | CUT
    ;

statement
    : INFO value+                                                  # infoStatement
    | graphics=(GFX_CLEAR | GFX_DRAW_RECTANGLE | GFX_DRAW_HEX | GFX_DRAW_CIRCLE
        | GFX_DRAW_TEXT | GFX_DRAW_ICON | GFX_AWAIT_NEXT_FRAME) value* # gfxStatement
    | declaration=(VAR | VAL) NAME (IS initializer | CUT)          # declareStatement
    | SET NAME (setter | CUT)                                      # setStatement
    | IF condition block (ELSE IF condition block)* (ELSE block)?  # ifStatement
    | WHILE condition block                                        # whileStatement
    | LOOP_COUNT (WITH NAME)? block                                # countStatement
    | BREAK                                                        # breakStatement
    | CONTINUE                                                     # continueStatement
    | SLEEP_MS value                                               # sleepStatement
    | CALL callee=NAME arguments? taking?                          # callStatement
    | given=(IO_READ | IO_WRITE | IO_MODIFIED | XML_CHILD | XML_ATTRIBUTE | STR_PARSE) value*
        taking?                                                    # givenStatement
    | UNSET NAME                                                   # unsetStatement
    | NIHIL                                                        # nihilStatement
    | RETURN (status=(EXIT_SUCCESS | EXIT_FAILURE) | value*)       # returnStatement
    | PUB statement?                                               # pubStatement
    | CUT                                                          # cutStatement
    ;

// The one value of a call that var and set ... to take, with ret 0_i32.
takeOne
    : WITH (RET (result=(NUMBER | INDEXED_TYPE) | CUT)? | CUT)?
    | CUT
    ;

// The values that a call returns, or that a statement such as xml_child
// gives, taken for its block, with ret 0_bool as done di ... do.
taking
    : WITH (RET (binding+ block? | CUT)? | CUT)?
    ;

binding
    : position=(NUMBER | INDEXED_TYPE) (AS NAME? | CUT)?
    ;

initializer
    : operation                                          # operationInitializer
    | callee=NAME arguments? takeOne?                    # callInitializer
    | NEW type=NAME heads+=value* (OF values+=value*)?   # newInitializer
    | ARRAY types+=(NUMBER | INDEXED_TYPE)+ (WITH_LEN_COL value | rows | CUT)  # arrayInitializer
    | arrayLength                                        # lengthInitializer
    | value orFlag*                                      # valueInitializer
    ;

setter
    : TO assigned                                                                  # assignSetter
    | AT index=value TO value*                                                     # rowSetter
    | TO_JOIN value*                                                                 # joinSetter
    | accumulation=(TO_ADD | TO_SUBTRACT | TO_MULTIPLY | TO_DIVIDE | TO_MOD) value+  # accumulationSetter
    | operation                                                                    # operationSetter
    ;

// What set ... to sets, read after to, as the C bosonoga does.
assigned
    : callee=NAME arguments? takeOne?  # callSetter
    | arrayLength                      # lengthSetter
    | value+                           # toSetter
    ;

// The values of a function after of, or the index of an array after at.
arguments
    : OF value*
    | AT value*
    ;

rows
    : DI row+ (DO | CUT)
    ;

// Flags joined to the flags before, as in read_flag shift_or write_flag.
orFlag
    : SHIFT_OR value?
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
        | MATH_TO_ABS
        | MATH_TO_SIN | MATH_TO_COS | MATH_TO_TAN
        | MATH_TO_ASIN | MATH_TO_ACOS
        | MATH_TO_SQRT
        | MATH_TO_FLOOR | MATH_TO_CEIL | MATH_TO_ROUND
        | MATH_TO_LOG | MATH_TO_LOG10 | MATH_TO_LOG1P
        | MATH_TO_EXP | MATH_TO_EXPM1) value
    | function=(MATH_TO_MIN | MATH_TO_MAX | MATH_TO_ATAN2 | MATH_TO_POW | MATH_TO_HYPOT) value value
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
    | special=(GFX_DELTA_TIME | GFX_KEY) # gfxValue
    | SHIFT_BIT                 # shiftValue
    | (TRUE_BOOL | FALSE_BOOL)  # boolValue
    | STRING                    # stringValue
    | NAME                      # nameValue
    | RESULT_NAME               # resultValue
    | VALUE_CUT                 # cutValue
    ;

BOSONOGA_MAIN   : 'bosonoga_main' ;
GFX_INIT : 'gfx_init' ;
GFX_FRAME : 'gfx_frame' ;
GFX_CLEANUP : 'gfx_cleanup' ;
GFX_KEY_DOWN : 'gfx_key_down' ;
GFX_MAIN : 'gfx_main' ;
GFX_CLEAR : 'gfx_clear' ;
GFX_DRAW_RECTANGLE : 'gfx_draw_rectangle' ;
GFX_DRAW_HEX : 'gfx_draw_hex' ;
GFX_DRAW_CIRCLE : 'gfx_draw_circle' ;
GFX_DRAW_TEXT : 'gfx_draw_text' ;
GFX_DRAW_ICON : 'gfx_draw_icon' ;
GFX_AWAIT_NEXT_FRAME : 'gfx_await_next_frame' ;
GFX_DELTA_TIME : 'gfx_delta_time' ;
GFX_KEY : 'gfx_key' ;
MOD           : 'mod' ;
IO_RAW_IMPORT : 'io_raw_import' ;
PUB           : 'pub' ;
BOSONOGA_GLOBAL : 'bosonoga_global' ;
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
EXPECT        : 'expect' ;
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
TO_JOIN     : 'to_join' ;

IO_READ       : 'io_read' ;
IO_WRITE      : 'io_write' ;
IO_MODIFIED   : 'io_modified' ;
XML_CHILD     : 'xml_child' ;
XML_ATTRIBUTE : 'xml_attribute' ;
STR_PARSE     : 'str_parse' ;

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

// Only LF ends the line of a string, so a CR is its text. The generator checks
// the text and the suffix, for the errors of the C bosonoga.
STRING
    : '"' (~["\n\\] | '\\' ~[\n])* '"' [A-Za-z0-9_]*
    ;

// Is an error, which ImportingTokenSource throws as it reads it.
UNTERMINATED_STRING
    : '"' (~["\n\\] | '\\' ~[\n])* '\\'?
    ;

// An index with a type that is not a number, as 1_str. With a number type, as
// 0_i32, it is a NUMBER, which is both a literal and an indexed type.
INDEXED_TYPE
    : DIGIT+ '_' ('flags' | 'color' | 'str' | 'bool' | 'input_key' | 'font' | 'icon' | 'xml_str' | 'dir')
    ;

// The generator checks the text as the C bosonoga reads it: the digits, a
// fraction after d, and the suffix, or a . that is an error.
NUMBER
    : '-'? DIGIT+ ('.' | [A-Za-z0-9_]*)
    ;

// The old ret_0 placeholders are reserved, even with more after the digit.
// They are values only so that their use is diagnosed.
RESULT_NAME
    : 'ret_' DIGIT [A-Za-z0-9_]*
    ;

NAME
    : [A-Za-z_] [A-Za-z0-9_]*
    ;

// As in the C tokenizer, a comment ends only at LF, and g_ascii_isspace
// includes vertical tab and form feed as token separators.
LINE_COMMENT
    : '//' ~[\n]* -> skip
    ;

WHITESPACE
    : [ \t\r\n\u000B\u000C]+ -> skip
    ;

fragment DIGIT
    : [0-9]
    ;
