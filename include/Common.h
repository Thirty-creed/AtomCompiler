#ifndef COMMON_H
#define COMMON_H

namespace ATC {
enum NodeType {
    ID_COMP_UNIT,
    
    ID_BASIC_TYPE,
    ID_ARRAY_TYPE,
    ID_POINTER_TYPE,

    ID_VAR_DECL,
    ID_FUNCTION_DEF,
    ID_VARIABLE,

    // expression
    ID_CONST_VAL,
    ID_VAR_REF,
    ID_INDEXED_REF,
    ID_NESTED_EXPRESSION,
    ID_UNARY_EXPRESSION,
    ID_BINARY_EXPRESSION,
    ID_FUNCTION_CALL,

    // statement
    ID_BLOCK,
    ID_ASSIGN_STATEMENT,
    ID_BLANK_STATEMENT,
    ID_IF_STATEMENT,
    ID_ELSE_STATEMENT,
    ID_WHILE_STATEMENT,
    ID_BREAK_STATEMENT,
    ID_CONTINUE_STATEMENT,
    ID_RETURN_STATEMENT,
    ID_OTHER_STATEMENT
};

enum Operator { PLUS, MINUS, NOT, MUL, DIV, MOD, LT, GT, LE, GE, EQ, NE, AND, OR };
}  // namespace ATC
#endif