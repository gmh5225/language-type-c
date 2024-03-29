//
// Created by praisethemoon on 28.04.23.
//

#include "error.h"
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <printf.h>
#include "../utils/vec.h"
#include "parser.h"
#include "parser_resolve.h"
#include "ast.h"
#include "error.h"
#include "tokens.h"
#include "ast_json.h"

#define TTTS(t) token_type_to_string(t)

void parser_assert(int cond, const char * rawcond, const char* func_name, int line, const char * fmt, ...) {
    if (cond)
        return;
    char temp[1024];
    va_list vl;
    va_start(vl, fmt);
    vsprintf(temp, fmt, vl);
    va_end(vl);
    fprintf(stdout, "Fatal error, assertion failed: `%s` in function `%s`, line %d \n", rawcond, func_name, line);
    fprintf(stdout, "%s", temp);
    fprintf(stdout, "\n");
}

char* extractLine(Parser* parser, Lexeme lexeme){
    uint32_t token_len = lexeme.type != TOK_IDENTIFIER? strlen(TTTS(lexeme.type)) : strlen(lexeme.string);
    char line[512] = {0};
    uint32_t lineIndex1 = lexeme.pos;
    // find new line pre pos:
    while (lineIndex1 > 0 && parser->lexerState->buffer[lineIndex1-1] != '\n') {
        lineIndex1--;
    }
    // find new line post pos:
    uint32_t lineIndex2 = lexeme.pos;
    while (lineIndex2 < parser->lexerState->len && parser->lexerState->buffer[lineIndex2] != '\n') {
        lineIndex2++;
    }
    // create new string
    uint32_t lineLength = lineIndex2 - lineIndex1;
    strncpy(line, parser->lexerState->buffer + lineIndex1, lineLength);
    // add new line
    line[lineLength] = '\n';
    // now we add spaces from new line until pos relative to line
    uint32_t spaces = lexeme.pos - lineIndex1;
    for (uint32_t i = 0; i < spaces; i++) {
        line[lineLength+i+1] = ' ';
    }
    // add ^ equal to token length
    for (uint32_t i = 0; i < token_len; i++) {
        line[lineLength+spaces+i+1] = '^';
    }
    line[lineLength+token_len+spaces+3] = '\0';


    //line[lineLength+spaces+1] = '^';
    //line[lineLength+spaces+2] = '\0';
    return strdup(line);
}

#define ACCEPT parser_accept(parser)
#define CURRENT lexeme = parser_peek(parser)
#define EXPAND_LEXEME lexeme.line, lexeme.col, TTTS(lexeme.type)
#define PARSER_ASSERT(c, msg, ...) {                            \
char* ____track____2455 = c?"":extractLine(parser, lexeme);      \
parser_assert(c, #c, __FUNCTION_NAME__, __LINE__ , msg, ##__VA_ARGS__);\
if(!c)fprintf(stdout, "%s\n", ____track____2455);                     \
assert(c);}

Parser* parser_init(LexerState* lexerState) {
    Parser* parser = malloc(sizeof(Parser));
    parser->lexerState = lexerState;
    vec_init(&parser->stack);
    parser->stack_index = 0;
    return parser;
}

Lexeme parser_peek(Parser* parser) {
    if (parser->stack_index == parser->stack.length) {
        Lexeme lexeme = lexer_lexCurrent(parser->lexerState);
        vec_push(&parser->stack, lexeme);
        parser->stack_index++;
        return lexeme;
    }
    else {
        parser->stack_index++;
        return parser->stack.data[parser->stack_index-1];
    }
}

void parser_accept(Parser* parser) {
    // TODO: free lexeme's str value
    vec_splice(&parser->stack, 0, parser->stack_index);
    parser->stack_index = 0;
}

void parser_reject(Parser* parser) {
    parser->stack_index = 0;
}

ASTNode* parser_parse(Parser* parser) {
    ASTNode* node = ast_makeProgramNode();
    parser_parseProgram(parser, node);

    return node;
}

void parser_parseProgram(Parser* parser, ASTNode* node) {
    Lexeme lexeme = parser_peek(parser);

    uint8_t can_loop = lexeme.type == TOK_FROM || lexeme.type == TOK_IMPORT;

    while(can_loop) {
        if(lexeme.type == TOK_FROM) {
            ACCEPT;
            parser_parseFromStmt(parser, node, node->scope);
        }
        if(lexeme.type == TOK_IMPORT) {
            ACCEPT;
            parser_parseImportStmt(parser, node, node->scope);
        }
        char* imports = ast_json_serializeImports(node->programNode);
        printf("%s\n", imports);
        lexeme = parser_peek(parser);
        can_loop = lexeme.type == TOK_FROM || lexeme.type == TOK_IMPORT;
    }

    // TODO: Resolve Imports and add them to symbol table
    // we no longer expect import or from after this

    can_loop = 1; //lexeme.type == TOK_TYPE;
    while (can_loop) {
        if(lexeme.type == TOK_EXTERN) {
            parser_reject(parser);
            ExternDecl* externDecl = parser_parseExternDecl(parser, node, node->scope);
            char* strDecl = ast_json_serializeExternDecl(externDecl);
            printf("%s\n", strDecl);
            CURRENT;
        }
        if(lexeme.type == TOK_TYPE) {
            parser_parseTypeDecl(parser, node, node->scope);
            CURRENT;
        }
        else if(lexeme.type == TOK_EOF) {
            printf("EOF reached, gracefully stopping.\n");
            return;
        }
        else {
            parser_reject(parser);

            Statement * stmt = parser_parseStmt(parser, node, node->scope);
            CURRENT;
            if (stmt == NULL) {
                PARSER_ASSERT(0, "Line %" PRIu32 ":%" PRIu32 ": Invalid token %s", EXPAND_LEXEME);
            }
            printf("%s\n", ast_json_serializeStatement(stmt));

            /*
            Expr* expr = parser_parseExpr(parser, node, node->scope);

            printf("%s\n", ast_json_serializeExpr(expr));
            CURRENT;
             */
        }
    }
}

ExternDecl* parser_parseExternDecl(Parser* parser, ASTNode* node, ASTScope* currentScope){
    ExternDecl* externDecl = ast_externdecl_make();

    Lexeme lexeme = parser_peek(parser);
    // assert extern
    PARSER_ASSERT(lexeme.type == TOK_EXTERN, "Line: %"PRIu16", Col: %"PRIu16" identifier expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // assert string token
    lexeme = parser_peek(parser);
    PARSER_ASSERT(lexeme.type == TOK_STRING_VAL, "Line: %"PRIu16", Col: %"PRIu16" identifier expected but %s was found.", EXPAND_LEXEME);
    // assert value is "C"
    PARSER_ASSERT(strcmp(lexeme.string, "\"C\"") == 0, "Line: %"PRIu16", Col: %"PRIu16" identifier expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // assert identifier
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" identifier expected but %s was found.", EXPAND_LEXEME);
    externDecl->name = strdup(lexeme.string);
    ACCEPT;
    // assert {
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;
    // now we expect an interface method
    // prepare to loop
    uint8_t can_loop = lexeme.type == TOK_FN;
    while(can_loop) {
        // parse interface method
        // we assert "fn"
        PARSER_ASSERT(lexeme.type == TOK_FN,
               "Line: %"PRIu16", Col: %"PRIu16" `fn` expected but %s was found.",
               EXPAND_LEXEME);
        // reject it
        parser_reject(parser);
        FnHeader * header = parser_parseFnHeader(parser, node, currentScope);
        // make sure fn doesn't already exist
        PARSER_ASSERT(map_get(&externDecl->methods, header->name) == NULL,
               "Line: %"PRIu16", Col: %"PRIu16" near %s, method `%s` already exists in interface.",
               EXPAND_LEXEME, header->name);

        // else we add it
        map_set(&externDecl->methods, header->name, header);
        vec_push(&externDecl->methodNames, header->name);
        // skip "," if any
        CURRENT;
        if(lexeme.type == TOK_COMMA) {
            ACCEPT;
            CURRENT;
        }

        // check if we reached "}"
        if(lexeme.type == TOK_RBRACE) {
            ACCEPT;
            can_loop = 0;
        }
    }
    return externDecl;
}

/*
We need to generate functions to parse each of the following:

<type_definition> ::= "type" <identifier> "=" <union_type>
<union_type> ::= <intersection_type> ( "|" <union_type> )*
<intersection_type> ::= <array_type> ( "&" <intersection_type> )*
<array_type> ::= <group_type> ("[" <int>? "]")*
<group_type> ::= <primary_type> | "(" <union_type> ")"

<primary_type> ::= <interface_type>
                 | <enum_type>
                 | <struct_type>
                 | <data_type>
                 | <function_type>
                 | <basic_type>
                 | <ptr_type>
                 | <class_type>
                 | <reference_type>

*/
void parser_parseTypeDecl(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    DataType * type = ast_type_makeType();
    type->kind = DT_REFERENCE;
    ACCEPT;
    Lexeme lexeme = parser_peek(parser);
    PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" identifier expected but %s was found.", EXPAND_LEXEME);
    type->name = strdup(lexeme.string);
    ACCEPT;

    lexeme = parser_peek(parser);

    if(lexeme.type == TOK_LESS) {
        // GENERICS!
        type->hasGeneric = 1;
        ACCEPT;
        CURRENT;
        uint8_t can_loop = 1;
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
               "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);

        while(can_loop) {
            // init generic param
            GenericParam * genericParam = ast_make_genericParam();
            genericParam->isGeneric = 1;

            // in a type declaration, all given templates in the referee are generic and not concrete.
            PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
                   "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.",
                   EXPAND_LEXEME);

            // get generic name
            genericParam->name = strdup(lexeme.string);
            ACCEPT;
            CURRENT;
            // check if have ":"
            if(lexeme.type == TOK_COLON) {
                ACCEPT;
                CURRENT;
                // get generic type
                genericParam->constraint = parser_parseTypeUnion(parser, node, NULL, currentScope);
                CURRENT;
            }
            else {
                // if not, set to any
                genericParam->constraint = NULL;
            }

            // add generic param
            vec_push(&type->genericParams, genericParam);
            if(lexeme.type == TOK_GREATER){
                can_loop = 0;
                ACCEPT;
                CURRENT;
            }

            else {
                PARSER_ASSERT(lexeme.type == TOK_COMMA,
                       "Line: %"PRIu16", Col: %"PRIu16" `,` expected but %s was found.",
                       EXPAND_LEXEME);
                ACCEPT;
                CURRENT;
            }
        }
    }

    PARSER_ASSERT(lexeme.type == TOK_EQUAL, "Line: %"PRIu16", Col: %"PRIu16" `=` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    //lexeme = parser_peek(parser);
    DataType* type_def = parser_parseTypeUnion(parser, node, type, currentScope);
    type->refType = ast_type_makeReference();
    type->refType->ref = type_def;
    //printf("%s\n", ast_stringifyType(type_def));
    printf("%s\n", ast_json_serializeDataType(type_def));

    map_set(&node->scope->dataTypes, type->name, type);
}

// <union_type> ::= <intersection_type> ( "|" <union_type> )*
DataType* parser_parseTypeUnion(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    // must parse union type
    DataType* type = parser_parseTypeIntersection(parser, node, parentReferee, currentScope);
    // check if we have intersection
    Lexeme lexeme = parser_peek(parser);
    if(lexeme.type == TOK_BITWISE_OR) {
        // we have an intersection
        ACCEPT;
        DataType* type2 = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
        // create intersection type
        UnionType * unions = ast_type_makeUnion();
        unions->left = type;
        unions->right = type2;

        // create new datatype to hold joints
        DataType* newType = ast_type_makeType();
        newType->kind = DT_TYPE_UNION;
        newType->unionType = unions;
        type = newType;
    }
    // TODO: make sure that unions lhs and rhs match
    // TODO: make sure that nullable types are valid (memory references)

    parser_reject(parser);
    return type;
}

// <intersection_type> ::= <group_type> ( "&" <group_type> )*
DataType* parser_parseTypeIntersection(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope){
    // must parse group type
    DataType* type = parser_parseTypeArray(parser, node, parentReferee, currentScope);
    // check if we have intersection
    Lexeme lexeme = parser_peek(parser);
    if(lexeme.type == TOK_BITWISE_AND) {
        // we have an intersection
        ACCEPT;
        DataType* type2 = parser_parseTypeIntersection(parser, node, parentReferee, currentScope);
        // create intersection type
        JoinType * join = ast_type_makeJoin();
        join->left = type;
        join->left = type2;

        // create new datatype to hold joints
        DataType* newType = ast_type_makeType();
        newType->kind = DT_TYPE_JOIN;
        newType->joinType = join;
        return newType;
    }
    parser_reject(parser);
    return type;
}

// <group_type> ::= <primary_type>  | "(" <union_type> ")" "?"?
DataType* parser_parseTypeGroup(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    // check if we have group first
    Lexeme lexeme = parser_peek(parser);
    if(lexeme.type == TOK_LPAREN) {
        // we have a group
        ACCEPT;
        DataType* type = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
        lexeme = parser_peek(parser);
        PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;

        // check if we have optional
        lexeme = parser_peek(parser);
        if(lexeme.type == TOK_NULLABLE) {
            // we have an optional
            ACCEPT;
            type->isNullable = 1;

        }
        else {
            parser_reject(parser);
        }
        return type;
    }
    parser_reject(parser);
    DataType* type = parser_parseTypePrimary(parser, node, parentReferee, currentScope);
    return type;
}

