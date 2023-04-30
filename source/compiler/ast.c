//
// Created by praisethemoon on 28.04.23.
//
#include <stdio.h>
#include "ast.h"
#include "../utils/vec.h"
#include "../utils/map.h"

#define ALLOC(v, t) t* v = malloc(sizeof(t))


ASTNode * ast_makeProgramNode() {
    ASTProgramNode* program = malloc(sizeof(ASTProgramNode));

    vec_init(&program->importStatements);

    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = AST_PROGRAM;
    node->programNode = program;
    map_init(&node->scope.dataTypes);

    return node;
}

PackageID* ast_makePackageID() {
    PackageID* package = malloc(sizeof(PackageID));
    vec_init(&package->ids);

    return package;
}

ImportStmt* ast_makeImportStmt(PackageID* source, PackageID* target, uint8_t hasAlias, char* alias) {
    uint32_t i; char** str;
    PackageID * full_path = ast_makePackageID();
    vec_foreach_ptr(&source->ids, str, i) {
        vec_push(&full_path->ids, strdup(*str));
    }
    vec_foreach_ptr(&target->ids, str, i) {
        vec_push(&full_path->ids, strdup(*str));
    }

    ImportStmt * importStmt = malloc(sizeof(ImportStmt));
    importStmt->hasAlias = hasAlias;
    importStmt->alias = alias != NULL? strdup(alias):NULL;
    importStmt->path = full_path;

    return importStmt;
}

char* ast_stringifyImport(ASTProgramNode* node, uint32_t index) {
    // returns string representation of the import statement at the given index
    // we concatenate the path and add "as" alias at the end if it has one
    ImportStmt* importStmt = node->importStatements.data[index];
    char* str = strdup("");
    uint32_t i; char** val;
    vec_foreach_ptr(&importStmt->path->ids, val, i) {
        str = realloc(str, strlen(str) + strlen(*val) + 1);
        strcat(str, *val);
        if(i != importStmt->path->ids.length - 1){
            str = realloc(str, strlen(str) + 1);
            strcat(str, ".");
        }
    }
    if(importStmt->hasAlias){
        str = realloc(str, strlen(str) + strlen(" as ") + strlen(importStmt->alias) + 1);
        strcat(str, " as ");
        strcat(str, importStmt->alias);
    }

    return str;
}

