#ifndef PAWSCRIPT_H
#define PAWSCRIPT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

typedef struct PawScriptContext PawScriptContext;

typedef struct {
    int row, col;
    char* msg;
    char* file;
} PawScriptError;

typedef enum {
    VISIBILITY_BLACKLIST,
    VISIBILITY_WHITELIST,
} PawScriptVisibility;

typedef enum {
    TYPE_VOID,
    TYPE_8BIT,
    TYPE_16BIT,
    TYPE_32BIT,
    TYPE_64BIT,
    TYPE_32FLT,
    TYPE_64FLT,
    TYPE_FUNCTION,
    TYPE_POINTER,
    TYPE_STRUCT,
} PawScriptTypeKind;

typedef struct PawScriptType {
    bool is_const;
    bool is_unsigned;
    bool is_native;
    bool is_incomplete;
    PawScriptTypeKind kind;
    char* name;
    struct PawScriptType* orig;
    union {
        struct {
            struct PawScriptType* base;
        } pointer_info;
        struct {
            size_t num_fields;
            size_t* field_offsets;
            struct PawScriptType** fields;
        } struct_info;
        struct {
            struct PawScriptType* return_value;
            struct PawScriptType** arg_types;
            size_t num_args;
        } function_info;
    };
} PawScriptType;

typedef struct {
    const char* type_str;
    uint64_t data;
} PawScriptVarargs;

#define ETC size_t __pawscript_num_varargs, PawScriptVarargs* __pawscript_varargs
#define VARARGS(...) sizeof((PawScriptVarargs[]){ __VA_ARGS__ }) / sizeof(PawScriptVarargs), (PawScriptVarargs[]){ __VA_ARGS__ }
#define ITEM(type, value) ({ \
    typeof((value) + 0) val = value; \
    uint64_t data = 0; \
    memcpy(&data, &val, sizeof(typeof(val))); \
    (PawScriptVarargs){ .type_str = type, .data = data }; \
})

PawScriptContext* pawscript_create_context();
void pawscript_destroy_context(PawScriptContext* context);
void pawscript_symbol_visibility(PawScriptContext* context, PawScriptVisibility visibility);
void pawscript_register_symbol(PawScriptContext* context, void* symbol_addr);

bool pawscript_run_file(PawScriptContext* context, const char* file);
bool pawscript_run(PawScriptContext* context, const char* code);
bool pawscript_any_errors(PawScriptContext* context);
bool pawscript_error(PawScriptContext* context, PawScriptError** error);

bool pawscript_get(PawScriptContext* context, const char* name, void* out);
bool pawscript_set(PawScriptContext* context, const char* name, void* in);
PawScriptType* pawscript_get_type(PawScriptContext* context, const char* name);
void pawscript_destroy_type(PawScriptType* type);

#endif