// <array_type> ::= <primary_type> "[" "]" | <primary_type> "[" <integer_literal> "]"
DataType * parser_parseTypeArray(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    // must parse primary type first
    DataType* primary = parser_parseTypeGroup(parser, node, parentReferee, currentScope);
    Lexeme lexeme = parser_peek(parser);
    DataType * last_type = primary;
    // if next token is "[" then it is an array

    uint8_t can_loop = lexeme.type == TOK_LBRACKET;

    while(can_loop){
        if(lexeme.type == TOK_LBRACKET) {
            // we have found an array
            ArrayType* array = ast_type_makeArray();
            array->arrayOf = last_type;
            ACCEPT;
            lexeme = parser_peek(parser);
            // if next token is "]" then it is an array of unknown size
            if(lexeme.type == TOK_RBRACKET) {
                ACCEPT;
            }
            else{
                uint32_t arrayLen = 0;
                // value must in either int, hex, oct or bin
                PARSER_ASSERT((lexeme.type == TOK_INT) || (lexeme.type == TOK_HEX_INT) ||
                       (lexeme.type == TOK_OCT_INT) || (lexeme.type == TOK_BINARY_INT),
                       "Line: %"PRIu16", Col: %"PRIu16" `int` expected but %s was found.",
                       EXPAND_LEXEME);
                // parse the value

                if(lexeme.type == TOK_INT) {
                    arrayLen = strtoul(lexeme.string, NULL, 10);
                }
                else if(lexeme.type == TOK_HEX_INT) {
                    arrayLen = strtoul(lexeme.string, NULL, 16);
                }
                else if(lexeme.type == TOK_OCT_INT) {
                    arrayLen = strtoul(lexeme.string, NULL, 8);
                }
                else if(lexeme.type == TOK_BINARY_INT) {
                    arrayLen = strtoul(lexeme.string, NULL, 2);
                }
                array->len = arrayLen;
                ACCEPT;
                lexeme = parser_peek(parser);
                PARSER_ASSERT(lexeme.type == TOK_RBRACKET,
                       "Line: %"PRIu16", Col: %"PRIu16" `]` expected but %s was found.",
                       EXPAND_LEXEME);
                ACCEPT;
            }

            DataType * retType = ast_type_makeType();
            retType->kind = DT_ARRAY;
            retType->arrayType = array;
            last_type = retType;
            CURRENT;
        }
        else {
            can_loop = 0;
        }
    }

    return last_type;
}

/*
<primary_type> ::= <interface_type> "?"?
                 | <enum_type> "?"?
                 | <struct_type> "?"?
                 | <data_type> "?"?
                 | <function_type> "?"?
                 | <basic_type> "?"?
                 | <ptr_type> "?"?
                 | <class_type> "?"?
                 | <reference_type> "?"?
 */
DataType* parser_parseTypePrimary(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    // parse enum if current keyword is enum
    DataType * type = NULL;
    Lexeme lexeme = parser_peek(parser);
    if(lexeme.type == TOK_ENUM) {
        type = parser_parseTypeEnum(parser, node, currentScope);
    }
    else if(lexeme.type == TOK_STRUCT){
        type = parser_parseTypeStruct(parser, node, parentReferee, currentScope);
    }
    else if(lexeme.type == TOK_VARIANT) {
        type = parser_parseTypeVariant(parser, node, parentReferee, currentScope);
    }
    // check if current keyword is a basic type
    else if((lexeme.type >= TOK_I8) && (lexeme.type <= TOK_CHAR)) {
        if(lexeme.type == TOK_VOID){
            DataTypeKind t = lexeme.type - TOK_I8;
            DataTypeKind t2 = lexeme.type - TOK_I8;
        }
        // create new type assign basic to it
        DataType* basicType = ast_type_makeType();
        basicType->kind = lexeme.type - TOK_I8;
        ACCEPT;
        type = basicType;
    }
    else if (lexeme.type == TOK_CLASS){
        type = parser_parseTypeClass(parser, node, parentReferee, currentScope);
    }
    else if (lexeme.type == TOK_INTERFACE){
        type = parser_parseTypeInterface(parser, node, parentReferee, currentScope);
    }
    else if (lexeme.type == TOK_FN) {
        type = parser_parseTypeFn(parser, node, parentReferee, currentScope);
    }
    else if (lexeme.type == TOK_PTR) {
        type = parser_parseTypePtr(parser, node, parentReferee, currentScope);
    }
    else if (lexeme.type == TOK_PROCESS) {
        type = parser_parseTypeProcess(parser, node, parentReferee, currentScope);
    }
    // check if we have an ID, we parse package then
    else if(lexeme.type == TOK_IDENTIFIER) {
        DataType* refType = parser_parseTypeRef(parser, node, parentReferee, currentScope);

        type = refType;
    }
    // check if we have an optional type
    CURRENT;
    if(lexeme.type == TOK_NULLABLE) {
        type->isNullable = 1;
        ACCEPT;
    }
    else
        parser_reject(parser);

    return type;
}