// returns a string representation of the type
char* ast_stringifyType(DataType* type){
    // create base string
    char* str = strdup("");
    if(type->name != NULL){
        str = realloc(str, strlen(str) + strlen(type->name) + 1);
        strcat(str, type->name);
    }
    // we check its kind
    switch (type->kind) {
        case DT_UNRESOLVED:
            str = realloc(str, strlen(str) + strlen("unresolved") + 1);
            strcat(str, "unresolved");
            break;
        case DT_I8:
            str = realloc(str, strlen(str) + strlen("i8") + 1);
            strcat(str, "i8");
            break;
        case DT_I16:
            str = realloc(str, strlen(str) + strlen("i16") + 1);
            strcat(str, "i16");
            break;
        case DT_I32:
            str = realloc(str, strlen(str) + strlen("i32") + 1);
            strcat(str, "i32");
            break;

        case DT_I64:
            str = realloc(str, strlen(str) + strlen("i64") + 1);
            strcat(str, "i64");
            break;
        case DT_U8:
            str = realloc(str, strlen(str) + strlen("u8") + 1);
            strcat(str, "u8");
            break;
        case DT_U16:
            str = realloc(str, strlen(str) + strlen("u16") + 1);
            strcat(str, "u16");
            break;
        case DT_U32:
            str = realloc(str, strlen(str) + strlen("u32") + 1);
            strcat(str, "u32");
            break;
        case DT_U64:
            str = realloc(str, strlen(str) + strlen("u64") + 1);
            strcat(str, "u64");
            break;
        case DT_F32:
            str = realloc(str, strlen(str) + strlen("f32") + 1);
            strcat(str, "f32");
            break;
        case DT_F64:
            str = realloc(str, strlen(str) + strlen("f64") + 1);
            strcat(str, "f64");
            break;
        case DT_BOOL:
            str = realloc(str, strlen(str) + strlen("bool") + 1);
            strcat(str, "bool");
            break;
        case DT_CHAR:
            str = realloc(str, strlen(str) + strlen("char") + 1);
            strcat(str, "char");
            break;
        case DT_STRING:
            str = realloc(str, strlen(str) + strlen("string") + 1);
            strcat(str, "string");
            break;
        case DT_ARRAY:
            // we print it in the form of array<type, size>
            str = realloc(str, strlen(str) + strlen("array") + 1);
            strcat(str, "array");
            str = realloc(str, strlen(str) + strlen("<") + 1);
            strcat(str, "[");
            char* arrayType = ast_stringifyType(type->arrayType->arrayOf);
            str = realloc(str, strlen(str) + strlen(arrayType) + 1);
            strcat(str, arrayType);
            str = realloc(str, strlen(str) + strlen(",") + 1);
            strcat(str, ",");
            char* arraySize = malloc(20);
            sprintf(arraySize, "%llu", type->arrayType->len);
            str = realloc(str, strlen(str) + strlen(arraySize) + 1);
            strcat(str, arraySize);
            str = realloc(str, strlen(str) + strlen(">") + 1);
            strcat(str, "]");

            break;
        case DT_ENUM:
            str = realloc(str, strlen(str) + strlen("enum") + 1);
            strcat(str, "enum");
            // we add all fields in the form of enum{field1, field2, field3}
            str = realloc(str, strlen(str) + strlen("{") + 1);
            strcat(str, "{");
            int i; char * val;
            vec_foreach(&type->enumType->enumNames, val, i) {
                str = realloc(str, strlen(str) + strlen(val) + 1);
                strcat(str, val);
                str = realloc(str, strlen(str) + strlen(",") + 1);
                strcat(str, ",");
            }
            // replace last , with }
            str[strlen(str) - 1] = '}';

            break;
        case DT_REFERENCE:
            // we check if package is null, if its not we add it to the string as <p1.p2.p3>
            if(type->refType->pkg != NULL){
                str = realloc(str, strlen(str) + strlen("ref(") + 1);
                strcat(str, "ref(");
                // we loop
                int i; char * val;
                vec_foreach(&type->refType->pkg->ids, val, i) {
                    str = realloc(str, strlen(str) + strlen(val) + 1);
                    strcat(str, val);
                    str = realloc(str, strlen(str) + strlen(".") + 1);
                    strcat(str, ".");
                }
                // we replace the last . with >
                str[strlen(str) - 1] = ')';
            }
            else {
                // we throw an error if the ref is nul
                if(type->refType->ref == NULL){
                    printf("Error: reference type is null\n");
                    exit(1);
                }

                // we add the name of the reference type, or "anynomous" if the name is null
                if(type->refType->ref->name == NULL){
                    str = realloc(str, strlen(str) + strlen("anynomous") + 1);
                    strcat(str, "anynomous");
                }
                else {
                    str = realloc(str, strlen(str) + strlen(type->refType->ref->name) + 1);
                    strcat(str, type->refType->ref->name);
                }
            }
            break;
        case DT_TYPE_UNION: {
            // we print union in the form of union{type1, type2, type3}
            str = realloc(str, strlen(str) + strlen("union") + 1);
            strcat(str, "union");
            str = realloc(str, strlen(str) + strlen("{") + 1);
            strcat(str, "{");
            int i;
            char *val;
            vec_foreach(&type->unionType->unions, val, i) {
                char *unionType = ast_stringifyType(val);
                str = realloc(str, strlen(str) + strlen(unionType) + 1);
                strcat(str, unionType);
                str = realloc(str, strlen(str) + strlen(",") + 1);
                strcat(str, ",");
            }
            // replace last , with }
            str[strlen(str) - 1] = '}';
            break;
        }
        case DT_TYPE_JOIN: {
            // same as we did to union but with join
            str = realloc(str, strlen(str) + strlen("join") + 1);
            strcat(str, "join");
            str = realloc(str, strlen(str) + strlen("{") + 1);
            strcat(str, "{");
            int i;
            char *val;
            vec_foreach(&type->joinType->joins, val, i) {
                char *joinType = ast_stringifyType(val);
                str = realloc(str, strlen(str) + strlen(joinType) + 1);
                strcat(str, joinType);
                str = realloc(str, strlen(str) + strlen(",") + 1);
                strcat(str, ",");
            }
            // replace last , with }
            str[strlen(str) - 1] = '}';
            break;

        }
        case DT_STRUCT:{
            // we add all struct fields to string
            str = realloc(str, strlen(str) + strlen("struct") + 1);
            strcat(str, "struct");
            str = realloc(str, strlen(str) + strlen("{") + 1);
            strcat(str, "{");
            int i; char * val;
            vec_foreach(&type->structType->attributeNames, val, i) {
                // we add the name of the attribute
                str = realloc(str, strlen(str) + strlen(val) + 1);
                strcat(str, val);
                // followed by :
                str = realloc(str, strlen(str) + strlen(":") + 1);
                strcat(str, ":");
                // followed by the type of the attribute
                // first get attribute type from the map
                StructAttribute ** attrType = map_get(&type->structType->attributes, val);
                // then stringify it
                char * attrTypeStr = ast_stringifyType((*attrType)->type);
                // then add it to the string
                str = realloc(str, strlen(str) + strlen(attrTypeStr) + 1);
                strcat(str, attrTypeStr);
                // followed by ,
                str = realloc(str, strlen(str) + strlen(",") + 1);
                strcat(str, ",");

            }
            // replace last , with }
            str[strlen(str) - 1] = '}';
            break;
        }
        case DT_VARIANT: {
            // we write it as variant{ConstructorName1(arg1: type1, arg2: type2), ConstructorName2(arg1: type1, arg2: type2) , etc)}
            str = realloc(str, strlen(str) + strlen("variant") + 1);
            strcat(str, "variant");
            str = realloc(str, strlen(str) + strlen("{") + 1);
            strcat(str, "{");
            int i; char * val;
            vec_foreach(&type->variantType->constructorNames, val, i) {
                // we add the name of the constructor
                str = realloc(str, strlen(str) + strlen(val) + 1);
                strcat(str, val);
                // followed by (
                str = realloc(str, strlen(str) + strlen("(") + 1);
                strcat(str, "(");
                // followed by the arguments
                int j; char * argName;
                // get current constructor
                VariantConstructor ** constructor = map_get(&type->variantType->constructors, val);
                // iterate through the arguments
                vec_foreach(&(*constructor)->argNames, argName, j) {
                    // add the name of the argument
                    str = realloc(str, strlen(str) + strlen(argName) + 1);
                    strcat(str, argName);
                    // followed by :
                    str = realloc(str, strlen(str) + strlen(":") + 1);
                    strcat(str, ":");
                    // followed by the type of the argument
                    // first get the argument type from the map
                    VariantConstructorArgument ** argType = map_get(&(*constructor)->args, argName);
                    // then stringify it
                    char * argTypeStr = ast_stringifyType((*argType)->type);
                    // then add it to the string
                    str = realloc(str, strlen(str) + strlen(argTypeStr) + 1);
                    strcat(str, argTypeStr);
                    // followed by ,
                    str = realloc(str, strlen(str) + strlen(",") + 1);
                    strcat(str, ",");
                }
                // replace last , with )
                str[strlen(str) - 1] = ')';
                // followed by ,
                str = realloc(str, strlen(str) + strlen(",") + 1);
                strcat(str, ",");
            }
            // replace last , with }
            str[strlen(str) - 1] = '}';
        }

        default:
            break;
    }

    // we check if it has generics
    if(type->isGeneric){
        // we add the generics to the string
        int i; char * val;
        // first we add a "<" to the string
        str = realloc(str, strlen(str) + 1);
        strcat(str, "<");
        // then we add the generics
        vec_foreach(&type->genericNames, val, i) {
                str = realloc(str, strlen(str) + strlen(val) + 1);
                strcat(str, val);
            }
        // we also iterator through concretegeneric types, serialize them and sparate them with a comma
        vec_foreach(&type->concreteGenerics, val, i) {
            char * genericType = ast_stringifyType(val);
            str = realloc(str, strlen(str) + strlen(genericType) + 1);
            strcat(str, genericType);
            str = realloc(str, strlen(str) + strlen(",") + 1);
            strcat(str, ",");
        }
        // replace last , with >
        str[strlen(str) - 1] = '>';

    }
    return str;
}

/**
* Building AST Data types
*/

ArrayType* ast_type_makeArray() {
    ALLOC(array, ArrayType);
    array->len = 0;
    return array;
}

EnumType* ast_type_makeEnum(){
    ALLOC(enu, EnumType);
    map_init(&enu->enums);
    vec_init(&enu->enumNames);

    return enu;
}

PtrType * ast_type_makePtr() {
    ALLOC(ptr, PtrType);
    ptr->target = NULL;

    return ptr;
}

ReferenceType* ast_type_makeReference() {
    ALLOC(ref, ReferenceType);
    ref->ref = NULL;

    return ref;
}

JoinType* ast_type_makeJoin() {
    ALLOC(join, JoinType);
    vec_init(&join->joins);

    return join;
}

UnionType* ast_type_makeUnion() {
    ALLOC(uni, UnionType);
    vec_init(&uni->unions);

    return uni;
}

VariantType* ast_type_makeVariant() {
    ALLOC(data, VariantType);
    map_init(&data->constructors);
    vec_init(&data->constructorNames);

    return data;
}

InterfaceType* ast_type_makeInterface() {
    ALLOC(interface, InterfaceType);
    map_init(&interface->methods);
    vec_init(&interface->methodNames);

    return interface;
}

ClassType* ast_type_makeClass() {
    ALLOC(class_, ClassType);
    map_init(&class_->methods);
    map_init(&class_->attributes);
    vec_init(&class_->attributeNames);
    vec_init(&class_->methodNames);

    return class_;
}