DataType* parser_parseTypeRef(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    // we create a reference refType
    DataType* refType = ast_type_makeType();
    refType->kind = DT_REFERENCE;
    refType->refType = ast_type_makeReference();
    // rollback
    parser_reject(parser);
    refType->refType->pkg = parser_parsePackage(parser, node, currentScope);

    // a generic list might follow
    Lexeme lexeme = parser_peek(parser);
    if(lexeme.type == TOK_LESS) {
        refType->hasGeneric = 1;
        // here, each generic refType can be any refType, so we recursively parse refType
        // format <refType> ("," <refType>)* ">"
        ACCEPT;
        lexeme = parser_peek(parser);
        uint8_t can_loop = lexeme.type != TOK_GREATER;
        while(can_loop) {
            // parse refType
            DataType* genericType = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
            GenericParam* gparam = ast_make_genericParam();
            gparam->isGeneric = 0;

            if(parentReferee != NULL) {
                // check if the param is a reference
                if (genericType->kind == DT_REFERENCE) {
                    // check if it has a single name (not entire package a.b.c)
                    if (genericType->refType->pkg->ids.length == 1) {
                        // check if it exists on parent
                        char *gtname = genericType->refType->pkg->ids.data[0];
                        uint32_t i;
                        GenericParam *parentGenericType;
                        vec_foreach(&parentReferee->genericParams, parentGenericType, i) {
                            if (parentGenericType->isGeneric == 1) {
                                if (strcmp(parentGenericType->name, gtname) == 0) {
                                    gparam->isGeneric = 1;
                                    gparam->name = strdup(gtname);
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if(!gparam->isGeneric) {
                gparam->type = genericType;
            }

            //add to generic list
            vec_push(&refType->genericParams, gparam);
            lexeme = parser_peek(parser);
            if(lexeme.type == TOK_COMMA) {
                ACCEPT;
            }
            else {
                // if no comma, we assert ">"
                PARSER_ASSERT(lexeme.type == TOK_GREATER,
                       "Line: %"PRIu16", Col: %"PRIu16" `>` expected but %s was found.",
                       EXPAND_LEXEME);
                ACCEPT;
                can_loop = 0;
            }
        }
    }
    else {
        parser_reject(parser);
    }
    // check if the type is simple id
    if (refType->refType->pkg->ids.length == 1){
        if((parentReferee != NULL) && (strcmp(parentReferee->name, refType->refType->pkg->ids.data[0]) == 0)) {
            printf("Type `%s` is resolved by itself\n", refType->refType->pkg->ids.data[0]);
        }

        DataType* t = resolver_resolveType(parser, node, currentScope, refType->refType->pkg->ids.data[0]);
        if(t != NULL) {
            printf("Type `%s` is resolved!\n", refType->refType->pkg->ids.data[0]);
        }
        else {
            printf("Type `%s` NOT is resolved!\n", refType->refType->pkg->ids.data[0]);
        }
    }

    return refType;
}

// interface_tupe ::= "interface" "{" <interface_decl> (","? <interface_decl>)* "}"
DataType* parser_parseTypeInterface(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope){
    // create base type
    DataType* interfaceType = ast_type_makeType();
    interfaceType->kind = DT_INTERFACE;
    interfaceType->interfaceType = ast_type_makeInterface();
    // accept interface
    ACCEPT;
    Lexeme CURRENT;

    // if we have "(", means interface extends other interfaces
    if(lexeme.type == TOK_LPAREN) {
        ACCEPT;
        parser_parseExtends(parser, node, parentReferee, &interfaceType->interfaceType->extends, currentScope);
        CURRENT;
    }
    else {

        //parser_reject(parser);
    }


    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;
    // now we expect an interface method
    // prepare to loop
    uint8_t can_loop = lexeme.type == TOK_FN;
    while(can_loop) {
        // parse interface method
        // we assert "fn"
        PARSER_ASSERT(lexeme.type == TOK_FN,
               "Line: %"PRIu16", Col: %"PRIu16" `fn` expected but %s was found.",
               EXPAND_LEXEME);
        // reject it
        parser_reject(parser);
        FnHeader * header = parser_parseFnHeader(parser, node, currentScope);
        // make sure fn doesn't already exist
        PARSER_ASSERT(map_get(&interfaceType->interfaceType->methods, header->name) == NULL,
               "Line: %"PRIu16", Col: %"PRIu16" near %s, method `%s` already exists in interface.",
               EXPAND_LEXEME, header->name);

        // else we add it
        map_set(&interfaceType->interfaceType->methods, header->name, header);
        vec_push(&interfaceType->interfaceType->methodNames, header->name);
        // skip "," if any
        CURRENT;
        if(lexeme.type == TOK_COMMA) {
            ACCEPT;
            CURRENT;
        }

        // check if we reached "}"
        if(lexeme.type == TOK_RBRACE) {
            ACCEPT;
            can_loop = 0;
        }
    }

    return interfaceType;
}

DataType* parser_parseTypeClass(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    DataType * classType = ast_type_makeType();
    classType->kind = DT_CLASS;
    classType->classType = ast_type_makeClass(currentScope);
    // accept class
    ACCEPT;
    Lexeme CURRENT;

    // if we have "(", means interface extends other interfaces
    if(lexeme.type == TOK_LPAREN) {
        ACCEPT;
        parser_parseExtends(parser, node, parentReferee, &classType->classType->extends, currentScope);
        CURRENT;
    }
    else {

        //parser_reject(parser);
    }


    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.\n%s", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;
    // now we expect an interface method
    // prepare to loop
    uint8_t can_loop = lexeme.type == TOK_FN || lexeme.type == TOK_LET;
    while(can_loop) {
        // parse interface method
        // we assert "fn"

        PARSER_ASSERT(lexeme.type == TOK_FN || lexeme.type == TOK_LET,
               "Line: %"PRIu16", Col: %"PRIu16" `fn` or `let` expected but %s was found\n%s",
               EXPAND_LEXEME);
        // reject it
        if(lexeme.type == TOK_FN){
            parser_reject(parser);
            Statement * stmt = parser_parseStmtFn(parser, node, currentScope);
            // assert stmt != NULL
            PARSER_ASSERT(stmt != NULL, "Line: %"PRIu16", Col: %"PRIu16" invalid token `%s`.",
                   EXPAND_LEXEME);

            FnDeclStatement * fnDecl = stmt->fnDecl;
            // add method name
            ClassMethod * classMethod = ast_type_makeClassMethod();
            classMethod->decl = fnDecl;
            map_set(&classType->classType->methods, fnDecl->header->name, classMethod);
            vec_push(&classType->classType->methodNames, fnDecl->header->name);
        }
        else {
            parser_reject(parser);
            Statement *stmt = parser_parseStmtLet(parser, node, currentScope);
            VarDeclStatement* varDecl = stmt->varDecl;
            uint32_t i = 0; LetExprDecl* letDecl;
            vec_foreach(&varDecl->letList, letDecl, i){
                vec_push(&classType->classType->letList, letDecl);
            }

            free(varDecl);
            free(stmt);
        }
        // skip "," if any
        CURRENT;
        if(lexeme.type == TOK_COMMA) {
            ACCEPT;
            CURRENT;
        }

        // check if we reached "}"
        if(lexeme.type == TOK_RBRACE) {
            ACCEPT;
            can_loop = 0;
        }
    }

    return classType;
}

// variant_type ::= "variant" "{" <variant_decl> (","? <variant_decl>)* "}"
DataType* parser_parseTypeVariant(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    //create base type
    DataType * variantType = ast_type_makeType();
    variantType->kind = DT_VARIANT;
    variantType->variantType = ast_type_makeVariant();
    // accept variant
    ACCEPT;
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // make sure we have an ID next
    CURRENT;

    // prepare to loop
    uint8_t can_loop = lexeme.type == TOK_IDENTIFIER;
    while(can_loop) {
        // we get the name of the variant
        // assert we have an ID
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);

        char* variantName = lexeme.string;
        // make sure the variant doesn't have constructor with same name, by checking variantType->variantType->constructors
        // using map_get
        PARSER_ASSERT(map_get(&variantType->variantType->constructors, variantName) == NULL,
               "Line: %"PRIu16", Col: %"PRIu16" near `%s` variant constructor with name %s already exists.", EXPAND_LEXEME);

        // we create a new VariantConstructor
        VariantConstructor* variantConstructor = ast_type_makeVariantConstructor();
        variantConstructor->name = strdup(variantName);
        // accept the name
        ACCEPT;

        CURRENT;
        // check if we have parenthesis
        if(lexeme.type == TOK_LPAREN) {
            // we parse the arguments
            // format: "(" <id>":" <type> (","? <id> ":"<type>)* ")"
            ACCEPT;
            CURRENT;
            // prepare to loop
            uint8_t can_loop_2 = lexeme.type == TOK_IDENTIFIER;
            while(can_loop_2) {
                // assert identifier
                PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" identifier expected but %s was found.", EXPAND_LEXEME);

                // we get the name of the argument
                char* argName = lexeme.string;

                // accept the name
                ACCEPT;
                CURRENT;

                // make sure we have a colon
                PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.", EXPAND_LEXEME);
                ACCEPT;
                CURRENT;
                // we parse the type
                DataType* argType = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
                // we create a new argument
                VariantConstructorArgument * arg = ast_type_makeVariantConstructorArgument();
                arg->name = strdup(argName);
                arg->type = argType;
                // add to argument list

                // make sure arg name doesn't already exist
                PARSER_ASSERT(map_get(&variantConstructor->args, argName) == NULL, "Line: %"PRIu16", Col: %"PRIu16" near %s, argument name `%s` already exists.", EXPAND_LEXEME, argName);
                vec_push(&variantConstructor->argNames, argName);
                map_set(&variantConstructor->args, argName, arg);

                CURRENT;
                // check if we have a comma

                if(lexeme.type == TOK_COMMA) {
                    ACCEPT;
                    CURRENT;
                }
                else if(lexeme.type == TOK_RPAREN) {
                    can_loop_2 = 0;
                    ACCEPT;
                }
                else {
                    // throw error, we need either a comma or a ")"
                    PARSER_ASSERT(0, "Line: %"PRIu16", Col: %"PRIu16" near %s, `,` or `)` expected but %s was found.", EXPAND_LEXEME);
                }
            }
        }
        // add constructor to map and its name to the vector
        map_set(&variantType->variantType->constructors, variantName, variantConstructor);
        vec_push(&variantType->variantType->constructorNames, variantName);
        // skip comma if any
        lexeme = parser_peek(parser);
        if(lexeme.type == TOK_COMMA) {
            ACCEPT;
            CURRENT;
        }

        if(lexeme.type == TOK_RBRACE) {
            can_loop = 0;
            ACCEPT;
        }
    }

    return variantType;
}

// struct_type ::= "struct" "{" <struct_decl> ("," <struct_decl>)* "}"
DataType* parser_parseTypeStruct(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    DataType * structType = ast_type_makeType();
    structType->kind = DT_STRUCT;
    structType->structType = ast_type_makeStruct();

    // currently at struct
    // we skip struct and make sure we have { after
    ACCEPT;
    Lexeme CURRENT;
    // if we have "(", means interface extends other interfaces
    if(lexeme.type == TOK_LPAREN) {
        ACCEPT;
        parser_parseExtends(parser, node, parentReferee, &structType->structType->extends, currentScope);
        CURRENT;
    }
    else {
        //parser_reject(parser);
    }
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;
    // prepare to loop
    uint8_t can_loop = lexeme.type == TOK_IDENTIFIER;
    while(can_loop) {
        // we parse in the form of identifier : type
        // create struct attribute
        StructAttribute* attr = ast_type_makeStructAttribute();
        // parse identifier
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
               "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
        attr->name = strdup(lexeme.string);
        ACCEPT;
        CURRENT;
        // parse :

        PARSER_ASSERT(lexeme.type == TOK_COLON,
               "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;

        // parse type
        attr->type = parser_parseTypeUnion(parser, node, parentReferee, currentScope);

        // add to struct
        // make sure type doesn't exist first
        PARSER_ASSERT(map_get(&structType->structType->attributes, attr->name) == NULL,
               "Line: %"PRIu16", Col: %"PRIu16" near %s, attribute `%s` already exists in struct.", EXPAND_LEXEME, attr->name);

        vec_push(&structType->structType->attributeNames, attr->name);
        map_set(&structType->structType->attributes, attr->name, attr);


        // check if we reached the end, means we do not have an ID anymore
        CURRENT;

        // ignore comma if exists
        if(lexeme.type == TOK_COMMA) {
            ACCEPT;
            CURRENT;
        }

        if(lexeme.type == TOK_RBRACE) {
            can_loop = 0;
        }
    }
    ACCEPT;
    return structType;
}

// enum_type ::= "enum" "{" <enum_decl> ("," <enum_decl>)* "}"
DataType* parser_parseTypeEnum(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    EnumType* enum_ = ast_type_makeEnum();
    // current position: enum
    ACCEPT;
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;

    uint8_t can_loop = 1;
    uint32_t index = 0;

    while(can_loop) {
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
               "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
        char* name = lexeme.string;
        // TODO: make sure index doesn't exeed some limit?

        if (map_get(&enum_->enums, name) != NULL) {
            PARSER_ASSERT(1==0,
                   "Line: %"PRIu16", Col: %"PRIu16", enum value duplicated: %s",
                   lexeme.line, lexeme.col, lexeme.string);
        }
        map_set(&enum_->enums, name, index++);
        vec_push(&enum_->enumNames, name);

        ACCEPT;
        CURRENT;

        if(lexeme.type == TOK_COMMA){
            ACCEPT;
            CURRENT;
        }

        if(lexeme.type != TOK_IDENTIFIER) {
            can_loop = 0;
        }
    }
    PARSER_ASSERT(lexeme.type == TOK_RBRACE, "Line: %"PRIu16", Col: %"PRIu16" `}` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    DataType * enumType = ast_type_makeType();
    enumType->kind = DT_ENUM;
    enumType->enumType = enum_;
    return enumType;
}

/*
 * "from" <package_id> "import" <package_id> ("as" <id>)? ("," <package_id> ("as" <id>)?)*
 */
void parser_parseFromStmt(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // from has already been accepted
    // we expect a namespace x.y.z
    PackageID* source = parser_parsePackage(parser, node, currentScope);
    Lexeme lexeme = parser_peek(parser);
    // next we need an import
    PARSER_ASSERT(lexeme.type == TOK_IMPORT, "Line: %"PRIu16", Col: %"PRIu16" `import` expected but %s was found.", EXPAND_LEXEME);

    ACCEPT;

    uint8_t can_loop = 1;
    do {
        PackageID* target = parser_parsePackage(parser, node, currentScope);
        lexeme = parser_peek(parser);

        uint8_t hasAlias = 0;
        char* alias = NULL;
        // is `as` present?
        if(lexeme.type == TOK_TYPE_CONVERSION) {
            hasAlias = 1;
            ACCEPT;
            lexeme = parser_peek(parser);
            PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
            alias = strdup(lexeme.string);
            ACCEPT;
        }
        else {
            parser_reject(parser);
        }
        ImportStmt* import = ast_makeImportStmt(source, target, hasAlias, alias);
        vec_push(&node->programNode->importStatements, import);
        lexeme = parser_peek(parser);

        // here we might find a comma. if we do, we accept it and keep looping
        can_loop = lexeme.type == TOK_COMMA;
        if(can_loop) {
            ACCEPT;
        }
    } while (can_loop);
    parser_reject(parser);
}

// parses extends list, must start from first symbol after "("
void parser_parseExtends(Parser* parser, ASTNode* node, DataType* parentReferee, vec_dtype_t* extends, ASTScope* currentScope){
    Lexeme lexeme = parser_peek(parser);
    uint8_t can_loop = lexeme.type != TOK_RPAREN;
    while(can_loop) {
        // we don't look out for id because it might be an anonymous type
        //PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
        //       "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.",
        //       EXPAND_LEXEME);
        // parse type
        parser_reject(parser);
        DataType* interfaceParentType = parser_parseTypePrimary(parser, node, parentReferee, currentScope);
        // add to extends
        vec_push(extends, interfaceParentType);

        // check if we have a comma
        lexeme = parser_peek(parser);
        if(lexeme.type == TOK_COMMA) {
            ACCEPT;
        }
        else {
            // if no comma, we assert ")"
            PARSER_ASSERT(lexeme.type == TOK_RPAREN,
                   "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.",
                   EXPAND_LEXEME);
            ACCEPT;
            can_loop = 0;
        }
    }
}
// "fn" "(" <param_list>? ")" ("->" <type>)? <block>
DataType* parser_parseTypeFn(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    // current position: fn
    ACCEPT;
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LPAREN, "Line: %"PRIu16", Col: %"PRIu16" `(` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;

    // create function type
    DataType* fnType = ast_type_makeType();
    fnType->kind = DT_FN;
    fnType->fnType = ast_type_makeFn();
    // parse parameters
    parser_parseFnDefArguments(parser, node, parentReferee, &fnType->fnType->args, &fnType->fnType->argNames, currentScope);
    // check if we have `->`
    lexeme = parser_peek(parser);
    if(lexeme.type == TOK_FN_RETURN_TYPE) {
        ACCEPT;
        // parse return type
        fnType->fnType->returnType = parser_parseTypeUnion(parser, node, fnType, currentScope);
        // assert return type is not null
        PARSER_ASSERT(fnType->fnType->returnType != NULL, "Line: %"PRIu16", Col: %"PRIu16" near %s, return type null detected, this is a parser issue.", EXPAND_LEXEME);
    }
    else {
        parser_reject(parser);
    }

    return fnType;
}

// "ptr" "<" <type> ">"
DataType* parser_parseTypePtr(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope){
    // build type
    DataType* ptrType = ast_type_makeType();
    ptrType->kind = DT_PTR;
    ptrType->ptrType = ast_type_makePtr();
    // currently at ptr
    ACCEPT;
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LESS, "Line: %"PRIu16", Col: %"PRIu16" `<` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // parse type
    ptrType->ptrType->target = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
    // assert type is not null
    PARSER_ASSERT(ptrType->ptrType->target != NULL, "Line: %"PRIu16", Col: %"PRIu16" near %s, type null detected, this is a parser issue.", EXPAND_LEXEME);
    // assert we have a ">"
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_GREATER, "Line: %"PRIu16", Col: %"PRIu16" `>` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    return ptrType;
}

DataType * parser_parseTypeProcess(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope){
    DataType * processType = ast_type_makeType();
    processType->kind = DT_PROCESS;
    processType->processType = ast_type_makeProcess();
    ACCEPT;
    // assert we have a "<"
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LESS, "Line: %"PRIu16", Col: %"PRIu16" `<` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // parse type
    processType->processType->inputType = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
    CURRENT;
    // assert we have a ","
    PARSER_ASSERT(lexeme.type == TOK_COMMA, "Line: %"PRIu16", Col: %"PRIu16" `,` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    processType->processType->outputType = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
    CURRENT;
    // assert we have a ">"
    PARSER_ASSERT(lexeme.type == TOK_GREATER, "Line: %"PRIu16", Col: %"PRIu16" `>` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // assert "("
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LPAREN, "Line: %"PRIu16", Col: %"PRIu16" `(` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;
    uint8_t loop = lexeme.type != TOK_RPAREN;
    while(loop){
        // create argument
        FnArgument* fnarg = ast_type_makeFnArgument();
        // assert ID
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
        fnarg->name = strdup(lexeme.string);
        ACCEPT;
        CURRENT;
        // assert ":"
        PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        // parse type
        fnarg->type = parser_parseTypeUnion(parser, node, parentReferee, currentScope);
        CURRENT;
        // check if we have "," else assert ) and stop
        if(lexeme.type == TOK_COMMA){
            ACCEPT;
            CURRENT;
        }
        else {
            PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.", EXPAND_LEXEME);

            loop = 0;
        }
        // add argument
        vec_push(&processType->processType->argNames, fnarg->name);
        map_set(&processType->processType->args, fnarg->name, fnarg);
    }
    ACCEPT;
    CURRENT;
    // assert "{"
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    parser_reject(parser);
    // parse block
    processType->processType->body = parser_parseStmtBlock(parser, node, currentScope);
    return processType;
}

// starts from the first argument
void parser_parseFnDefArguments(Parser* parser, ASTNode* node, DataType* parentType, map_fnargument_t* args, vec_str_t* argsNames, ASTScope* currentScope){
    Lexeme CURRENT;
    uint8_t loop = lexeme.type != TOK_RPAREN;
    while(loop) {
        parser_reject(parser);
        // build fnarg
        FnArgument* fnarg = ast_type_makeFnArgument();
        // check if we have `mut` or id
        lexeme = parser_peek(parser);
        if(lexeme.type == TOK_MUT) {
            fnarg->isMutable = 1;
            ACCEPT;
            CURRENT;
        }
        // assert an id
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
        fnarg->name = strdup(lexeme.string);
        ACCEPT;
        // assert ":"
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        // parse type
        fnarg->type = parser_parseTypeUnion(parser, node, parentType, currentScope);
        // assert type is not null
        PARSER_ASSERT(fnarg->type != NULL, "Line: %"PRIu16", Col: %"PRIu16" `type` near %s, generated a NULL type. This is a parser issue.", EXPAND_LEXEME);
        // add to args
        map_set(args, fnarg->name, fnarg);
        vec_push(argsNames, fnarg->name);
        // check if we have a comma
        lexeme = parser_peek(parser);
        if(lexeme.type == TOK_COMMA) {
            ACCEPT;
        }
        else {
            // if no comma, we assert ")"
            PARSER_ASSERT(lexeme.type == TOK_RPAREN,
                   "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.",
                   EXPAND_LEXEME);
            ACCEPT;
            loop = 0;
        }
    }
}

/*
 * "import" <package_id> ("as" <id>)?
 */
void parser_parseImportStmt(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // from has already been accepted
    // we expect a namespace x.y.z
    PackageID* source = parser_parsePackage(parser, node, currentScope);
    uint8_t hasAlias = 0;
    char* alias = NULL;
    Lexeme lexeme = parser_peek(parser);
    // is `as` present?
    if(lexeme.type == TOK_TYPE_CONVERSION) {
        hasAlias = 1;
        ACCEPT;
        lexeme = parser_peek(parser);
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
        alias = strdup(lexeme.string);
        ACCEPT;
    }
    else{
        parser_reject(parser);
    }

    PackageID * empty = ast_makePackageID();
    ImportStmt* import = ast_makeImportStmt(source, empty, hasAlias, alias);
    // TODO: free empty
    vec_push(&node->programNode->importStatements, import);
}


/** Expressions **/
Expr* parser_parseExpr(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    return parser_parseLetExpr(parser, node, currentScope);
}

/*
 * let <id> (":" <type>)? "=" <uhs> "in" <uhs>
 * let "{"<id> (":" <type>)? (<id> (":" <type>)?)*"}" "=" <uhs> "in" <uhs>
 * let "["<id> (":" <type>)? (<id> (":" <type>)?)*"]" "=" <uhs> "in" <uhs>
 */
Expr* parser_parseLetExpr(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Lexeme CURRENT;
    if(lexeme.type == TOK_LET)
    {
        Expr *expr = ast_expr_makeExpr(ET_LET);
        LetExpr *let = ast_expr_makeLetExpr(currentScope);

        expr->letExpr = let;

        // assert let
        PARSER_ASSERT(lexeme.type == TOK_LET, "Line: %"PRIu16", Col: %"PRIu16" `let` expected but %s was found.",
               EXPAND_LEXEME);
        ACCEPT;

        CURRENT;
        uint8_t loop_over_expr = 1;
        while (loop_over_expr) {
            LetExprDecl *letDecl = ast_expr_makeLetExprDecl();
            char *expect = NULL;

            // check if we have id or { or [
            if (lexeme.type == TOK_LBRACE) {
                expect = "]";
                letDecl->initializerType = LIT_STRUCT_DECONSTRUCTION;
                ACCEPT;
                CURRENT;
            } else if (lexeme.type == TOK_LBRACKET) {
                expect = "]";
                letDecl->initializerType = LIT_ARRAY_DECONSTRUCTION;
                ACCEPT;
                CURRENT;
            } else {
                expect = NULL;
                letDecl->initializerType = LIT_NONE;
            }

            uint8_t loop = 1;
            while (loop) {
                FnArgument *var = ast_type_makeFnArgument();
                // check if we have a mut
                if (lexeme.type == TOK_MUT) {
                    var->isMutable = 1;
                    ACCEPT;
                    CURRENT;
                }
                // assert ID
                PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
                       "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
                var->name = strdup(lexeme.string);
                ACCEPT;
                CURRENT;
                // assert ":"
                if(lexeme.type == TOK_COLON) {
                    PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.",
                           EXPAND_LEXEME);
                    ACCEPT;
                    // parse type
                    var->type = parser_parseTypeUnion(parser, node, NULL, currentScope);
                    // assert type is not null
                    PARSER_ASSERT(var->type != NULL,
                           "Line: %"PRIu16", Col: %"PRIu16" `type` near %s, generated a NULL type. This is a parser issue.",
                           EXPAND_LEXEME);

                    // check if we have a comma
                    CURRENT;
                }
                // add to args
                map_set(&letDecl->variables, var->name, var);
                vec_push(&letDecl->variableNames, var->name);

                if (lexeme.type == TOK_COMMA) {
                    ACCEPT;
                    CURRENT;
                } else {
                    parser_reject(parser);
                    loop = 0;
                }
            }
            // if we expected something earlier, we must find it closed now
            if (expect != NULL) {
                CURRENT;
                // if we expected "]", we must find "]"
                if (expect[0] == '[') {
                    // assert "]"
                    PARSER_ASSERT(lexeme.type == TOK_RBRACKET,
                           "Line: %"PRIu16", Col: %"PRIu16" `]` expected but %s was found.",
                           EXPAND_LEXEME);
                    ACCEPT;
                    CURRENT;
                } else if (expect[0] == '{') {
                    // assert "]"
                    PARSER_ASSERT(lexeme.type == TOK_RBRACE, "Line: %"PRIu16", Col: %"PRIu16" `]` expected but %s was found.",
                           EXPAND_LEXEME);
                    ACCEPT;
                    CURRENT;
                }
            }

            CURRENT;
            // assert "="
            PARSER_ASSERT(lexeme.type == TOK_EQUAL, "Line: %"PRIu16", Col: %"PRIu16" `=` expected but %s was found.",
                   EXPAND_LEXEME);
            ACCEPT;

            Expr *initializer = parser_parseExpr(parser, node, currentScope);
            letDecl->initializer = initializer;
            // assert initializer is not null
            PARSER_ASSERT(initializer != NULL,
                   "Line: %"PRIu16", Col: %"PRIu16" `uhs` near %s, generated a NULL uhs. This is a parser issue.",
                   EXPAND_LEXEME);
            // add the decl to the let uhs
            vec_push(&let->letList, letDecl);

            CURRENT;
            // check if we have a comma
            if (lexeme.type == TOK_COMMA) {
                ACCEPT;
                CURRENT;
            } else {
                parser_reject(parser);
                loop_over_expr = 0;
            }
        }


        // assert "in"
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_IN, "Line: %"PRIu16", Col: %"PRIu16" `in` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;

        // parse in uhs
        Expr *inExpr = parser_parseExpr(parser, node, currentScope);
        let->inExpr = inExpr;
        // assert in uhs is not null
        PARSER_ASSERT(inExpr != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `uhs` near %s, generated a NULL uhs. This is a parser issue.",
               EXPAND_LEXEME);

        return expr;
    }
    parser_reject(parser);
    return parser_parseMatchExpr(parser, node, currentScope);
}

// "match" uhs "{" <cases> "}"
Expr* parser_parseMatchExpr(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Lexeme CURRENT;
    if(lexeme.type != TOK_MATCH) {
        parser_reject(parser);
        return parser_parseOpAssign(parser, node, currentScope);
    }

    ACCEPT;
    Expr* expr = ast_expr_makeExpr(ET_MATCH);
    MatchExpr* match = ast_expr_makeMatchExpr(parser_parseExpr(parser, node, currentScope));
    expr->matchExpr = match;
    // assert "{"
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    uint8_t loop = 1;

    while(loop){

        Expr* condition = parser_parseExpr(parser, node, currentScope);
        // assert condition is not null
        PARSER_ASSERT(condition != NULL, "Line: %"PRIu16", Col: %"PRIu16" `uhs` near %s, generated a NULL uhs. This is a parser issue.", EXPAND_LEXEME);

        CURRENT;
        // assert "=>"
        PARSER_ASSERT(lexeme.type == TOK_CASE_EXPR, "Line: %"PRIu16", Col: %"PRIu16" `=>` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        Expr* result = parser_parseExpr(parser, node, currentScope);
        // assert result is not null
        PARSER_ASSERT(result != NULL, "Line: %"PRIu16", Col: %"PRIu16" `uhs` near %s, generated a NULL uhs. This is a parser issue.", EXPAND_LEXEME);
        CaseExpr* case_ = ast_expr_makeCaseExpr(condition, result);

        CURRENT;

        // add case to match
        vec_push(&match->cases, case_);

        // if not comma then exit
        if(lexeme.type != TOK_COMMA){
            parser_reject(parser);
            loop = 0;
        }
        else{
            ACCEPT;
        }
    }
    // assert "}"
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_RBRACE, "Line: %"PRIu16", Col: %"PRIu16" `}` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    return expr;
}

Expr* parser_parseOpAssign(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Expr* lhs = parser_parseOpOr(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(
            lexeme.type == TOK_EQUAL ||
            lexeme.type == TOK_PLUS_EQUAL ||
            lexeme.type == TOK_MINUS_EQUAL ||
            lexeme.type == TOK_STAR_EQUAL ||
            lexeme.type == TOK_DIV_EQUAL
    ) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(BET_ASSIGN, NULL, NULL);

        if(lexeme.type == TOK_EQUAL)
            binaryExpr->binaryExpr->type = BET_ASSIGN;
        else if(lexeme.type == TOK_PLUS_EQUAL)
            binaryExpr->binaryExpr->type = BET_ADD_ASSIGN;
        else if(lexeme.type == TOK_MINUS_EQUAL)
            binaryExpr->binaryExpr->type = BET_SUB_ASSIGN;
        else if(lexeme.type == TOK_STAR_EQUAL)
            binaryExpr->binaryExpr->type = BET_MUL_ASSIGN;
        else if(lexeme.type == TOK_DIV_EQUAL)
            binaryExpr->binaryExpr->type = BET_DIV_ASSIGN;
        else
            // assert 0==1
            PARSER_ASSERT(0==1, "Line: %"PRIu16", Col: %"PRIu16" This is a parser error", EXPAND_LEXEME);


        Expr* rhs = parser_parseOpAssign(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }
    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpOr(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Expr* lhs = parser_parseOpAnd(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_LOGICAL_OR) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(BET_OR, NULL, NULL);

        Expr* rhs = parser_parseOpOr(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpAnd(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Expr* lhs = parser_parseOpBinOr(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_LOGICAL_AND) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(BET_AND, NULL, NULL);

        Expr* rhs = parser_parseOpAnd(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpBinOr(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Expr* lhs = parser_parseOpBinXor(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_BITWISE_OR) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(BET_BIT_OR, NULL, NULL);

        Expr* rhs = parser_parseOpBinOr(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpBinXor(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Expr* lhs =  parser_parseOpBinAnd(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_BITWISE_XOR) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr =ast_expr_makeBinaryExpr(BET_BIT_XOR, NULL, NULL);

        Expr* rhs = parser_parseOpBinXor(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpBinAnd(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Expr* lhs = parser_parseOpEq(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_BITWISE_AND) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(BET_BIT_AND, NULL, NULL);

        Expr* rhs = parser_parseOpBinAnd(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpEq(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Expr* lhs = parser_parseOpCompare(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_EQUAL_EQUAL || lexeme.type == TOK_NOT_EQUAL) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(lexeme.type == TOK_EQUAL_EQUAL?BET_EQ:BET_NEQ, NULL, NULL);

        Expr* rhs = parser_parseOpEq(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

uint8_t lookUpGenericFunctionCall(Parser* parser){
    // will look into the next elements, it skips nested <>, (), [], {}.
    // It will return 1 if it finds a consecutive ">" "(" within the same scope
    vec_char_t stack;
    vec_init(&stack);
    vec_push(&stack, TOK_LESS);
    uint8_t prevWasGreater = 0;
    while(1){
        Lexeme CURRENT;
        if(lexeme.type == TOK_LESS ||
            lexeme.type == TOK_LBRACE ||
            lexeme.type == TOK_LPAREN ||
            lexeme.type == TOK_LBRACKET){

            if(prevWasGreater && lexeme.type == TOK_LPAREN && stack.length == 0){
                return 1;
            }
            vec_push(&stack, lexeme.type);
        }
        else if (lexeme.type == TOK_GREATER || lexeme.type == TOK_RBRACE ||
                 lexeme.type == TOK_RPAREN ||  lexeme.type == TOK_RBRACKET){
            if(lexeme.type == TOK_GREATER){
                prevWasGreater = 1;
            }
            vec_pop(&stack);

        }else if(lexeme.type == TOK_EOF){
            parser_reject(parser);
            return 0;
        }
        else {
            prevWasGreater = 0;
        }
    }
    return 0;
}

Expr* parser_parseOpCompare(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Expr* lhs = parser_parseOpShift(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_LESS || lexeme.type == TOK_GREATER ||
    lexeme.type == TOK_LESS_EQUAL || lexeme.type == TOK_GREATER_EQUAL) {
        ACCEPT;
        if(lexeme.type == TOK_LESS && lookUpGenericFunctionCall(parser)) {
            parser_reject(parser);
            CURRENT; // <
            //ACCEPT;

            // make call expression
            Expr* callExpr = ast_expr_makeExpr(ET_CALL);
            callExpr->callExpr = ast_expr_makeCallExpr(lhs);
            callExpr->callExpr->hasGenerics = 1;

            // parse generic arguments
            uint8_t can_loop = lexeme.type != TOK_GREATER;
            while(can_loop) {
                DataType * type = parser_parseTypeUnion(parser, node, NULL, currentScope);
                vec_push(&callExpr->callExpr->generics, type);

                CURRENT;
                if(lexeme.type == TOK_COMMA) {
                    ACCEPT;
                }
                else {
                    PARSER_ASSERT(lexeme.type == TOK_GREATER, "Line: %"PRIu16", Col: %"PRIu16" `>` expected but %s was found.", EXPAND_LEXEME);
                    ACCEPT;
                    can_loop = 0;
                }
            }

            // assert that the next token is a (
            CURRENT;
            PARSER_ASSERT(lexeme.type == TOK_LPAREN, "Line: %"PRIu16", Col: %"PRIu16" `(` expected but %s was found.", EXPAND_LEXEME);

            ACCEPT;
            CURRENT;
            can_loop = lexeme.type != TOK_RPAREN;

            while(can_loop) {
                Expr* index = parser_parseExpr(parser, node, currentScope);
                vec_push(&callExpr->callExpr->args, index);
                CURRENT;
                if(lexeme.type == TOK_COMMA) {
                    ACCEPT;
                }
                else {
                    PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.", EXPAND_LEXEME);
                    ACCEPT;
                    can_loop = 0;
                }
            }
            ACCEPT;

            // todo check datatype
            return callExpr;
        }


        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(BET_LT, NULL, NULL);

        if(lexeme.type == TOK_GREATER)
            binaryExpr->binaryExpr->type = BET_GT;
        else if(lexeme.type == TOK_LESS_EQUAL)
            binaryExpr->binaryExpr->type = BET_LTE;
        else if(lexeme.type == TOK_GREATER_EQUAL)
            binaryExpr->binaryExpr->type = BET_GTE;


        Expr* rhs = parser_parseOpCompare(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    else if (lexeme.type == TOK_TYPE_CONVERSION) {
        ACCEPT;
        Expr* castExpr = ast_expr_makeExpr(ET_CAST);
        castExpr->castExpr = ast_expr_makeCastExpr(NULL, NULL);
        castExpr->castExpr->expr = lhs;
        castExpr->castExpr->type = parser_parseTypeUnion(parser, node, NULL, currentScope);
        return castExpr;
    }

    else if (lexeme.type == TOK_IS) {
        ACCEPT;
        Expr* checkExpr = ast_expr_makeExpr(ET_INSTANCE_CHECK);
        checkExpr->instanceCheckExpr = ast_expr_makeInstanceCheckExpr(NULL, NULL);
        checkExpr->instanceCheckExpr->expr = lhs;
        checkExpr->instanceCheckExpr->type = parser_parseTypeUnion(parser, node, NULL, currentScope);
        return  checkExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpShift(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Expr* lhs = parser_parseAdd(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_RIGHT_SHIFT || lexeme.type == TOK_LEFT_SHIFT) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(lexeme.type == TOK_RIGHT_SHIFT?BET_RSHIFT:BET_LSHIFT, NULL, NULL);

        Expr* rhs = parser_parseOpShift(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseAdd(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Expr* lhs = parser_parseOpMult(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_PLUS || lexeme.type == TOK_MINUS) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(lexeme.type == TOK_PLUS?BET_ADD:BET_SUB, NULL, NULL);

        Expr* rhs = parser_parseAdd(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpMult(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Expr* lhs = parser_parseOpUnary(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_STAR || lexeme.type == TOK_DIV || lexeme.type == TOK_PERCENT) {
        ACCEPT;
        Expr *binaryExpr = ast_expr_makeExpr(ET_BINARY);
        binaryExpr->binaryExpr = ast_expr_makeBinaryExpr(lexeme.type == TOK_STAR?BET_MUL:BET_DIV, NULL, NULL);
        if(lexeme.type == TOK_PERCENT)
            binaryExpr->binaryExpr->type = BET_MOD;

        Expr* rhs = parser_parseOpMult(parser, node, currentScope);
        binaryExpr->binaryExpr->lhs = lhs;
        binaryExpr->binaryExpr->rhs = rhs;

        // Todo: compute data type
        //binaryExpr->dataType

        return binaryExpr;
    }

    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpUnary(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    // check if we have prefix op
    Lexeme CURRENT;
    if (lexeme.type == TOK_STAR || lexeme.type == TOK_MINUS || lexeme.type == TOK_BITWISE_NOT ||
        lexeme.type == TOK_NOT || lexeme.type == TOK_INCREMENT || lexeme.type == TOK_DECREMENT ||
        lexeme.type == TOK_DENULL || lexeme.type == TOK_BITWISE_AND) {
        ACCEPT;
        Expr *unaryExpr = ast_expr_makeExpr(ET_UNARY);

        unaryExpr->unaryExpr = ast_expr_makeUnaryExpr(UET_DEREF, NULL);
        if(lexeme.type == TOK_MINUS)
            unaryExpr->unaryExpr->type = UET_NEG;
        else if(lexeme.type == TOK_BITWISE_NOT)
            unaryExpr->unaryExpr->type = UET_BIT_NOT;
        else if(lexeme.type == TOK_NOT)
            unaryExpr->unaryExpr->type = UET_NOT;
        else if(lexeme.type == TOK_INCREMENT)
            unaryExpr->unaryExpr->type = UET_PRE_INC;
        else if(lexeme.type == TOK_DECREMENT)
            unaryExpr->unaryExpr->type = UET_PRE_DEC;
        else if(lexeme.type == TOK_DENULL)
            unaryExpr->unaryExpr->type = UET_DENULL;
        else if(lexeme.type == TOK_BITWISE_AND)
            unaryExpr->unaryExpr->type = UET_ADDRESS_OF;

        Expr *uhs = parser_parseOpUnary(parser, node, currentScope);
        unaryExpr->unaryExpr->uhs = uhs;

        return unaryExpr;
    }
    else if (lexeme.type == TOK_NEW) {
        Expr* new = ast_expr_makeExpr(ET_NEW);
        ACCEPT;
        // parse type
        DataType* dt = parser_parseTypeUnion(parser, node, NULL, currentScope);
        new->newExpr = ast_expr_makeNewExpr(dt);

        // assert dt not NULL
        PARSER_ASSERT(dt != NULL, "Line: %"PRIu16", Col: %"PRIu16" `new` datatype returned NULL, this is a parser issue", lexeme.line, lexeme.col);
        CURRENT;
        // check for "("
        if(lexeme.type == TOK_LPAREN) {
            ACCEPT;
            CURRENT;
            uint8_t can_loop = lexeme.type != TOK_RPAREN;
            while(can_loop){
                Expr* arg = parser_parseExpr(parser, node, currentScope);
                // assert arg not null
                PARSER_ASSERT(arg != NULL, "Line: %"PRIu16", Col: %"PRIu16" `new` argument returned NULL, this is a parser issue", lexeme.line, lexeme.col);

                vec_push(&new->newExpr->args, arg);

                CURRENT;
                if(lexeme.type == TOK_COMMA) {
                    ACCEPT;
                }
                else {
                    // check for ")"
                    PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but `%s` was found", lexeme.line, lexeme.col);
                    can_loop = 0;
                }
            }
            ACCEPT;

            return new;
        }
        else {
            parser_reject(parser);
            return NULL;
        }
    }
    else if (lexeme.type == TOK_SPAWN){
        // prepare expr
        Expr* spawn = ast_expr_makeExpr(ET_SPAWN);
        ACCEPT;
        // prepare spawn struct
        spawn->spawnExpr = ast_expr_makeSpawnExpr();
        CURRENT;
        if(lexeme.type == TOK_PROCESS_LINK){
            ACCEPT;
            spawn->spawnExpr->expr = parser_parseExpr(parser, node, currentScope);
            return spawn;
        }
        else {
            parser_reject(parser);
            spawn->spawnExpr->callback = parser_parseExpr(parser, node, currentScope);
            CURRENT;
            PARSER_ASSERT(lexeme.type == TOK_PROCESS_LINK, "Line: %"PRIu16", Col: %"PRIu16" `->` expected but `%s` was found", lexeme.line, lexeme.col);
            ACCEPT;
            spawn->spawnExpr->expr = parser_parseExpr(parser, node, currentScope);
            // TODO: assert callback is valid and expr is of type process

            return spawn;
        }
    }
    else if (lexeme.type == TOK_EMIT){
        Expr* emit = ast_expr_makeExpr(ET_EMIT);
        ACCEPT;
        emit->emitExpr = ast_expr_makeEmitExpr(NULL);
        CURRENT;
        if(lexeme.type == TOK_PROCESS_LINK){
            ACCEPT;
            emit->emitExpr->msg = parser_parseExpr(parser, node, currentScope);
            return emit;
        }
        else {
            parser_reject(parser);
            emit->emitExpr->process = parser_parseExpr(parser, node, currentScope);
            CURRENT;
            PARSER_ASSERT(lexeme.type == TOK_PROCESS_LINK, "Line: %"PRIu16", Col: %"PRIu16" `->` expected but `%s` was found", lexeme.line, lexeme.col);
            ACCEPT;
            emit->emitExpr->msg = parser_parseExpr(parser, node, currentScope);
            return  emit;
        }
    }

    parser_reject(parser);
    Expr* uhs = parser_parseOpPointer(parser, node, currentScope);
    Expr *unaryExpr = ast_expr_makeExpr(ET_UNARY);
    CURRENT;

    if(lexeme.type == TOK_INCREMENT || lexeme.type == TOK_DECREMENT) {
        ACCEPT;
        unaryExpr->unaryExpr = ast_expr_makeUnaryExpr(lexeme.type == TOK_INCREMENT?UET_POST_INC:UET_POST_DEC, NULL);
        unaryExpr->unaryExpr->uhs = uhs;

        return unaryExpr;
    }

    if (lexeme.type == TOK_TYPE_CONVERSION) {
        ACCEPT;
        // TODO parse type
        PARSER_ASSERT(1==0, "`type conversion` operation is not yet implemented");
        return NULL;
    }

    parser_reject(parser);
    return uhs;
}

Expr* parser_parseOpPointer(Parser* parser, ASTNode* node, ASTScope* currentScope){

    Expr* lhs = parser_parseOpValue(parser, node, currentScope);
    if(lhs == NULL){
        return NULL;
    }
    Lexeme CURRENT;
    if(lexeme.type == TOK_DOT) {
        ACCEPT;
        Expr* rhs = parser_parseOpPointer(parser, node, currentScope);
        MemberAccessExpr *memberAccessExpr = ast_expr_makeMemberAccessExpr(lhs, rhs);

        Expr* memberExpr = ast_expr_makeExpr(ET_MEMBER_ACCESS);
        memberExpr->memberAccessExpr = memberAccessExpr;
        return memberExpr;
    }
    if(lexeme.type == TOK_LBRACKET) {
        ACCEPT;
        IndexAccessExpr* idx = ast_expr_makeIndexAccessExpr(lhs);
        uint8_t can_loop = 1;

        while(can_loop) {
            Expr* index = parser_parseExpr(parser, node, currentScope);
            vec_push(&idx->indexes, index);
            CURRENT;
            if(lexeme.type == TOK_COMMA) {
                ACCEPT;
            }
            else {
                can_loop = 0;
                parser_reject(parser);
            }
        }
        // assert ]
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_RBRACKET, "Line: %"PRIu16", Col: %"PRIu16" `]` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        Expr* expr = ast_expr_makeExpr(ET_INDEX_ACCESS);
        expr->indexAccessExpr = idx;

        // todo check datatype
        return expr;
    }

    if(lexeme.type == TOK_LPAREN) {
        ACCEPT;
        CURRENT;
        CallExpr* call = ast_expr_makeCallExpr(lhs);
        uint8_t can_loop = lexeme.type != TOK_RPAREN;

        while(can_loop) {
            parser_reject(parser);
            Expr* index = parser_parseExpr(parser, node, currentScope);
            vec_push(&call->args, index);
            CURRENT;
            if(lexeme.type == TOK_COMMA) {
                ACCEPT;
            }
            else {
                PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.", EXPAND_LEXEME);
                ACCEPT;
                can_loop = 0;
            }
        }
        ACCEPT;
        Expr* expr = ast_expr_makeExpr(ET_CALL);
        expr->callExpr = call;

        // todo check datatype
        return expr;
    }
    parser_reject(parser);
    return lhs;
}

Expr* parser_parseOpValue(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Lexeme CURRENT;
    if(lexeme.type == TOK_IDENTIFIER){
        Expr* expr = ast_expr_makeExpr(ET_ELEMENT);
        expr->elementExpr = ast_expr_makeElementExpr(lexeme.string);
        ACCEPT;

        return expr;
    }
    if(lexeme.type == TOK_FN){
        // lambda expression
        parser_reject(parser);
        FnHeader * fnHeader= parser_parseLambdaFnHeader(parser, node, NULL, currentScope);

        Expr* expr = ast_expr_makeExpr(ET_LAMBDA);
        expr->lambdaExpr = ast_expr_makeLambdaExpr(currentScope);
        expr->lambdaExpr->header = fnHeader;
        /**
         * TODO: add args to scope and detect closures
         */

        CURRENT;
        if(lexeme.type == TOK_EQUAL){
            expr->lambdaExpr->bodyType = FBT_EXPR;
            ACCEPT;
            expr->lambdaExpr->expr = parser_parseExpr(parser, node, expr->lambdaExpr->scope);

            return expr;
        }
        else{
            // assert {
            PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` or `=` expected but %s was found.", EXPAND_LEXEME);
            expr->lambdaExpr->bodyType = FBT_BLOCK;
            parser_reject(parser);
            expr->lambdaExpr->block = parser_parseStmtBlock(parser, node, expr->lambdaExpr->scope);
            return expr;
        }

    }
    if(lexeme.type == TOK_LPAREN) {
        ACCEPT;

        Expr* expr = parser_parseExpr(parser, node, currentScope);
        // assert )
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        return expr;
    }
    // check for array construction [
    if(lexeme.type == TOK_LBRACKET) {
        ACCEPT;
        CURRENT;
        ArrayConstructionExpr* arrayConstructionExpr = ast_expr_makeArrayConstructionExpr();
        uint8_t can_loop = lexeme.type != TOK_RBRACKET;

        while(can_loop) {
            Expr* index = parser_parseExpr(parser, node, currentScope);
            vec_push(&arrayConstructionExpr->args, index);
            CURRENT;
            if(lexeme.type == TOK_COMMA) {
                ACCEPT;
            }
            else {
                can_loop = 0;
                parser_reject(parser);
                CURRENT;
            }
        }
        // assert ]
        PARSER_ASSERT(lexeme.type == TOK_RBRACKET, "Line: %"PRIu16", Col: %"PRIu16" `]` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        Expr* expr = ast_expr_makeExpr(ET_ARRAY_CONSTRUCTION);
        expr->arrayConstructionExpr = arrayConstructionExpr;

        // todo check datatype
        return expr;
    }
    // check if we have a "{"
    if(lexeme.type == TOK_LBRACE) {
        ACCEPT;
        // we need to look a head find an ID and ":", then its named
        // else its unnamed
        CURRENT;
        if(lexeme.type == TOK_IDENTIFIER){
            CURRENT;
            if(lexeme.type == TOK_COLON){
                // build expressions
                NamedStructConstructionExpr* namedStruct = ast_expr_makeNamedStructConstructionExpr();
                Expr* expr = ast_expr_makeExpr(ET_NAMED_STRUCT_CONSTRUCTION);
                expr->namedStructConstructionExpr = namedStruct;

                parser_reject(parser);
                // we are back at the identifier
                uint8_t loop = 1;
                while(loop) {
                    CURRENT;
                    // we make sure format is <id>":"<expr> (","<id>":"<expr>)*
                    // we parse the id
                    PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
                    char* argName = strdup(lexeme.string);
                    ACCEPT;
                    // we assert ":"
                    CURRENT;
                    PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.", EXPAND_LEXEME);
                    ACCEPT;

                    Expr* value = parser_parseExpr(parser, node, currentScope);
                    // we add the arg to the struct
                    vec_push(&namedStruct->argNames, argName);
                    map_set(&namedStruct->args, argName, value);
                    // we check if we have a "," or a "}"
                    CURRENT;
                    if(lexeme.type == TOK_COMMA) {
                        ACCEPT;
                    }
                    else if(lexeme.type == TOK_RBRACE) {
                        ACCEPT;
                        loop = 0;
                    }
                }

                return expr;
            }
            else {
                parser_reject(parser);
            }
        }
        else{
            parser_reject(parser);
        }

        // unnamed struct
        UnnamedStructConstructionExpr* unnamedStruct = ast_expr_makeUnnamedStructConstructionExpr();
        Expr* expr = ast_expr_makeExpr(ET_UNNAMED_STRUCT_CONSTRUCTION);
        // prepare loop
        uint8_t can_loop = 1;
        while(can_loop) {
            Expr* index = parser_parseExpr(parser, node, currentScope);
            vec_push(&unnamedStruct->args, index);
            CURRENT;
            if(lexeme.type == TOK_COMMA) {
                ACCEPT;
            }
            else {
                // assert }
                PARSER_ASSERT(lexeme.type == TOK_RBRACE, "Line: %"PRIu16", Col: %"PRIu16" `}` expected but %s was found.", EXPAND_LEXEME);
                ACCEPT;
                can_loop = 0;
                parser_reject(parser);
            }
        }
        expr->unnamedStructConstructionExpr = unnamedStruct;
        return expr;
    }
    if(lexeme.type == TOK_UNSAFE){
        // build unsafe expr
        UnsafeExpr* unsafeExpr = ast_expr_makeUnsafeExpr(currentScope);
        Expr* expr = ast_expr_makeExpr(ET_UNSAFE);
        expr->unsafeExpr = unsafeExpr;

        ACCEPT;
        // assert "("
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_LPAREN, "Line: %"PRIu16", Col: %"PRIu16" `(` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        expr->unsafeExpr->expr = parser_parseExpr(parser, node, currentScope);
        // assert ")"
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;

        return expr;
    }
    if(lexeme.type == TOK_IF) {
        // create IfElseExpr
        IfElseExpr* ifElseExpr = ast_expr_makeIfElseExpr();
        ACCEPT;
        ifElseExpr->condition = parser_parseExpr(parser, node, currentScope);
        // assert {
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        ifElseExpr->ifExpr = parser_parseExpr(parser, node, currentScope);
        // assert }
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_RBRACE, "Line: %"PRIu16", Col: %"PRIu16" `}` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        CURRENT;
        // assert else
        PARSER_ASSERT(lexeme.type == TOK_ELSE, "Line: %"PRIu16", Col: %"PRIu16" `else` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        // assert {
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        ifElseExpr->elseExpr = parser_parseExpr(parser, node, currentScope);
        CURRENT;
        // assert }
        PARSER_ASSERT(lexeme.type == TOK_RBRACE, "Line: %"PRIu16", Col: %"PRIu16" `}` expected but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        // build expr
        Expr* expr = ast_expr_makeExpr(ET_IF_ELSE);
        expr->ifElseExpr = ifElseExpr;
        return expr;
    }
    parser_reject(parser);
    return parser_parseLiteral(parser, node, currentScope);
}


/*
    TOK_STRING_VAL,            // string literal  (double quotes)
    TOK_CHAR_VAL,              // single character (single quote)
    TOK_INT,
    TOK_BINARY_INT,         // 0b[01]+
    TOK_OCT_INT,           // 0o[0-7]+
    TOK_HEX_INT,           // 0x[0-9A-F]+
    TOK_FLOAT,             //
    TOK_DOUBLE,
 */
Expr* parser_parseLiteral(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Expr* expr = ast_expr_makeExpr(ET_LITERAL);
    expr->dataType = ast_type_makeType();

    expr->literalExpr = ast_expr_makeLiteralExpr(0);
    Lexeme lexeme = parser_peek(parser);

    ACCEPT;
    switch(lexeme.type){
        case TOK_STRING_VAL:
            expr->literalExpr->type = LT_STRING;
            expr->dataType->kind = DT_STRING;
            break;
        case TOK_CHAR_VAL:
            expr->literalExpr->type = LT_CHARACTER;
            expr->dataType->kind = DT_CHAR;
            break;
        case TOK_INT:
            expr->literalExpr->type = LT_INTEGER;
            // TODO maybe check the value to compute an accurate type ?
            expr->dataType->kind = DT_I32;
            break;
        case TOK_BINARY_INT:
            expr->literalExpr->type = LT_BINARY_INT;
            expr->dataType->kind = DT_U32;
            break;
        case TOK_OCT_INT:
            expr->literalExpr->type = LT_OCTAL_INT;
            expr->dataType->kind = DT_U32;
            break;
        case TOK_HEX_INT:
            expr->literalExpr->type = LT_HEX_INT;
            expr->dataType->kind = DT_U32;
            break;
        case TOK_FLOAT:
            expr->literalExpr->type = LT_FLOAT;
            expr->dataType->kind = DT_F32;
            break;
        case TOK_DOUBLE:
            expr->literalExpr->type = LT_DOUBLE;
            expr->dataType->kind = DT_F64;
            break;
        case TOK_TRUE:
            expr->literalExpr->type = LT_BOOLEAN;
            expr->dataType->kind = DT_BOOL;
        default:
            // TODO: free memory
            return NULL;
    }
    expr->literalExpr->value = strdup(lexeme.string);
    return expr;
}

/*
 * Terminal parsers
*/

PackageID* parser_parsePackage(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    PackageID * package = ast_makePackageID();
    Lexeme lexeme = parser_peek(parser);
    PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier/package` expected but %s was found.", EXPAND_LEXEME);
    vec_push(&package->ids, strdup(lexeme.string));

    ACCEPT;

    lexeme = parser_peek(parser);
    uint8_t can_go = lexeme.type == TOK_DOT;
    while(can_go) {
        lexeme = parser_peek(parser);
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier/package` expected but %s was found.", EXPAND_LEXEME);
        vec_push(&package->ids, strdup(lexeme.string));
        ACCEPT;

        lexeme = parser_peek(parser);
        can_go = lexeme.type == TOK_DOT;
    }

    parser_reject(parser);

    return package;
}

// parses fn header for interfaces, interfaces cannot have mut arguments, they are not allowed
// to mutate arguments given to them.
FnHeader* parser_parseFnHeader(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    // build header struct
    FnHeader* header = ast_makeFnHeader();
    header->type = ast_type_makeFn();
    // assert we are at "fn"
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_FN, "Line: %"PRIu16", Col: %"PRIu16" `fn` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;
    // assert we got a name
    PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
    header->name = strdup(lexeme.string);
    // accept name
    ACCEPT;

    // if we have "<" it's a generic
    CURRENT;
    if(lexeme.type == TOK_LESS) {
        header->isGeneric = 1;
        ACCEPT;
        // get current lexeme
        CURRENT;

        // prepare to loop
        uint8_t can_loop = lexeme.type == TOK_IDENTIFIER;
        uint32_t idx = 0;
        while(can_loop) {
            // assert its an ID
            PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected in function header but %s was found.", EXPAND_LEXEME);

            // add it to our generic list
            // make sure we do not have duplicates by getting the element of the map with the name and asserting it is null
            PARSER_ASSERT(map_get(&header->generics, lexeme.string) == NULL, "Line: %"PRIu16", Col: %"PRIu16" generic name `%s` already defined.", EXPAND_LEXEME);

            vec_push(&header->genericNames, strdup(lexeme.string));
            map_set(&header->generics, lexeme.string, idx++);

            // get current lexeme
            CURRENT;
            // assert we got a name

            // if we have a ">" we are done
            if(lexeme.type == TOK_GREATER) {
                can_loop = 0;
                ACCEPT;
            }
            else {
                PARSER_ASSERT(lexeme.type == TOK_COMMA, "Line: %"PRIu16", Col: %"PRIu16" `,` or `>` expected in generic list but %s was found.", EXPAND_LEXEME);
                ACCEPT;
                CURRENT;
            }
        }
    }
    // assert we have "("
    PARSER_ASSERT(lexeme.type == TOK_LPAREN, "Line: %"PRIu16", Col: %"PRIu16" `(` expected after function name but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    // we are going to parse the arguments
    CURRENT;
    uint8_t can_loop = lexeme.type == TOK_IDENTIFIER;
    // create fnHeader object
    while(can_loop) {
        // PARSER_ASSERT ID
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected for arg declaration but %s was found.", EXPAND_LEXEME);
        // accept ID
        char* name = strdup(lexeme.string);
        ACCEPT;
        CURRENT;
        // assert ":"
        PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected after arg name but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        CURRENT;
        // assert type
        DataType* type = parser_parseTypeUnion(parser, node, NULL, currentScope);

        // make sure arg doesn't already exist
        PARSER_ASSERT(map_get(&header->type->args, name) == NULL, "Line: %"PRIu16", Col: %"PRIu16" argument name `%s` already exists.", EXPAND_LEXEME);

        // make FnArg
        FnArgument * arg = ast_type_makeFnArgument();
        arg->type = type;
        arg->isMutable = 0;
        arg->name = name;

        // add arg to header
        vec_push(&header->type->argNames, name);
        map_set(&header->type->args, name, arg);

        // check if we reached ")"
        CURRENT;
        if(lexeme.type == TOK_RPAREN) {
            break;

        }
        else {
            PARSER_ASSERT(lexeme.type == TOK_COMMA, "Line: %"PRIu16", Col: %"PRIu16" `,` or `)` expected after arg declaration but %s was found.", EXPAND_LEXEME);
            ACCEPT;
            CURRENT;
        }
    }

    // do we have a return type?
    ACCEPT;
    CURRENT;
    if(lexeme.type == TOK_FN_RETURN_TYPE) {
        ACCEPT;
        header->type->returnType = parser_parseTypeUnion(parser, node, NULL, currentScope);
    }
    else{
        parser_reject(parser);
    }

    return header;
}

FnHeader* parser_parseLambdaFnHeader(Parser* parser, ASTNode* node, DataType* parentReferee, ASTScope* currentScope) {
    // build header struct
    FnHeader* header = ast_makeFnHeader();
    header->type = ast_type_makeFn();
    // assert we are at "fn"
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_FN, "Line: %"PRIu16", Col: %"PRIu16" `fn` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    // if we have "<" it's a generic
    CURRENT;
    if(lexeme.type == TOK_LESS) {
        header->isGeneric = 1;
        ACCEPT;
        // get current lexeme
        CURRENT;

        // prepare to loop
        uint8_t can_loop = lexeme.type == TOK_IDENTIFIER;
        uint32_t idx = 0;
        while(can_loop) {
            // assert its an ID
            PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected in function header but %s was found.", EXPAND_LEXEME);

            // add it to our generic list
            // make sure we do not have duplicates by getting the element of the map with the name and asserting it is null
            PARSER_ASSERT(map_get(&header->generics, lexeme.string) == NULL, "Line: %"PRIu16", Col: %"PRIu16" generic name `%s` already defined.", EXPAND_LEXEME);

            vec_push(&header->genericNames, strdup(lexeme.string));
            map_set(&header->generics, lexeme.string, idx++);

            // get current lexeme
            CURRENT;
            // assert we got a name

            // if we have a ">" we are done
            if(lexeme.type == TOK_GREATER) {
                can_loop = 0;
                ACCEPT;
                CURRENT;
            }
            else {
                PARSER_ASSERT(lexeme.type == TOK_COMMA, "Line: %"PRIu16", Col: %"PRIu16" `,` or `>` expected in generic list but %s was found.", EXPAND_LEXEME);
                ACCEPT;
                CURRENT;
            }
        }
    }
    // assert we have "("
    PARSER_ASSERT(lexeme.type == TOK_LPAREN, "Line: %"PRIu16", Col: %"PRIu16" `(` expected after function name but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    // we are going to parse the arguments
    CURRENT;
    uint8_t can_loop = lexeme.type == TOK_IDENTIFIER;
    // create fnHeader object
    while(can_loop) {
        // PARSER_ASSERT ID
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected for arg declaration but %s was found.", EXPAND_LEXEME);
        // accept ID
        char* name = strdup(lexeme.string);
        ACCEPT;
        CURRENT;
        // assert ":"
        PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected after arg name but %s was found.", EXPAND_LEXEME);
        ACCEPT;
        CURRENT;
        // assert type
        DataType* type = parser_parseTypeUnion(parser, node, NULL, currentScope);

        // make sure arg doesn't already exist
        PARSER_ASSERT(map_get(&header->type->args, name) == NULL, "Line: %"PRIu16", Col: %"PRIu16" argument name `%s` already exists.", EXPAND_LEXEME);

        // make FnArg
        FnArgument * arg = ast_type_makeFnArgument();
        arg->type = type;
        arg->isMutable = 0;
        arg->name = name;

        // add arg to header
        vec_push(&header->type->argNames, name);
        map_set(&header->type->args, name, arg);

        // check if we reached ")"
        CURRENT;
        if(lexeme.type == TOK_RPAREN) {
            can_loop = 0;
            ACCEPT;
        }
        else {
            PARSER_ASSERT(lexeme.type == TOK_COMMA, "Line: %"PRIu16", Col: %"PRIu16" `,` or `)` expected after arg declaration but %s was found.", EXPAND_LEXEME);
            ACCEPT;
            CURRENT;
        }
    }

    // do we have a return type?
    CURRENT;
    if(lexeme.type == TOK_FN_RETURN_TYPE) {
        ACCEPT;
        header->type->returnType = parser_parseTypeUnion(parser, node, NULL, currentScope);
    }
    else{
        parser_reject(parser);
    }

    return header;
}

Statement* parser_parseStmt(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    // we check all the options. if no option is chosen, we parse expressions as a statement
    // if we have a return statement
    Lexeme CURRENT;
    // if we have a let statement
    if(lexeme.type == TOK_LET) {
        parser_reject(parser);
        return parser_parseStmtLet(parser, node, currentScope);
    }
    // if we have a function declaration
    else if(lexeme.type == TOK_FN) {
        parser_reject(parser);
        return parser_parseStmtFn(parser, node, currentScope);
    }
    // if we have an if statement
    else if(lexeme.type == TOK_IF) {
        parser_reject(parser);
        return parser_parseStmtIf(parser, node, currentScope);
    }
    // if we have a while statement
    else if(lexeme.type == TOK_WHILE) {
        parser_reject(parser);
        return parser_parseStmtWhile(parser, node, currentScope);
    }
    // check if we have do
    else if(lexeme.type == TOK_DO) {
        parser_reject(parser);
        return parser_parseStmtDoWhile(parser, node, currentScope);
    }
    // if we have a for statement
    else if(lexeme.type == TOK_FOR) {
        parser_reject(parser);
        return parser_parseStmtFor(parser, node, currentScope);
    }
    // if we have foreach
    else if(lexeme.type == TOK_FOREACH) {
        parser_reject(parser);
        return parser_parseStmtForEach(parser, node, currentScope);
    }
    // if we have a break statement
    else if(lexeme.type == TOK_BREAK) {
        parser_reject(parser);
        return parser_parseStmtBreak(parser, node, currentScope);
    }
    // if we have a continue statement
    else if(lexeme.type == TOK_CONTINUE) {
        parser_reject(parser);
        return parser_parseStmtContinue(parser, node, currentScope);
    }
    // if we have a return statement
    else if(lexeme.type == TOK_RETURN) {
        parser_reject(parser);
        return parser_parseStmtReturn(parser, node, currentScope);
    }
    // if we have a block statement
    else if(lexeme.type == TOK_LBRACE) {
        parser_reject(parser);
        return parser_parseStmtBlock(parser, node, currentScope);
    }
    // if we have a match
    else if(lexeme.type == TOK_MATCH) {
        parser_reject(parser);
        return parser_parseStmtMatch(parser, node, currentScope);
    }
    // if we have unsafe
    else if(lexeme.type == TOK_UNSAFE) {
        parser_reject(parser);
        return parser_parseStmtUnsafe(parser, node, currentScope);
    }
    // otherwise, we expect expr

    parser_reject(parser);
    return parser_parseStmtExpr(parser, node, currentScope);
}

Statement* parser_parseStmtLet(Parser* parser, ASTNode* node, ASTScope* currentScope){
    Statement* stmt = ast_stmt_makeStatement(ST_VAR_DECL);
    stmt->varDecl = ast_stmt_makeVarDeclStatement(currentScope);
    Lexeme CURRENT;
    // assert let
    PARSER_ASSERT(lexeme.type == TOK_LET, "Line: %"PRIu16", Col: %"PRIu16" `let` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    CURRENT;
    uint8_t loop_over_expr = 1;
    while (loop_over_expr) {
        LetExprDecl *letDecl = ast_expr_makeLetExprDecl();
        char *expect = NULL;

        // check if we have id or { or [
        if (lexeme.type == TOK_LBRACE) {
            expect = "]";
            letDecl->initializerType = LIT_STRUCT_DECONSTRUCTION;
            ACCEPT;
            CURRENT;
        } else if (lexeme.type == TOK_LBRACKET) {
            expect = "]";
            letDecl->initializerType = LIT_ARRAY_DECONSTRUCTION;
            ACCEPT;
            CURRENT;
        } else {
            expect = NULL;
            letDecl->initializerType = LIT_NONE;
        }

        uint8_t loop = 1;
        while (loop) {
            FnArgument *var = ast_type_makeFnArgument();
            // check if we have a mut
            if (lexeme.type == TOK_MUT) {
                var->isMutable = 1;
                ACCEPT;
                CURRENT;
            }
            // assert ID
            PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
                   "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);
            var->name = strdup(lexeme.string);
            ACCEPT;
            CURRENT;
            if(lexeme.type == TOK_COLON){
                // assert ":"
                PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.",
                       EXPAND_LEXEME);
                ACCEPT;
                // parse type
                var->type = parser_parseTypeUnion(parser, node, NULL, currentScope);
                // assert type is not null
                PARSER_ASSERT(var->type != NULL,
                       "Line: %"PRIu16", Col: %"PRIu16" `type` near %s, generated a NULL type. This is a parser issue.",
                       EXPAND_LEXEME);
                CURRENT;
            }
            // add to args
            map_set(&letDecl->variables, var->name, var);
            vec_push(&letDecl->variableNames, var->name);

            // check if we have a comma

            if (lexeme.type == TOK_COMMA) {
                ACCEPT;
                CURRENT;
            } else {
                parser_reject(parser);
                loop = 0;
            }
        }
        // if we expected something earlier, we must find it closed now
        if (expect != NULL) {
            CURRENT;
            // if we expected "]", we must find "]"
            if (expect[0] == '[') {
                // assert "]"
                PARSER_ASSERT(lexeme.type == TOK_RBRACKET,
                       "Line: %"PRIu16", Col: %"PRIu16" `]` expected but %s was found.",
                       EXPAND_LEXEME);
                ACCEPT;
                CURRENT;
            } else if (expect[0] == '{') {
                // assert "]"
                PARSER_ASSERT(lexeme.type == TOK_RBRACE, "Line: %"PRIu16", Col: %"PRIu16" `]` expected but %s was found.",
                       EXPAND_LEXEME);
                ACCEPT;
                CURRENT;
            }
        }

        CURRENT;
        // assert "="
        PARSER_ASSERT(lexeme.type == TOK_EQUAL, "Line: %"PRIu16", Col: %"PRIu16" `=` expected but %s was found.",
               EXPAND_LEXEME);
        ACCEPT;

        Expr *initializer = parser_parseExpr(parser, node, currentScope);
        letDecl->initializer = initializer;
        // assert initializer is not null
        PARSER_ASSERT(initializer != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `uhs` near %s, generated a NULL uhs. This is a parser issue.",
               EXPAND_LEXEME);
        // add the decl to the let uhs
        vec_push(&stmt->varDecl->letList, letDecl);

        CURRENT;
        // check if we have a comma
        if (lexeme.type == TOK_COMMA) {
            ACCEPT;
            CURRENT;
        } else {
            parser_reject(parser);
            loop_over_expr = 0;
        }
    }
    return stmt;
}

Statement* parser_parseStmtFn(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    Statement* stmt = ast_stmt_makeStatement(ST_FN_DECL);
    stmt->fnDecl = ast_stmt_makeFnDeclStatement(currentScope);
    // create the header
    stmt->fnDecl->header = ast_makeFnHeader();

    Lexeme CURRENT;
    // assert fn
    PARSER_ASSERT(lexeme.type == TOK_FN, "Line: %"PRIu16", Col: %"PRIu16" `fn` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    CURRENT;
    // assert ID
    PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.",
           EXPAND_LEXEME);
    stmt->fnDecl->header->name = strdup(lexeme.string);
    ACCEPT;
    CURRENT;


    if(lexeme.type == TOK_LESS) {
        // GENERICS!
        stmt->fnDecl->hasGeneric = 1;
        ACCEPT;
        CURRENT;
        uint8_t can_loop = 1;
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
               "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.", EXPAND_LEXEME);

        while(can_loop) {
            // init generic param
            GenericParam * genericParam = ast_make_genericParam();
            genericParam->isGeneric = 1;

            // in a type declaration, all given templates in the referee are generic and not concrete.
            PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER,
                   "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.",
                   EXPAND_LEXEME);

            // get generic name
            genericParam->name = strdup(lexeme.string);
            ACCEPT;
            CURRENT;
            // check if have ":"
            if(lexeme.type == TOK_COLON) {
                ACCEPT;
                CURRENT;
                // get generic type
                genericParam->constraint = parser_parseTypeUnion(parser, node, NULL, currentScope);
                CURRENT;
            }
            else {
                // if not, set to any
                genericParam->constraint = NULL;
            }

            // add generic param
            vec_push(&stmt->fnDecl->genericParams, genericParam);
            if(lexeme.type == TOK_GREATER){
                can_loop = 0;
                ACCEPT;
                CURRENT;
            }

            else {
                PARSER_ASSERT(lexeme.type == TOK_COMMA,
                       "Line: %"PRIu16", Col: %"PRIu16" `,` expected but %s was found.",
                       EXPAND_LEXEME);
                ACCEPT;
                CURRENT;
            }
        }
    }

    // assert "("
    PARSER_ASSERT(lexeme.type == TOK_LPAREN, "Line: %"PRIu16", Col: %"PRIu16" `(` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    CURRENT;
    uint8_t loop = lexeme.type != TOK_RPAREN;
    while (loop) {
        FnArgument *arg = ast_type_makeFnArgument();
        // check if we have a mut
        if (lexeme.type == TOK_MUT) {
            arg->isMutable = 1;
            ACCEPT;
            CURRENT;
        }
        // assert ID
        PARSER_ASSERT(lexeme.type == TOK_IDENTIFIER, "Line: %"PRIu16", Col: %"PRIu16" `identifier` expected but %s was found.",
               EXPAND_LEXEME);
        arg->name = strdup(lexeme.string);
        ACCEPT;
        CURRENT;
        // assert ":"
        PARSER_ASSERT(lexeme.type == TOK_COLON, "Line: %"PRIu16", Col: %"PRIu16" `:` expected but %s was found.",
               EXPAND_LEXEME);
        ACCEPT;
        // parse type
        arg->type = parser_parseTypeUnion(parser, node, NULL, currentScope);
        // assert type is not null
        PARSER_ASSERT(arg->type != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `type` near %s, generated a NULL type. This is a parser issue.",
               EXPAND_LEXEME);
        // add to args
        map_set(&stmt->fnDecl->header->type->args, arg->name, arg);
        vec_push(&stmt->fnDecl->header->type->argNames, arg->name);

        // check if we have a comma
        lexeme = parser_peek(parser);
        if (lexeme.type == TOK_COMMA) {
            ACCEPT;
            CURRENT;
        } else {
            PARSER_ASSERT(lexeme.type == TOK_RPAREN, "Line: %"PRIu16", Col: %"PRIu16" `)` expected but %s was found.", EXPAND_LEXEME);
            loop = 0;
        }
    }
    ACCEPT;
    CURRENT;

    if (lexeme.type == TOK_FN_RETURN_TYPE) {
        ACCEPT;
        // parse the return type
        // TODO: add generics to scope if they exist?
        stmt->fnDecl->header->type->returnType = parser_parseTypeUnion(parser, node, NULL, currentScope);
        // assert type is not null
        PARSER_ASSERT(stmt->fnDecl->header->type->returnType != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `type` near %s, generated a NULL type. This is a parser issue.",
               EXPAND_LEXEME);
        ACCEPT;
        CURRENT;

    }

    // check if we have `=` or `{`
    //lexeme = parser_peek(parser);
    if (lexeme.type == TOK_EQUAL) {
        stmt->fnDecl->bodyType = FBT_EXPR;
        ACCEPT;
        CURRENT;
        // parse the body
        // TODO: update current
        stmt->fnDecl->expr = parser_parseExpr(parser, node, stmt->fnDecl->scope);
        // assert body is not null
        PARSER_ASSERT(stmt->fnDecl->expr != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `body` near %s, generated a NULL body. This is a parser issue.",
               EXPAND_LEXEME);
        ACCEPT;
        CURRENT;
    } else if (lexeme.type == TOK_LBRACE) {
        stmt->fnDecl->bodyType = FBT_BLOCK;
        parser_reject(parser);
        stmt->fnDecl->block = parser_parseStmtBlock(parser, node, stmt->fnDecl->scope);
    } else {
        parser_reject(parser);
        // assert false
        PARSER_ASSERT(0, "Line: %"PRIu16", Col: %"PRIu16" `=` or `{` expected but %s was found.", EXPAND_LEXEME);
    }
    return stmt;
}

Statement* parser_parseStmtBlock(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // create statement instance
    Statement* stmt = ast_stmt_makeStatement(ST_BLOCK);
    stmt->blockStmt = ast_stmt_makeBlockStatement(currentScope);
    // assert {
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;

    uint8_t loop = 1;

    while(loop) {
        Statement * s = parser_parseStmt(parser, node, stmt->blockStmt->scope);
        PARSER_ASSERT(s != NULL, "Line: %"PRIu16", Col: %"PRIu16" `statement` near %s, generated a NULL statement. This is a parser issue.", EXPAND_LEXEME);
        vec_push(&stmt->blockStmt->stmts, s);
        // TODO: free s

        CURRENT;
        if(lexeme.type == TOK_RBRACE || lexeme.type == TOK_EOF){
            ACCEPT;
            loop = 0;
        }
        else {
            loop++;
            parser_reject(parser);
        }
    }

    return stmt;
}

Statement* parser_parseStmtIf(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // create statement instance
    Statement* stmt = ast_stmt_makeStatement(ST_IF_CHAIN);
    stmt->ifChain = ast_stmt_makeIfChainStatement();
    // assert if
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_IF, "Line: %"PRIu16", Col: %"PRIu16" `if` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    CURRENT;
    // prepare to loop
    uint8_t loop = 1;
    /*
     * if true {} else if true {} else {}
     * we parse it as 2 conditions and 2 blocks, final else is separate
     */
    while (loop) {
        Expr* condition = parser_parseExpr(parser, node, currentScope);
        // assert condition is not null
        PARSER_ASSERT(condition != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `condition` near %s, generated a NULL condition. This is a parser issue.",
               EXPAND_LEXEME);
        // next token


        Statement* block = parser_parseStmtBlock(parser, node, currentScope);

        vec_push(&stmt->ifChain->conditions, condition);
        vec_push(&stmt->ifChain->blocks, block);

        CURRENT;

        if(lexeme.type == TOK_ELSE){
            ACCEPT;
            CURRENT;
            if(lexeme.type == TOK_IF){
                ACCEPT;
                CURRENT;
            }
            else{
                parser_reject(parser);
                stmt->ifChain->elseBlock = parser_parseStmtBlock(parser, node, currentScope);
                loop = 0;
            }
        }
        else {
            parser_reject(parser);
            stmt->ifChain->elseBlock = NULL;
            loop = 0;
        }
    }

    return stmt;
}
Statement* parser_parseStmtMatch(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // get current token
    Lexeme CURRENT;
    // assert match
    PARSER_ASSERT(lexeme.type == TOK_MATCH, "Line: %"PRIu16", Col: %"PRIu16" `match` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // create statement instance
    Statement* stmt = ast_stmt_makeStatement(ST_MATCH);
    stmt->match = ast_stmt_makeMatchStatement();
    // parse the main expression to be matched
    stmt->match->expr = parser_parseExpr(parser, node, currentScope);
    // assert expr is not null
    PARSER_ASSERT(stmt->match->expr != NULL,
           "Line: %"PRIu16", Col: %"PRIu16" `expr` near %s, generated a NULL expr. This is a parser issue.",
           EXPAND_LEXEME);
    // assert {
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // prepare to loop
    uint8_t loop = 1;
    while(loop) {
        CURRENT;
        // do we have a wildcard
        if (lexeme.type == TOK_WILDCARD){
            ACCEPT;
            stmt->match->elseBlock = parser_parseStmtBlock(parser, node, currentScope);
            loop = 0;
        }
        else {
            parser_reject(parser);
            // create CaseStatement
            CaseStatement *caseStmt = ast_stmt_makeCaseStatement();
            // parse the condition
            caseStmt->condition = parser_parseExpr(parser, node, currentScope);
            PARSER_ASSERT(caseStmt->condition != NULL,
                   "Line: %"PRIu16", Col: %"PRIu16" `condition` near %s, generated a NULL condition. This is a parser issue.",
                   EXPAND_LEXEME);
            // next token
            CURRENT;
            // assert {
            PARSER_ASSERT(lexeme.type == TOK_LBRACE, "Line: %"PRIu16", Col: %"PRIu16" `{` expected but %s was found.",
                   EXPAND_LEXEME);
            parser_reject(parser);
            // parse the block
            caseStmt->block = parser_parseStmtBlock(parser, node, currentScope);
            // assert block is not null
            PARSER_ASSERT(caseStmt->block != NULL,
                   "Line: %"PRIu16", Col: %"PRIu16" `block` near %s, generated a NULL block. This is a parser issue.",
                   EXPAND_LEXEME);
            vec_push(&stmt->match->cases, caseStmt);
        }

        CURRENT;
        if(lexeme.type == TOK_RBRACE){
            ACCEPT;
            loop = 0;
        }
        else
            parser_reject(parser);
    }

    return stmt;
}

Statement* parser_parseStmtWhile(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_WHILE);
    stmt->whileLoop = ast_stmt_makeWhileStatement();
    // assert while
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_WHILE, "Line: %"PRIu16", Col: %"PRIu16" `while` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // parse condition
    stmt->whileLoop->condition = parser_parseExpr(parser, node, currentScope);
    // assert condition is not null
    PARSER_ASSERT(stmt->whileLoop->condition != NULL,
           "Line: %"PRIu16", Col: %"PRIu16" `condition` near %s, generated a NULL condition. This is a parser issue.",
           EXPAND_LEXEME);
    // parse block
    stmt->whileLoop->block = parser_parseStmtBlock(parser, node, currentScope);
    // assert block is not null
    PARSER_ASSERT(stmt->whileLoop->block != NULL,
           "Line: %"PRIu16", Col: %"PRIu16" `block` near %s, generated a NULL block. This is a parser issue.",
           EXPAND_LEXEME);

    return stmt;
}

Statement* parser_parseStmtDoWhile(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_DO_WHILE);
    stmt->doWhileLoop = ast_stmt_makeDoWhileStatement();
    // assert do
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_DO, "Line: %"PRIu16", Col: %"PRIu16" `do` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // parse block
    stmt->doWhileLoop->block = parser_parseStmtBlock(parser, node, currentScope);
    // assert block is not null
    PARSER_ASSERT(stmt->doWhileLoop->block != NULL,
           "Line: %"PRIu16", Col: %"PRIu16" `block` near %s, generated a NULL block. This is a parser issue.",
           EXPAND_LEXEME);
    // assert while
    CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_WHILE, "Line: %"PRIu16", Col: %"PRIu16" `while` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // parse condition
    stmt->doWhileLoop->condition = parser_parseExpr(parser, node, currentScope);
    // assert condition is not null
    PARSER_ASSERT(stmt->doWhileLoop->condition != NULL,
           "Line: %"PRIu16", Col: %"PRIu16" `condition` near %s, generated a NULL condition. This is a parser issue.",
           EXPAND_LEXEME);

    return stmt;
}

Statement* parser_parseStmtFor(Parser* parser, ASTNode* node, ASTScope* currentScope) {
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_FOR);
    stmt->forLoop = ast_stmt_makeForStatement(currentScope);
    // assert for
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_FOR, "Line: %"PRIu16", Col: %"PRIu16" `for` expected but %s was found.", EXPAND_LEXEME);
    ACCEPT;
    // make sure we have initializer, i.e token is not ";"
    CURRENT;
    if(lexeme.type != TOK_SEMICOLON){
        parser_reject(parser);
        // parse initializer
        stmt->forLoop->initializer = parser_parseStmtLet(parser, node, currentScope);

        // assert initializer is not null
        PARSER_ASSERT(stmt->forLoop->initializer != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `initializer` near %s, generated a NULL initializer. This is a parser issue.",
               EXPAND_LEXEME);
        // assert token is ";"
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_SEMICOLON, "Line: %"PRIu16", Col: %"PRIu16" `;` expected but %s was found.",
               EXPAND_LEXEME);
        ACCEPT;
    }
    else{
        ACCEPT;
    }
    CURRENT;
    // now we parse the condition
    if(lexeme.type != TOK_SEMICOLON){
        parser_reject(parser);
        // parse condition
        stmt->forLoop->condition = parser_parseExpr(parser, node, currentScope);
        // assert condition is not null
        PARSER_ASSERT(stmt->forLoop->condition != NULL,
               "Line: %"PRIu16", Col: %"PRIu16" `condition` near %s, generated a NULL condition. This is a parser issue.",
               EXPAND_LEXEME);
        // assert token is ";"
        CURRENT;
        PARSER_ASSERT(lexeme.type == TOK_SEMICOLON, "Line: %"PRIu16", Col: %"PRIu16" `;` expected but %s was found.",
               EXPAND_LEXEME);
        ACCEPT;
    }
    else
        ACCEPT;

    // check if we have {
    CURRENT;
    if(lexeme.type != TOK_LBRACE) {

        parser_reject(parser);
        // now we parse increments as <expr>(","<expr>)*
        // prepare to loop
        uint8_t loop = 1;
        while(loop) {
            // parse one increment
            Expr* increment = parser_parseExpr(parser, node, currentScope);
            // assert increment is not null
            PARSER_ASSERT(increment != NULL,
                   "Line: %"PRIu16", Col: %"PRIu16" `increment` near %s, generated a NULL increment. This is a parser issue.",
                   EXPAND_LEXEME);
            // add increment to list
            vec_push(&stmt->forLoop->increments, increment);
            // check if we have a comma
            CURRENT;
            if(lexeme.type == TOK_COMMA)
                ACCEPT;
            else
                loop = 0;
        }
    }
    parser_reject(parser);

    // current
    stmt->forLoop->block = parser_parseStmtBlock(parser, node, currentScope);
    // assert block is not null
    PARSER_ASSERT(stmt->forLoop->block != NULL,
           "Line: %"PRIu16", Col: %"PRIu16" `block` near %s, generated a NULL block. This is a parser issue.",
           EXPAND_LEXEME);
    return stmt;
}

Statement* parser_parseStmtForEach(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // throw not implemented error
    Lexeme CURRENT;
    PARSER_ASSERT(0, "Line: %"PRIu16", Col: %"PRIu16" `foreach` is not implemented yet.");
}

Statement* parser_parseStmtContinue(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_CONTINUE);
    stmt->continueStmt = ast_stmt_makeContinueStatement();
    // assert continue
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_CONTINUE, "Line: %"PRIu16", Col: %"PRIu16" `continue` expected but %s was found.",
           EXPAND_LEXEME);
    ACCEPT;
    return stmt;
}

Statement* parser_parseStmtReturn(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_RETURN);
    stmt->returnStmt = ast_stmt_makeReturnStatement();
    // assert return
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_RETURN, "Line: %"PRIu16", Col: %"PRIu16" `return` expected but %s was found.",
           EXPAND_LEXEME);
    ACCEPT;
    // parse expression
    stmt->returnStmt->expr = parser_parseExpr(parser, node, currentScope);
    // return can be NULL.

    return stmt;
}

Statement* parser_parseStmtBreak(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_BREAK);
    stmt->breakStmt = ast_stmt_makeBreakStatement();
    // assert break
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_BREAK, "Line: %"PRIu16", Col: %"PRIu16" `break` expected but %s was found.",
           EXPAND_LEXEME);
    ACCEPT;
    return stmt;
}

Statement* parser_parseStmtUnsafe(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_UNSAFE);
    stmt->unsafeStmt = ast_stmt_makeUnsafeStatement();
    // assert unsafe
    Lexeme CURRENT;
    PARSER_ASSERT(lexeme.type == TOK_UNSAFE, "Line: %"PRIu16", Col: %"PRIu16" `unsafe` expected but %s was found.",
           EXPAND_LEXEME);
    ACCEPT;
    // parse block
    stmt->unsafeStmt->block = parser_parseStmtBlock(parser, node, currentScope);
    // assert block is not null
    PARSER_ASSERT(stmt->unsafeStmt->block != NULL,
           "Line: %"PRIu16", Col: %"PRIu16" `block` near %s, generated a NULL block. This is a parser issue.",
           EXPAND_LEXEME);
    return stmt;
}
Statement* parser_parseStmtExpr(Parser* parser, ASTNode* node, ASTScope* currentScope){
    // build statement
    Statement* stmt = ast_stmt_makeStatement(ST_EXPR);
    stmt->expr = ast_stmt_makeExprStatement();
    // parse expression
    stmt->expr->expr = parser_parseExpr(parser, node, currentScope);
    // expr can be null

    if(stmt->expr->expr == NULL){
        // TODO free memory
        free(stmt->expr);
        free(stmt);
        return NULL;
    }

    return stmt;
}

#undef CURRENT
#undef ACCEPT
#undef TTTS