FnType* ast_type_makeFn() {
    ALLOC(fn, FnType);
    map_init(&fn->args);
    vec_init(&fn->argsNames);
    fn->returnType = NULL;

    return fn;
}

StructType* ast_type_makeStruct() {
    ALLOC(struct_, StructType);
    map_init(&struct_->attributes);
    vec_init(&struct_->attributeNames);

    return struct_;
}

DataType* ast_type_makeType() {
    ALLOC(type, DataType);
    type->name = NULL;
    type->isGeneric = 0;
    type->kind = DT_UNRESOLVED;
    type ->classType = NULL;

    vec_init(&type->concreteGenerics);
    vec_init(&type->genericNames);
    map_init(&type->generics);

    return type;
}

StructAttribute * ast_type_makeStructAttribute(){
    ALLOC(attribute, StructAttribute);
    attribute->type = NULL;
    attribute->name = NULL;

    return attribute;
}

ClassAttribute * ast_type_makeClassAttribute(){
    ALLOC(attribute, ClassAttribute);
    attribute->type = NULL;
    attribute->name = NULL;

    return attribute;
}

VariantConstructorArgument * ast_type_makeVariantConstructorArgument() {
    ALLOC(argument, VariantConstructorArgument);
    argument->type = NULL;
    argument->name = NULL;

    return argument;
}

VariantConstructor* ast_type_makeVariantConstructor(){
    ALLOC(constructor, VariantConstructor);
    map_init(&constructor->args);
    vec_init(&constructor->argNames);

    return constructor;
}


#undef ALLOC