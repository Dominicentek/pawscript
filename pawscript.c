#include <math.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#endif

#ifdef _WIN32
#define NUM_INT_REGS 4
#define NUM_FLT_REGS 4
#else
#define NUM_INT_REGS 6
#define NUM_FLT_REGS 8
#endif

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
    TYPE_VARARGS,
} TypeKind;

typedef struct Type {
    bool is_const;
    bool is_unsigned;
    bool is_native;
    bool is_incomplete;
    TypeKind kind;
    char* name;
    struct Type* orig;
    union {
        struct {
            struct Type* base;
        } pointer_info;
        struct {
            size_t num_fields;
            size_t* field_offsets;
            struct Type** fields;
        } struct_info;
        struct {
            struct Type* return_value;
            struct Type** arg_types;
            size_t num_args;
        } function_info;
    };
} Type;

#define PARENS ()
#define EXPAND(...) EXPAND4(EXPAND4(EXPAND4(EXPAND4(__VA_ARGS__))))
#define EXPAND4(...) EXPAND3(EXPAND3(EXPAND3(EXPAND3(__VA_ARGS__))))
#define EXPAND3(...) EXPAND2(EXPAND2(EXPAND2(EXPAND2(__VA_ARGS__))))
#define EXPAND2(...) EXPAND1(EXPAND1(EXPAND1(EXPAND1(__VA_ARGS__))))
#define EXPAND1(...) __VA_ARGS__

#define FOR_EACH(macro, ...) __VA_OPT__(EXPAND(FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define FOR_EACH_HELPER(macro, a1, ...) macro(a1) __VA_OPT__(FOR_EACH_AGAIN PARENS (macro, __VA_ARGS__))
#define FOR_EACH_AGAIN() FOR_EACH_HELPER

#define TOKENS(...) \
typedef enum { \
    FOR_EACH(ENUM, __VA_ARGS__) \
} TokenKind; \
const char* token_table[] = { \
    FOR_EACH(DECL, __VA_ARGS__) \
}; \
int num_token_table_entries = sizeof(token_table) / sizeof(*token_table);

#define ENUM_KEYWORD(x) TOKEN_##x,
#define ENUM_SPECIAL(x) TOKEN_##x,
#define ENUM_SYMBOL(x, y) TOKEN_##y,
#define DECL_KEYWORD(x) #x,
#define DECL_SPECIAL(x) NULL,
#define DECL_SYMBOL(x, y) x,
#define ENUM(x) ENUM_##x
#define DECL(x) DECL_##x

#undef true
#undef false
#undef bool

TOKENS(
    KEYWORD(bitcast),
    KEYWORD(break),
    KEYWORD(cast),
    KEYWORD(continue),
    KEYWORD(elif),
    KEYWORD(else),
    KEYWORD(extern),
    KEYWORD(for),
    KEYWORD(if),
    KEYWORD(in),
    KEYWORD(include),
    KEYWORD(return),
    KEYWORD(typedef),
    KEYWORD(while),
    KEYWORD(adopt),
    KEYWORD(delete),
    KEYWORD(global),
    KEYWORD(new),
    KEYWORD(promote),
    KEYWORD(scoped),
    KEYWORD(true),
    KEYWORD(false),
    KEYWORD(null),
    KEYWORD(this),
    KEYWORD(infoof),
    KEYWORD(offsetof),
    KEYWORD(scopeof),
    KEYWORD(sizeof),
    KEYWORD(bool),
    KEYWORD(u8),
    KEYWORD(u16),
    KEYWORD(u32),
    KEYWORD(u64),
    KEYWORD(s8),
    KEYWORD(s16),
    KEYWORD(s32),
    KEYWORD(s64),
    KEYWORD(f32),
    KEYWORD(f64),
    KEYWORD(void),
    KEYWORD(const),
    KEYWORD(struct),
    SPECIAL(IDENTIFIER),
    SPECIAL(STRING),
    SPECIAL(INTEGER),
    SPECIAL(FLOAT),
    SYMBOL("(", PARENTHESIS_OPEN),
    SYMBOL(")", PARENTHESIS_CLOSE),
    SYMBOL("[", BRACKET_OPEN),
    SYMBOL("]", BRACKET_CLOSE),
    SYMBOL("{", BRACE_OPEN),
    SYMBOL("}", BRACE_CLOSE),
    SYMBOL("->", ARROW),
    SYMBOL(",", COMMA),
    SYMBOL(":", COLON),
    SYMBOL(";", SEMICOLON),
    SYMBOL(".", DOT),
    SYMBOL("+", PLUS),
    SYMBOL("-", MINUS),
    SYMBOL("/", SLASH),
    SYMBOL("%", PERCENT),
    SYMBOL("*", ASTERISK),
    SYMBOL("^", HAT),
    SYMBOL("&", AMPERSAND),
    SYMBOL("|", PIPE),
    SYMBOL("++", DOUBLE_PLUS),
    SYMBOL("--", DOUBLE_MINUS),
    SYMBOL("**", DOUBLE_ASTERISK),
    SYMBOL("&&", DOUBLE_AMPERSAND),
    SYMBOL("||", DOUBLE_PIPE),
    SYMBOL("==", DOUBLE_EQUALS),
    SYMBOL("!=", NOT_EQUALS),
    SYMBOL("<", LESS_THAN),
    SYMBOL(">", GREATER_THAN),
    SYMBOL("<=", LESS_THAN_EQUALS),
    SYMBOL(">=", GREATER_THAN_EQUALS),
    SYMBOL("<<", DOUBLE_LESS_THAN),
    SYMBOL(">>", DOUBLE_GREATER_THAN),
    SYMBOL("=", EQUALS),
    SYMBOL("+=", PLUS_EQUALS),
    SYMBOL("-=", MINUS_EQUALS),
    SYMBOL("*=", ASTERISK_EQUALS),
    SYMBOL("/=", SLASH_EQUALS),
    SYMBOL("%=", PERCENT_EQUALS),
    SYMBOL("**=", DOUBLE_ASTERISK_EQUALS),
    SYMBOL("&=", AMPERSAND_EQUALS),
    SYMBOL("|=", PIPE_EQUALS),
    SYMBOL("^=", HAT_EQUALS),
    SYMBOL("<<=", DOUBLE_LESS_THAN_EQUALS),
    SYMBOL(">>=", DOUBLE_GREATER_THAN_EQUALS),
    SYMBOL("~", TILDE),
    SYMBOL("!", EXCLAMATION_MARK),
    SYMBOL("...", TRIPLE_DOT),
    SYMBOL("@", AT),
    SYMBOL("?", QUESTION_MARK),
    SYMBOL("??", DOUBLE_QUESTION_MARK),
    SYMBOL("//", COMMENT),
    SYMBOL("/*", COMMENT_START),
    SYMBOL("*/", COMMENT_END),
    SPECIAL(END_OF_FILE)
)

#undef PARENS
#undef EXPAND
#undef EXPAND4
#undef EXPAND3
#undef EXPAND2
#undef EXPAND1
#undef FOR_EACH
#undef FOR_EACH_HELPER
#undef FOR_EACH_AGAIN
#undef TOKENS
#undef ENUM
#undef DECL
#undef ENUM_KEYWORD
#undef ENUM_SPECIAL
#undef ENUM_SYMBOL
#undef DECL_KEYWORD
#undef DECL_SPECIAL
#undef DECL_SYMBOL

#define bool _Bool
#define true 1
#define false 0

typedef struct {
    int row, col;
    TokenKind type;
    char* filename;
    union {
        char* string;
        uint64_t integer;
        double floating;
    } value;
} Token;

typedef enum {
    STATE_RUNNING,
    STATE_RETURN,
    STATE_BREAK,
    STATE_CONTINUE,
} PawScriptState;

typedef struct {
    Type* type;
    void* address;
    bool no_alloc;
} Variable;

typedef struct {
    int row, col;
    char* msg;
    char* file;
} PawScriptError;

typedef struct {
    void* ptr;
    size_t size;
    bool strict;
} PawScriptAllocation;

typedef struct {
    Variable** args;
    size_t count;
} PawScriptVarargs;

typedef struct {
    const char* type_str;
    uint64_t data;
} PawScriptVarargItem;

typedef enum {
    VISIBILITY_BLACKLIST,
    VISIBILITY_WHITELIST,
} PawScriptVisibility;

typedef enum {
    SCOPE_REGULAR,
    SCOPE_BREAKABLE,
    SCOPE_FUNCTION,
} PawScriptScopeType;

typedef struct PawScriptScope {
    struct PawScriptScope* parent;
    PawScriptScopeType type;
    size_t num_variables;
    size_t num_allocs;
    size_t num_typedefs;
    Variable** variables;
    PawScriptAllocation** allocs;
    Type** typedefs;
} PawScriptScope;

typedef struct {
    PawScriptScope* scope;
    PawScriptVisibility address_visibility;
    PawScriptState state;
    uint64_t state_var;
    size_t num_tokens;
    size_t num_addresses;
    size_t num_strings;
    size_t num_errors;
    size_t error_ptr;
    PawScriptError** errors;
    Token** tokens;
    void** addresses;
    char** strings;
    bool dry_run;
} PawScriptContext;

typedef struct {
    uint64_t int_regs[NUM_INT_REGS];
    double   flt_regs[NUM_FLT_REGS];
    uint64_t* stack;
    int int_ptr;
    int flt_ptr;
    int stack_ptr;
    int num_fixed_float_args;
    bool varargs;

    uint64_t int_return;
    double   flt_return;
} FFI;

typedef union {
    Token* operator;
    struct {
        Token** tokens;
        size_t num_tokens;
    } operand;
} ExprItem;

typedef struct ExprNode {
    bool is_leaf;
    union {
        struct {
            Token* operator;
            struct ExprNode* a;
            struct ExprNode* b;
        } branch;
        struct {
            Token** tokens;
            size_t num_tokens;
        } leaf;
    };
} ExprNode;

typedef enum {
    LeftToRight,
    RightToLeft
} Associativity;

typedef enum {
    Null,
    Any,
    Number,
    Integer,
    Pointer,
    Assignable,
} ValueType;

typedef struct {
    PawScriptContext* context;
    size_t num_parameters;
    Variable** parameters;
    Variable* this;
} FuncCall;

typedef struct {
    char jmp[5];

    Token** start;
    size_t num_tokens;
} Function;

static char* dynstr_create() {
    int* dynstr = calloc(1024 + sizeof(int) * 2, 1);
    *(dynstr + 0) = 1024;
    *(dynstr + 1) = 0;
    return (char*)(dynstr + 2);
}

static void dynstr_appendc(char** dynstr, char c) {
    int cap = *((int*)*dynstr - 2);
    int ptr = *((int*)*dynstr - 1);
    (*dynstr)[ptr++] = c;
    if (cap == ptr) {
        cap += 1024;
        *dynstr = (char*)realloc(*dynstr - sizeof(int) * 2, cap) + sizeof(int) * 2;
        *((int*)*dynstr - 2) = cap;
    }
    (*dynstr)[ptr] = 0;
    *((int*)*dynstr - 1) = ptr;
}

static void dynstr_append(char** dynstr, char* str) {
    char c;
    while ((c = *(str++)) != 0) {
        dynstr_appendc(dynstr, c);
    }
}

static void dynstr_free(char* dynstr) {
    free((int*)dynstr - 2);
}

static char* dynarr_create() {
    int* dynarr = calloc(1024 + sizeof(int) * 2, 1);
    *(dynarr + 0) = 1024;
    *(dynarr + 1) = 0;
    return (char*)(dynarr + 2);
}

static void dynarr_append_value(char** dynarr, char value) {
    int cap = *((int*)*dynarr - 2);
    int ptr = *((int*)*dynarr - 1);
    if (cap == ptr) {
        cap += 1024;
        *dynarr = (char*)realloc(*dynarr - sizeof(int) * 2, cap) + sizeof(int) * 2;
        *((int*)*dynarr - 2) = cap;
    }
    (*dynarr)[ptr++] = value;
    *((int*)*dynarr - 1) = ptr;
}

static void dynarr_append(char** dynarr, void* value, size_t size) {
    char* ptr = value;
    for (size_t i = 0; i < size; i++) {
        dynarr_append_value(dynarr, ptr[i]);
    }
}

static void dynarr_free(char* dynarr) {
    free((int*)dynarr - 2);
}

static int dynarr_length(char* dynarr) {
    return *((int*)dynarr - 1);
}

static char* dynarr_regular(char* dynarr) {
    int ptr = *((int*)dynarr - 1);
    char* array = malloc(ptr);
    memcpy(array, dynarr, ptr);
    dynarr_free(dynarr);
    return array;
}

static FFI* ffi_create() {
    return calloc(sizeof(FFI), 1);
}

static void ffi_push_int(FFI* ffi, uint64_t value) {
    if (ffi->int_ptr < NUM_INT_REGS) ffi->int_regs[ffi->int_ptr++] = value;
    else {
        ffi->stack_ptr++;
        ffi->stack = realloc(ffi->stack, sizeof(uint64_t) * ffi->stack_ptr);
        ffi->stack[ffi->stack_ptr - 1] = value;
    }
}

static void ffi_push_double(FFI* ffi, double value) {
#ifdef _WIN32
    if (ffi->varargs) {
        ffi_push_int(ffi, *(uint64_t*)&value);
        return;
    }
#endif
    if (ffi->flt_ptr < NUM_FLT_REGS) ffi->flt_regs[ffi->flt_ptr++] = value;
    else {
        ffi->stack_ptr++;
        ffi->stack = realloc(ffi->stack, sizeof(uint64_t) * ffi->stack_ptr);
        ffi->stack[ffi->stack_ptr - 1] = *(uint64_t*)&value;
    }
}

static void ffi_push_float(FFI* ffi, float value) {
    if (ffi->varargs) {
        ffi_push_double(ffi, value);
        return;
    }
    double dbl;
    memset(&dbl, 0, sizeof(double));
    memcpy(&dbl, &value, sizeof(float));
    ffi_push_double(ffi, dbl);
}

static void ffi_varargs(FFI* ffi) {
    ffi->varargs = true;
}

static void ffi_call(FFI* ffi, void* func) {
    if (ffi->stack_ptr % 2 == 1) ffi_push_int(ffi, 0); // 16-byte alignment

    register void* rsp asm("rsp");
    register uint64_t rax  asm("rax");
    register double   xmm0 asm("xmm0");

    rsp -= ffi->stack_ptr * 8;
    memcpy(rsp, ffi->stack, ffi->stack_ptr * 8);

#ifdef _WIN32
    ((void(*)(
        uint64_t, uint64_t, uint64_t, uint64_t,
        double, double, double, double
    ))func)(
        ffi->int_regs[0], ffi->int_regs[1], ffi->int_regs[2], ffi->int_regs[3],
        ffi->flt_regs[0], ffi->flt_regs[1], ffi->flt_regs[2], ffi->flt_regs[3]
    );
#else
    rax = ffi->flt_ptr;
    ((void(*)(
        uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
        double, double, double, double, double, double, double, double
    ))func)(
        ffi->int_regs[0], ffi->int_regs[1], ffi->int_regs[2], ffi->int_regs[3], ffi->int_regs[4], ffi->int_regs[5],
        ffi->flt_regs[0], ffi->flt_regs[1], ffi->flt_regs[2], ffi->flt_regs[3], ffi->flt_regs[4], ffi->flt_regs[5], ffi->flt_regs[6], ffi->flt_regs[7]
    );
#endif

    rsp += ffi->stack_ptr * 8;

    ffi->int_return = rax;
    ffi->flt_return = xmm0;
}

static uint64_t ffi_get_int(FFI* ffi) {
    return ffi->int_return;
}

static double ffi_get_double(FFI* ffi) {
    return ffi->flt_return;
}

static float ffi_get_float(FFI* ffi) {
    return *(float*)&ffi->flt_return;
}

static void ffi_destroy(FFI* ffi) {
    free(ffi->stack);
    free(ffi);
}

static void pawscript_push_error(PawScriptContext* context, char* file, int row, int col, const char* msg, ...) {
    va_list args1, args2;
    va_start(args1, msg);
    va_copy(args2, args1);
    size_t bufsiz = vsnprintf(NULL, 0, msg, args1);
    char formatted[bufsiz + 1];
    vsprintf(formatted, msg, args2);
    va_end(args1);
    va_end(args2);
    PawScriptError* error = malloc(sizeof(PawScriptError));
    error->row = row;
    error->col = col;
    error->file = file;
    error->msg = strdup(formatted);
    context->num_errors++;
    context->errors = realloc(context->errors, sizeof(void*) * context->num_errors);
    context->errors[context->num_errors - 1] = error;
}

bool pawscript_any_errors(PawScriptContext* context) {
    return context->error_ptr < context->num_errors;
}

bool pawscript_error(PawScriptContext* context, PawScriptError** error) {
    if (context->error_ptr >= context->num_errors) return false;
    *error = context->errors[context->error_ptr++];
    return true;
}

static size_t pawscript_sizeof(Type* type) {
    size_t size = 0;
    switch (type->kind) {
        case TYPE_VOID:       return 0;
        case TYPE_8BIT:       return 1;
        case TYPE_16BIT:      return 2;
        case TYPE_32BIT:      return 4;
        case TYPE_64BIT:      return 8;
        case TYPE_32FLT:      return 4;
        case TYPE_64FLT:      return 8;
        case TYPE_POINTER:    return 8;
        case TYPE_FUNCTION:   return 8;
        case TYPE_VARARGS:    return 0;
        case TYPE_STRUCT: {
            if (type->struct_info.num_fields == 0) return 1;
            size_t max = 0;
            size_t max_size = 0;
            for (size_t i = 0; i < type->struct_info.num_fields; i++) {
                size_t size = pawscript_sizeof(type->struct_info.fields[i]);
                size_t bound = type->struct_info.field_offsets[i] + size;
                if (max < bound) max = bound;
                if (max_size < size) max_size = size;
            }
            if (max % max_size == 0) return max;
            return max + (max_size - max % max_size);
        } break;
    }
    return size == 0 ? 1 : size;
}

static Type* pawscript_copy_type_inner(Type* orig, size_t* num_visited, Type*** visited) {
    if (!orig) return NULL;
    for (size_t i = 0; i < *num_visited; i++) {
        if ((*visited)[i * 2] == orig) return (*visited)[i * 2 + 1];
    }
    Type* copy = calloc(sizeof(Type), 1);
    copy->is_const = orig->is_const;
    copy->is_unsigned = orig->is_unsigned;
    copy->is_native = orig->is_native;
    copy->is_incomplete = orig->is_incomplete;
    copy->kind = orig->kind;
    copy->name = orig->name ? strdup(orig->name) : NULL;
    copy->orig = orig->orig;
    (*num_visited)++;
    *visited = realloc(*visited, sizeof(Type*) * *num_visited * 2);
    (*visited)[(*num_visited - 1) * 2 + 0] = orig;
    (*visited)[(*num_visited - 1) * 2 + 1] = copy;
    switch (copy->kind) {
        case TYPE_FUNCTION:
            copy->function_info.num_args = orig->function_info.num_args;
            copy->function_info.return_value = pawscript_copy_type_inner(orig->function_info.return_value, num_visited, visited);
            copy->function_info.arg_types = calloc(sizeof(void*) * orig->function_info.num_args, 1);
            for (size_t i = 0; i < copy->function_info.num_args; i++) {
                copy->function_info.arg_types[i] = pawscript_copy_type_inner(orig->function_info.arg_types[i], num_visited, visited);
            }
            break;
        case TYPE_POINTER:
            copy->pointer_info.base = pawscript_copy_type_inner(orig->pointer_info.base, num_visited, visited);
            break;
        case TYPE_STRUCT:
            copy->struct_info.num_fields = orig->struct_info.num_fields;
            copy->struct_info.fields = calloc(sizeof(void*) * orig->struct_info.num_fields, 1);
            copy->struct_info.field_offsets = calloc(sizeof(size_t) * orig->struct_info.num_fields, 1);
            for (size_t i = 0; i < copy->struct_info.num_fields; i++) {
                copy->struct_info.fields[i] = pawscript_copy_type_inner(orig->struct_info.fields[i], num_visited, visited);
                copy->struct_info.field_offsets[i] = orig->struct_info.field_offsets[i];
            }
            break;
        default: break;
    }
    return copy;
}

static Type* pawscript_copy_type(Type* orig) {
    size_t num_visited = 0;
    Type** visited = NULL;
    Type* copied = pawscript_copy_type_inner(orig, &num_visited, &visited);
    free(visited);
    return copied;
}

static Type* pawscript_create_type(TypeKind kind) {
    Type* type = calloc(sizeof(Type), 1);
    type->kind = kind;
    type->orig = type;
    return type;
}

void pawscript_destroy_type_inner(Type* type, size_t* num_visited, Type*** visited) {
    for (size_t i = 0; i < *num_visited; i++) {
        if ((*visited)[i] == type) return;
    }
    (*num_visited)++;
    *visited = realloc(*visited, sizeof(Type*) * *num_visited);
    (*visited)[*num_visited - 1] = type;
    switch (type->kind) {
        case TYPE_POINTER:
            pawscript_destroy_type_inner(type->pointer_info.base, num_visited, visited);
            break;
        case TYPE_STRUCT:
            for (size_t i = 0; i < type->struct_info.num_fields; i++) {
                pawscript_destroy_type_inner(type->struct_info.fields[i], num_visited, visited);
            }
            free(type->struct_info.field_offsets);
            free(type->struct_info.fields);
            break;
        case TYPE_FUNCTION:
            pawscript_destroy_type_inner(type->function_info.return_value, num_visited, visited);
            for (size_t i = 0; i < type->function_info.num_args; i++) {
                pawscript_destroy_type_inner(type->function_info.arg_types[i], num_visited, visited);
            }
            free(type->function_info.arg_types);
            break;
        default: break;
    }
    free(type->name);
    free(type);
}

void pawscript_destroy_type(Type* type) {
    size_t num_visited = 0;
    Type** visited = NULL;
    pawscript_destroy_type_inner(type, &num_visited, &visited);
    free(visited);
}

static void pawscript_make_native_inner(Type* type, bool is_native, size_t* num_visited, Type*** visited) {
    for (size_t i = 0; i < *num_visited; i++) {
        if ((*visited)[i] == type) return;
    }
    (*num_visited)++;
    *visited = realloc(*visited, sizeof(Type*) * *num_visited);
    (*visited)[*num_visited - 1] = type;
    type->is_native = is_native;
    switch (type->kind) {
        case TYPE_POINTER:
            pawscript_make_native_inner(type->pointer_info.base, is_native, num_visited, visited);
            break;
        case TYPE_FUNCTION:
            pawscript_make_native_inner(type->function_info.return_value, is_native, num_visited, visited);
            for (size_t i = 0; i < type->function_info.num_args; i++) {
                pawscript_make_native_inner(type->function_info.arg_types[i], is_native, num_visited, visited);
            }
            break;
        case TYPE_STRUCT:
            for (size_t i = 0; i < type->struct_info.num_fields; i++) {
                pawscript_make_native_inner(type->struct_info.fields[i], is_native, num_visited, visited);
            }
            break;
        default: break;
    }
}

static void pawscript_make_native(Type* type, bool is_native) {
    size_t num_visited = 0;
    Type** visited = NULL;
    pawscript_make_native_inner(type, is_native, &num_visited, &visited);
    free(visited);
}

static void pawscript_make_original_inner(Type* type, size_t* num_visited, Type*** visited) {
    for (size_t i = 0; i < *num_visited; i++) {
        if ((*visited)[i] == type) return;
    }
    (*num_visited)++;
    *visited = realloc(*visited, sizeof(Type*) * *num_visited);
    (*visited)[*num_visited - 1] = type;
    type->orig = type;
    switch (type->kind) {
        case TYPE_POINTER:
            pawscript_make_original_inner(type->pointer_info.base, num_visited, visited);
            break;
        case TYPE_FUNCTION:
            pawscript_make_original_inner(type->function_info.return_value, num_visited, visited);
            for (size_t i = 0; i < type->function_info.num_args; i++) {
                pawscript_make_original_inner(type->function_info.arg_types[i], num_visited, visited);
            }
            break;
        case TYPE_STRUCT:
            for (size_t i = 0; i < type->struct_info.num_fields; i++) {
                pawscript_make_original_inner(type->struct_info.fields[i], num_visited, visited);
            }
            break;
        default: break;
    }
}

static Type* pawscript_make_original(Type* type) {
    size_t num_visited = 0;
    Type** visited = NULL;
    pawscript_make_original_inner(type, &num_visited, &visited);
    free(visited);
    return type;
}

static void pawscript_replace_incompletes(Type* type, Type* with) {
#define REPLACE(type) if (type->is_incomplete) { \
        pawscript_destroy_type(type); \
        type = with; \
    } \
    else pawscript_replace_incompletes(type, with);

    switch (type->kind) {
        case TYPE_POINTER:
            REPLACE(type->pointer_info.base);
            break;
        case TYPE_FUNCTION:
            REPLACE(type->function_info.return_value);
            for (size_t i = 0; i < type->function_info.num_args; i++) {
                REPLACE(type->function_info.arg_types[i]);
            }
            break;
        case TYPE_STRUCT:
            for (size_t i = 0; i < type->struct_info.num_fields; i++) {
                REPLACE(type->struct_info.fields[i]);
            }
            break;
        default: break;
    }
}

static char* pawscript_get_filename(char* base, char* filename) {
    if (!base) base = "./";
    base = strdup(base);
    for (int i = strlen(base) - 1; i >= 0; i--) {
        if (base[i] == '/') break;
        base[i] = 0;
    }
    char* out = malloc(strlen(base) + strlen(filename) + 1);
    strcpy(out, base);
    strcat(out, filename);
    free(base);
    if (access(out, F_OK) == 0) return out;
    free(out);
    return strdup(filename);
}

static bool pawscript_is_alphanumeric(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

static bool pawscript_is_numeric(char c) {
    return c >= '0' && c <= '9';
}

static bool pawscript_is_symbol(char c) {
    return c > ' ' && c <= '~' && !pawscript_is_alphanumeric(c);
}

static bool pawscript_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

static bool pawscript_get_octal(char c, int* out) {
    if (c >= '0' && c <= '7') *out = c - '0';
    else return false;
    return true;
}

static bool pawscript_get_decimal(char c, int* out) {
    if (c >= '0' && c <= '9') *out = c - '0';
    else return false;
    return true;
}

static bool pawscript_get_hexadecimal(char c, int* out) {
    if      (c >= '0' && c <= '9') *out = c - '0';
    else if (c >= 'a' && c <= 'f') *out = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') *out = c - 'A' + 10;
    else return false;
    return true;
}

static void pawscript_encode_utf8(char** buffer, uint32_t value) {
    if (value <= 0x7F) {
        dynstr_appendc(buffer, value);
    }
    else if (value <= 0x7FF) {
        dynstr_appendc(buffer, 0xC0 | (value >> 6));
        dynstr_appendc(buffer, 0x80 | (value & 0x3F));
    }
    else if (value <= 0xFFFF) {
        dynstr_appendc(buffer, 0xE0 | (value >> 12));
        dynstr_appendc(buffer, 0x80 | ((value >> 6) & 0x3F));
        dynstr_appendc(buffer, 0x80 | (value & 0x3F));
    }
    else if (value <= 0x10FFFF) {
        dynstr_appendc(buffer, 0xF0 | (value >> 18));
        dynstr_appendc(buffer, 0x80 | ((value >> 12) & 0x3F));
        dynstr_appendc(buffer, 0x80 | ((value >> 6) & 0x3F));
        dynstr_appendc(buffer, 0x80 | (value & 0x3F));
    }
}

static bool pawscript_try_parse_int(char* string, uint64_t* out) {
    int base = 10;
    int ptr = 0;
    if (string[0] == '0') {
        ptr = 2;
        switch (string[1]) {
            case 0:             *out = 0;  return true;
            case 'x': case 'X': base = 16; break;
            case 'b': case 'B': base = 2;  break;
            case '0'...'7':     base = 8; ptr = 1; break;
            default: return false;
        }
    }
    if (string[ptr] == 0) return false;
    bool suffix_end = false;
    *out = 0;
    for (; string[ptr]; ptr++) {
        int digit;
        if (suffix_end) return false;
        if (!pawscript_get_hexadecimal(string[ptr], &digit)) return false;
        if (digit >= base) return false;
        *out = *out * base + digit;
    }
    return true;
}

static bool pawscript_try_parse_flt(char* string, double* out) {
    int base = 10;
    int ptr = 0;
    int exp = 0;
    int frac = 0;
    bool neg_exp = false;
    bool hex = false;
    enum {
        Integer,
        Fraction,
        Exponent,
        ExponentFirstChar,
        ExponentFirstCharNoSign,
    } state = Integer;
    if (string[0] == '0' && (string[1] == 'x' || string[1] == 'X')) {
        ptr = 2;
        base = 16;
        hex = true;
    }
    if (string[ptr] == 0) return false;
    *out = 0;
    for (; string[ptr]; ptr++) {
        int digit = 0;
        bool valid = pawscript_get_hexadecimal(string[ptr], &digit) && digit < base;
        switch (state) {
            case Integer:
                if (string[ptr] == '.') state = Fraction;
                else if (((string[ptr] == 'e' || string[ptr] == 'E') && !hex) || ((string[ptr] == 'p' || string[ptr] == 'P') && hex)) {
                    state = ExponentFirstChar;
                    base = 10;
                }
                else if (valid) *out = *out * base + digit;
                else return false;
                break;
            case Fraction:
                if (((string[ptr] == 'e' || string[ptr] == 'E') && !hex) || ((string[ptr] == 'p' || string[ptr] == 'P') && hex)) {
                    state = ExponentFirstChar;
                    base = 10;
                }
                else if (valid) *out += digit * pow(base, -(++frac));
                else return false;
                break;
            case Exponent:
            case ExponentFirstChar:
            case ExponentFirstCharNoSign:
                if ((string[ptr] == '-' || string[ptr] == '+') && state == ExponentFirstChar) {
                    neg_exp = string[ptr] == '-';
                    state = ExponentFirstCharNoSign;
                    continue;
                }
                if (!pawscript_get_decimal(string[ptr], &digit)) return false;
                if (valid) exp = exp * base + digit;
                else return false;
                state = Exponent;
        }
    }
    *out *= pow(hex ? 2 : 10, neg_exp ? -exp : exp);
    return (!hex && (state == Integer || state == Fraction || state == Exponent)) || (hex && state == Exponent);
}

static void pawscript_destroy_tokens(Token** tokens, size_t num_tokens) {
    if (num_tokens == 0) return;
    for (size_t i = 0; i < num_tokens; i++) {
        if (tokens[i]->type == TOKEN_IDENTIFIER || tokens[i]->type == TOKEN_STRING) free(tokens[i]->value.string);
        free(tokens[i]);
    }
    free(tokens);
}

static Token* pawscript_append_token(Token*** tokens, size_t* num_tokens, char* filename, int row, int col) {
    Token* token = malloc(sizeof(Token));
    token->row = row;
    token->col = col;
    token->filename = filename;
    *tokens = realloc(*tokens, sizeof(Token*) * ++(*num_tokens));
    (*tokens)[*num_tokens - 1] = token;
    return token;
}

static void pawscript_append_tokens(Token*** dst, size_t* dst_num, Token** src, size_t src_num) {
    *dst = realloc(*dst, sizeof(Token*) * (*dst_num + src_num));
    memcpy(*dst + *dst_num, src, sizeof(Token*) * src_num);
    *dst_num += src_num;
    free(src);
}

static Token** pawscript_lex_internal(PawScriptContext* context, const char* code, char* filename, size_t* num_tokens) {
    char c;
    size_t ptr = 0;
    Token** tokens = NULL;
    bool no_increment = false;
    bool zero = false;
    int digit = 0;
    int row = 1, col = 0;
    *num_tokens = 0;
    struct {
        enum {
            Idle,
            ParsingWord,
            ParsingSymbol,
            ParsingStringLiteral,
            ParsingCharLiteral,
        } parse_state;
        char* buffer;
        int row, col;
        union {
            struct {
                int num_digits;
                uint32_t value;
                enum {
                    None,
                    CharStart,
                    Backslash,
                    Octal,
                    Hex2,
                    Hex4,
                    Hex8,
                } state;
            } string_state;
            struct {
                bool first_char;
                bool dot_in_word;
                bool can_have_sign;
                bool can_be_sign;
            } word_state;
        };
    } state;
    memset(&state, 0, sizeof(state));
    while ((c = code[no_increment ? ptr - 1 : ptr++]) != 0) {
        if (!no_increment) {
            col++;
            if (c == '\n') {
                col = 0;
                row++;
            }
        }
        no_increment = false;
        if (pawscript_any_errors(context)) {
            if (state.buffer) dynstr_free(state.buffer);
            pawscript_destroy_tokens(tokens, *num_tokens);
            return NULL;
        }
        if (state.parse_state == Idle) {
            if      (c == '"' )                  state.parse_state = ParsingStringLiteral;
            else if (c == '\'')                  state.parse_state = ParsingCharLiteral;
            else if (pawscript_is_alphanumeric(c) || (c == '.' && pawscript_is_numeric(code[ptr]))) state.parse_state = ParsingWord;
            else if (pawscript_is_symbol      (c)) state.parse_state = ParsingSymbol;
            else if (pawscript_is_whitespace  (c)) continue;
            else pawscript_push_error(context, filename, row, col, "Invalid codepoint: \\x%02x", c);
            state.buffer = dynstr_create();
            state.row = row;
            state.col = col;
            state.string_state.state = state.parse_state == ParsingCharLiteral ? CharStart : None;
            if (state.parse_state == ParsingWord || state.parse_state == ParsingSymbol) no_increment = true;
            if (state.parse_state == ParsingWord) state.word_state.first_char = true;
        }
        else if (state.parse_state == ParsingStringLiteral || state.parse_state == ParsingCharLiteral) {
            switch (state.string_state.state) {
                case CharStart:
                    if (c == '\\') pawscript_push_error(context, filename, row - 1, col, "Empty char literal");
                    state.string_state.state = None;
                case None:
                    if (c == '\\') state.string_state.state = Backslash;
                    else if (c == '"' && state.parse_state == ParsingStringLiteral) {
                        Token* token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col);
                        token->type = TOKEN_STRING;
                        token->value.string = strdup(state.buffer);
                        dynstr_free(state.buffer);
                        state.buffer = NULL;
                        state.parse_state = Idle;
                    }
                    else if (c == '\'' && state.parse_state == ParsingCharLiteral) {
                        Token* token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col);
                        token->type = TOKEN_INTEGER;
                        token->value.integer = 0;
                        for (char* ptr = state.buffer; *ptr; ptr++) {
                            token->value.integer = token->value.integer * 0x100 + *ptr;
                        }
                        dynstr_free(state.buffer);
                        state.buffer = NULL;
                        state.parse_state = Idle;
                    }
                    else dynstr_appendc(&state.buffer, c);
                    break;
                case Backslash:
                    state.string_state.num_digits = 0;
                    state.string_state.value = 0;
                    switch (c) {
                        case 'a': c = 0x07; break;
                        case 'b': c = 0x08; break;
                        case 'e': c = 0x1B; break;
                        case 'f': c = 0x1C; break;
                        case 'n': c = 0x0A; break;
                        case 'r': c = 0x0D; break;
                        case 't': c = 0x09; break;
                        case 'v': c = 0x0B; break;
                        case 'x':       state.string_state.state = Hex2;  break;
                        case 'u':       state.string_state.state = Hex4;  break;
                        case 'U':       state.string_state.state = Hex8;  break;
                        case '0'...'9': state.string_state.state = Octal; no_increment = true; break;
                        case '\n': c = 0xFF; break;
                    }
                    if (state.string_state.state == Backslash) {
                        if (c != -1) dynstr_appendc(&state.buffer, c);
                        state.string_state.state = None;
                    }
                    break;
                case Octal: case Hex2: case Hex4: case Hex8:
                    if ((state.string_state.state == Octal ? pawscript_get_octal : pawscript_get_hexadecimal)(c, &digit))
                        state.string_state.value = state.string_state.value * (state.string_state.state == Octal ? 8 : 16) + digit;
                    else pawscript_push_error(context, filename, row, col, "Not a valid digit");
                    state.string_state.num_digits++;
                    if (state.string_state.num_digits == (
                        state.string_state.state == Octal ? 3 :
                        state.string_state.state == Hex2  ? 2 :
                        state.string_state.state == Hex4  ? 4 :
                        state.string_state.state == Hex8  ? 8 : 1
                    )) {
                        if (state.string_state.state == Octal || state.string_state.state == Hex2) dynstr_appendc(&state.buffer, state.string_state.value);
                        else pawscript_encode_utf8(&state.buffer, state.string_state.value);
                        state.string_state.state = None;
                    }
                    break;
            }
        }
        else if (state.parse_state == ParsingWord) {
            if (state.word_state.first_char && (pawscript_is_numeric(c) || c == '.')) {
                state.word_state.first_char = false;
                state.word_state.dot_in_word = true;
                state.word_state.can_have_sign = true;
            }
            bool just_set = false;
            if ((c == 'e' || c == 'E' || c == 'p' || c == 'P') && state.word_state.can_have_sign) {
                state.word_state.can_be_sign = true;
                state.word_state.can_have_sign = false;
                just_set = true;
            }
            if (pawscript_is_alphanumeric(c) || (c == '.' && state.word_state.dot_in_word) || ((c == '+' || c == '-') && state.word_state.can_be_sign)) {
                if (c == '.') state.word_state.dot_in_word = false;
                if (state.word_state.can_be_sign && !just_set) state.word_state.can_be_sign = false;
                dynstr_appendc(&state.buffer, c);
            }
            else {
                Token* token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col);
                if      (pawscript_try_parse_int(state.buffer, &token->value.integer))  token->type = TOKEN_INTEGER;
                else if (pawscript_try_parse_flt(state.buffer, &token->value.floating)) token->type = TOKEN_FLOAT;
                else {
                    token->type = TOKEN_IDENTIFIER;
                    for (int i = 0; i < num_token_table_entries; i++) {
                        if (!token_table[i]) continue;
                        if (strcmp(token_table[i], state.buffer) == 0) token->type = i;
                    }
                    if (token->type == TOKEN_IDENTIFIER) {
                        Token* curr_token = token;
                        int len = strlen(state.buffer), ptr = 0;
                        char buf[len + 1];
                        for (int i = 0; i < len; i++) {
                            if (state.buffer[i] == '.') {
                                if (ptr != 0) {
                                    buf[ptr] = 0;
                                    if (!curr_token) curr_token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col + i - strlen(buf));
                                    curr_token->type = TOKEN_IDENTIFIER;
                                    curr_token->value.string = strdup(buf);
                                    curr_token = NULL;
                                }
                                if (!curr_token) curr_token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col + i);
                                curr_token->type = TOKEN_DOT;
                                curr_token = NULL;
                                ptr = 0;
                                continue;
                            }
                            buf[ptr++] = state.buffer[i];
                        }
                        if (ptr != 0) {
                            buf[ptr] = 0;
                            if (!curr_token) curr_token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col + len - strlen(buf));
                            curr_token->type = TOKEN_IDENTIFIER;
                            curr_token->value.string = strdup(buf);
                            curr_token = NULL;
                        }
                    }
                }
                dynstr_free(state.buffer);
                state.buffer = NULL;
                state.parse_state = Idle;
                no_increment = true;
            }
        }
        else if (state.parse_state == ParsingSymbol) {
            int starts_with = 0;
            int exact_match = -1;
            for (int i = 0; i < num_token_table_entries; i++) {
                if (!token_table[i]) continue;
                if (strncmp(state.buffer, token_table[i], strlen(state.buffer)) == 0) starts_with++;
                if (strcmp(state.buffer, token_table[i]) == 0) exact_match = i;
            }
            if (exact_match != -1 && (starts_with == 1 || !pawscript_is_symbol(c))) {
                Token* token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col);
                token->type = exact_match;
                dynstr_free(state.buffer);
                state.buffer = NULL;
                state.parse_state = Idle;
                no_increment = true;
                continue;
            }
            if (pawscript_is_symbol(c) && starts_with != 0) {
                dynstr_appendc(&state.buffer, c);
                starts_with = 0;
                for (int i = 0; i < num_token_table_entries; i++) {
                    if (!token_table[i]) continue;
                    if (strncmp(state.buffer, token_table[i], strlen(state.buffer)) == 0) starts_with++;
                }
                if (starts_with == 0 && exact_match != -1) {
                    Token* token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col);
                    token->type = exact_match;
                    dynstr_free(state.buffer);
                    state.buffer = NULL;
                    state.parse_state = Idle;
                    no_increment = true;
                }
                continue;
            }
            int len = strlen(state.buffer);
            if (exact_match == -1) pawscript_push_error(context, filename, row - len, col, "Invalid token");
        }
    }
    Token* token = pawscript_append_token(&tokens, num_tokens, filename, state.row, state.col);
    token->type = TOKEN_END_OF_FILE;
    (*num_tokens)--;
    return tokens;
}

static Token** pawscript_remove_comments(PawScriptContext* context, char* filename, Token** tokens, size_t* num_tokens) {
    *num_tokens = 0;
    Token** processed = NULL;
    size_t i = -1;
    int comment_line = 0;
    Token* multiline_comment = NULL;
    while (tokens[++i]->type != TOKEN_END_OF_FILE) {
        if (comment_line == tokens[i]->row) continue;
        if (multiline_comment) {
            if (tokens[i]->type == TOKEN_COMMENT_END) multiline_comment = NULL;
            continue;
        }
        if (tokens[i]->type == TOKEN_COMMENT) comment_line = tokens[i]->row;
        if (tokens[i]->type == TOKEN_COMMENT_START) multiline_comment = tokens[i];
        if (tokens[i]->type == TOKEN_COMMENT || tokens[i]->type == TOKEN_COMMENT_START) continue;
        (*num_tokens)++;
        processed = realloc(processed, sizeof(Token*) * *num_tokens);
        processed[*num_tokens - 1] = tokens[i];
    }
    free(tokens);
    if (multiline_comment) pawscript_push_error(context, filename, multiline_comment->row, multiline_comment->col, "Unterminated multiline comment");
    return processed;
}

static Token** pawscript_lex(PawScriptContext* context, const char* code, char* filename, size_t* num_tokens) {
    size_t len = strlen(code);
    char* with_newline = malloc(len + 2);
    memcpy(with_newline, code, len);
    with_newline[len + 0] = '\n';
    with_newline[len + 1] = 0;
    Token** tokens = pawscript_lex_internal(context, with_newline, filename, num_tokens);
    free(with_newline);
    if (!tokens) return NULL;
    return pawscript_remove_comments(context, filename, tokens, num_tokens);
}

static void pawscript_add_allocation(PawScriptScope* scope, void* ptr, size_t size, bool strict) {
    for (size_t i = 0; i < scope->num_allocs; i++) {
        if (scope->allocs[i]->ptr == NULL) {
            scope->allocs[i]->size = size;
            scope->allocs[i]->ptr = ptr;
            scope->allocs[i]->strict = strict;
            return;
        }
    }
    PawScriptAllocation* alloc = malloc(sizeof(PawScriptAllocation));
    alloc->size = size;
    alloc->ptr = ptr;
    alloc->strict = strict;
    scope->num_allocs++;
    scope->allocs = realloc(scope->allocs, sizeof(PawScriptAllocation*) * scope->num_allocs);
    scope->allocs[scope->num_allocs - 1] = alloc;
}

static void* pawscript_allocate(PawScriptScope* scope, size_t size, bool exec, bool strict) {
#ifdef _WIN32
    void* ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, (exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE));
    if (!ptr) return NULL;
#else
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | (exec ? PROT_EXEC : 0), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;
#endif
    memset(ptr, 0, size);
    pawscript_add_allocation(scope, ptr, size, strict);
    return ptr;
}

static void pawscript_free_allocation(PawScriptContext* context, void* ptr, bool free_strict) {
    PawScriptScope* curr = context->scope;
    while (curr) {
        for (size_t i = 0; i < curr->num_allocs; i++) {
            if (!free_strict && curr->allocs[i]->strict) continue;
            if (curr->allocs[i]->ptr == ptr) {
#ifdef _WIN32
                VirtualFree(curr->allocs[i]->ptr, curr->allocs[i]->size, MEM_RELEASE);
#else
                munmap(curr->allocs[i]->ptr, curr->allocs[i]->size);
#endif
                curr->allocs[i]->ptr = NULL;
                return;
            }
        }
        curr = curr->parent;
    }
}

static void pawscript_move_allocation(PawScriptContext* context, PawScriptScope* target, void* ptr) {
    PawScriptScope* curr = context->scope;
    while (curr) {
        if (curr != target) for (size_t i = 0; i < curr->num_allocs; i++) {
            if (curr->allocs[i]->strict) continue;
            if (curr->allocs[i]->ptr == ptr) {
                pawscript_add_allocation(target, curr->allocs[i]->ptr, curr->allocs[i]->size, false);
                curr->allocs[i]->ptr = NULL;
                return;
            }
        }
        curr = curr->parent;
    }
}

static PawScriptScope* pawscript_find_allocation_scope(PawScriptContext* context, void* ptr) {
    PawScriptScope* curr = context->scope;
    while (curr) {
        for (size_t i = 0; i < curr->num_allocs; i++) {
            if (curr->allocs[i]->ptr == ptr) return curr;
        }
        curr = curr->parent;
    }
    return NULL;
}

static PawScriptAllocation* pawscript_find_allocation(PawScriptContext* context, void* ptr, size_t size) {
    PawScriptScope* curr = context->scope;
    while (curr) {
        for (size_t i = 0; i < curr->num_allocs; i++) {
            if (curr->allocs[i]->ptr == NULL) continue;
            if (
                (uint64_t)curr->allocs[i]->ptr <= (uint64_t)ptr &&
                (uint64_t)curr->allocs[i]->ptr + curr->allocs[i]->size >= (uint64_t)ptr + size
            )
            return curr->allocs[i];
        }
        curr = curr->parent;
    }
    return NULL;
}

static Variable* pawscript_create_variable(PawScriptContext* context, Type* type, void* address) {
    PawScriptScope* scope = context->scope;
    while (scope->parent) scope = scope->parent;
    Variable* variable = calloc(sizeof(Variable), 1);
    variable->no_alloc = address != NULL;
    variable->address = address ? address : pawscript_allocate(scope, pawscript_sizeof(type), false, true);
    variable->type = pawscript_copy_type(type);
    return variable;
}

static void pawscript_destroy_variable(PawScriptContext* context, Variable* variable) {
    pawscript_destroy_type(variable->type);
    if (!variable->no_alloc) pawscript_free_allocation(context, variable->address, true);
    free(variable);
}

static void pawscript_push_scope(PawScriptContext* context) {
    PawScriptScope* scope = calloc(sizeof(PawScriptScope), 1);
    scope->parent = context->scope;
    context->scope = scope;
}

static void pawscript_pop_scope(PawScriptContext* context) {
    PawScriptScope* scope = context->scope;
    context->scope = scope->parent;
    for (size_t i = 0; i < scope->num_variables; i++) {
        pawscript_destroy_variable(context, scope->variables[i]);
    }
    for (size_t i = 0; i < scope->num_allocs; i++) {
        if (!scope->allocs[i]) continue;
#ifdef _WIN32
        VirtualFree(scope->allocs[i]->ptr, scope->allocs[i]->size, MEM_RELEASE);
#else
        munmap(scope->allocs[i]->ptr, scope->allocs[i]->size);
#endif
        free(scope->allocs[i]);
    }
    for (size_t i = 0; i < scope->num_typedefs; i++) {
        pawscript_destroy_type(scope->typedefs[i]);
    }
    free(scope->variables);
    free(scope->allocs);
    free(scope->typedefs);
    free(scope);
}

static Variable* pawscript_find_variable(PawScriptContext* context, const char* name, PawScriptScope** out_scope) {
    PawScriptScope* scope = context->scope;
    bool out_of_function = false;
    while (scope) {
        if (!out_of_function || scope->parent == NULL) for (size_t i = 0; i < scope->num_variables; i++) {
            if (strcmp(scope->variables[i]->type->name, name) == 0) {
                if (out_scope) *out_scope = scope;
                return scope->variables[i];
            }
        }
        if (scope->type == SCOPE_FUNCTION) out_of_function = true;
        scope = scope->parent;
    }
    return NULL;
}

static Type* pawscript_find_type(PawScriptContext* context, const char* name) {
    PawScriptScope* scope = context->scope;
    bool out_of_function = false;
    while (scope) {
        if (!out_of_function || scope->parent == NULL) for (size_t i = 0; i < scope->num_typedefs; i++) {
            if (strcmp(scope->typedefs[i]->name, name) == 0) return scope->typedefs[i];
        }
        if (scope->type == SCOPE_FUNCTION) out_of_function = true;
        scope = scope->parent;
    }
    return NULL;
}

static Variable* pawscript_declare_variable(PawScriptContext* context, Type* type, void* address) {
    if (!type->name) return NULL;
    if (pawscript_find_type(context, type->name)) return NULL;
    PawScriptScope* scope = context->scope;
    for (size_t i = 0; i < scope->num_variables; i++) {
        if (strcmp(scope->variables[i]->type->name, type->name) == 0) return NULL;
    }
    Variable* variable = pawscript_create_variable(context, type, address);
    pawscript_make_original(variable->type);
    scope->num_variables++;
    scope->variables = realloc(scope->variables, sizeof(Variable*) * scope->num_variables);
    return scope->variables[scope->num_variables - 1] = variable;
}

static bool pawscript_declare_type(PawScriptContext* context, Type* type) {
    if (!type->name) return false;
    PawScriptScope* scope = context->scope;
    for (size_t i = 0; i < scope->num_typedefs; i++) {
        if (scope->typedefs[i]->is_incomplete && strcmp(scope->typedefs[i]->name, type->name) == 0) {
            pawscript_destroy_type(scope->typedefs[i]);
            scope->typedefs[i] = pawscript_make_original(pawscript_copy_type(type));
            return true;
        }
    }
    if (pawscript_find_type(context, type->name)) return false;
    if (pawscript_find_variable(context, type->name, NULL)) return false;
    scope->num_typedefs++;
    scope->typedefs = realloc(scope->typedefs, sizeof(Type*) * scope->num_typedefs);
    scope->typedefs[scope->num_typedefs - 1] = pawscript_make_original(pawscript_copy_type(type));
    return true;
}

static void* pawscript_get_symbol(const char* name) {
#ifdef _WIN32
    return GetProcAddress(GetModuleHandle(NULL), name);
#else
    return dlsym(NULL, name);
#endif
}

static size_t pawscript_alignof(Type* type) {
    if (type->kind == TYPE_STRUCT) {
        size_t max_align = 1;
        for (size_t i = 0; i < type->struct_info.num_fields; i++) {
            size_t alignment = pawscript_alignof(type->struct_info.fields[i]);
            if (max_align < alignment) max_align = alignment;
        }
        return max_align;
    }
    return pawscript_sizeof(type);
}

static size_t pawscript_get_offset(Type* structure, Type* field) {
    size_t struct_size = 0;
    for (size_t i = 0; i < structure->struct_info.num_fields; i++) {
        size_t field = pawscript_sizeof(structure->struct_info.fields[i]) + structure->struct_info.field_offsets[i];
        if (struct_size < field) struct_size = field;
    }
    size_t alignment = pawscript_alignof(field);
    if (struct_size % alignment == 0) return struct_size;
    return struct_size + (alignment - struct_size % alignment);
}

static bool pawscript_is_symbol_allowed(PawScriptContext* context, void* addr) {
    bool is_registered = false;
    for (size_t i = 0; i < context->num_addresses; i++) {
        if (context->addresses[i] == addr) {
            is_registered = true;
            break;
        }
    }
    return is_registered == (context->address_visibility == VISIBILITY_WHITELIST);
}

static bool pawscript_is_truthy(Variable* variable) {
    switch (variable->type->kind) {
        case TYPE_32FLT: return *(float*)variable->address != 0;
        case TYPE_64FLT: return *(double*)variable->address != 0;
        case TYPE_8BIT: return *(uint8_t*)variable->address != 0;
        case TYPE_16BIT: return *(uint16_t*)variable->address != 0;
        case TYPE_32BIT: return *(uint32_t*)variable->address != 0;
        default: return *(uint64_t*)variable->address != 0;
    }
}

static bool pawscript_get_integer(Variable* variable, uint64_t* out);
static Variable* pawscript_evaluate_expression(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i, bool no_void);
static bool pawscript_evaluate_statement(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i);
static bool pawscript_evaluate(PawScriptContext* context, Token** tokens, size_t num_tokens);
static bool pawscript_scan_until(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i, TokenKind type);

bool pawscript_run_file_inner(PawScriptContext* context, const char* filename, Token* token) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        pawscript_push_error(context,
            token ? token->filename : NULL,
            token ? token->row      : 0,
            token ? token->col      : 0,
            "Cannot open file '%s' for reading: %s",
            filename, strerror(errno)
        );
        return false;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* code = malloc(size + 1);
    fread(code, size, 1, f);
    fclose(f);
    code[size] = 0;

    size_t num_tokens;
    char* filename_dup = strdup(filename);
    Token** tokens = pawscript_lex(context, code, filename_dup, &num_tokens);
    bool result = pawscript_evaluate(context, tokens, num_tokens);
    pawscript_append_tokens(&context->tokens, &context->num_tokens, tokens, num_tokens);
    free(code);
    context->num_strings++;
    context->strings = realloc(context->strings, sizeof(char*) * context->num_strings);
    context->strings[context->num_strings - 1] = filename_dup;
    return result;
}

static void pawscript_set_state(PawScriptContext* context, PawScriptState state, Variable* var) {
    context->state_var = 0;
    context->state = state;
    if (var) memcpy(&context->state_var, var->address, pawscript_sizeof(var->type));
}

#ifdef _WIN32 // shut up
#undef FAILED
#undef ERROR
#endif

static Type* pawscript_parse_type(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i) {
#define FAILED ({ if (type) pawscript_destroy_type(type); NULL; })
#define ERROR(msg, ...) ({ pawscript_push_error(context, token->filename, token->row, token->col, msg " (interpreter line %d)", __VA_ARGS__ __VA_OPT__(,) __LINE__); FAILED; })
#define NEXT ({ (*i)++; token = CURR; if (*i > num_tokens) return ERROR("Unexpected end of expression"); CURR; })
#define CURR tokens[*i]
#define CREATE_TYPE(type_kind, unsigned) type = pawscript_create_type(type_kind); type->is_unsigned = unsigned; type->is_const = is_const; token = NEXT; break;
    Token* token = tokens[*i];
    Type* type = NULL;
    bool is_const = false;
    if (token->type == TOKEN_const) {
        token = NEXT;
        is_const = true;
    }
    switch (token->type) {
        case TOKEN_bool:
        case TOKEN_u8:     CREATE_TYPE(TYPE_8BIT,   true)
        case TOKEN_u16:    CREATE_TYPE(TYPE_16BIT,  true)
        case TOKEN_u32:    CREATE_TYPE(TYPE_32BIT,  true)
        case TOKEN_u64:    CREATE_TYPE(TYPE_64BIT,  true)
        case TOKEN_s8:     CREATE_TYPE(TYPE_8BIT,   false)
        case TOKEN_s16:    CREATE_TYPE(TYPE_16BIT,  false)
        case TOKEN_s32:    CREATE_TYPE(TYPE_32BIT,  false)
        case TOKEN_s64:    CREATE_TYPE(TYPE_64BIT,  false)
        case TOKEN_f32:    CREATE_TYPE(TYPE_32FLT,  false)
        case TOKEN_f64:    CREATE_TYPE(TYPE_64FLT,  false)
        case TOKEN_void:   CREATE_TYPE(TYPE_VOID,   false)
        case TOKEN_struct: {
            type = pawscript_create_type(TYPE_STRUCT);
            type->is_incomplete = true;
            token = NEXT;
            char* struct_name = NULL;
            if (token->type == TOKEN_IDENTIFIER) {
                struct_name = token->value.string;
                type->name = struct_name;
                if (!pawscript_declare_type(context, type)) return ERROR("Identifier already taken");
                type->name = NULL;
                token = NEXT;
            }
            if (token->type == TOKEN_COLON) {
                token = NEXT;
                if (token->type != TOKEN_IDENTIFIER) return ERROR("Expected identifier");
                pawscript_destroy_type(type);
                type = pawscript_copy_type(pawscript_find_type(context, token->value.string));
                if (!type) return ERROR("Undefined type '%s'", token->value.string);
                if (type->kind != TYPE_STRUCT) return ERROR("Type '%s' is not a struct", type->name);
                free(type->name);
                type->name = NULL;
                token = NEXT;
            }
            if (token->type != TOKEN_BRACE_OPEN) return ERROR("Expected '{'");
            token = NEXT;
            while (true) {
                if (token->type == TOKEN_BRACE_CLOSE) break;
                if (token->type == TOKEN_SEMICOLON) {
                    token = NEXT;
                    continue;
                }
                Type* field = pawscript_parse_type(context, tokens, num_tokens, i);
                if (!field) {
                    if (pawscript_any_errors(context)) return FAILED;
                    if (token->type == TOKEN_IDENTIFIER) return ERROR("Undefined type '%s'", token->value.string);
                    else return ERROR("Invalid token");
                }
                token = CURR;
                size_t offset = pawscript_get_offset(type, field);
                if (token->type == TOKEN_AT) {
                    token = NEXT;
                    pawscript_push_scope(context);
                    Type* offset_type = pawscript_create_type(TYPE_64BIT);
                    offset_type->is_unsigned = true;
                    for (size_t i = 0; i <= type->struct_info.num_fields; i++) {
                        if (offset_type->name) free(offset_type->name);
                        offset_type->name = strdup(i == type->struct_info.num_fields ? field->name : type->struct_info.fields[i]->name);
                        *(uint64_t*)pawscript_declare_variable(context, offset_type, NULL)->address = i == type->struct_info.num_fields ? offset : type->struct_info.field_offsets[i];
                    }
                    pawscript_destroy_type(offset_type);
                    Variable* result = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
                    if (!result) return FAILED;
                    if (!pawscript_get_integer(result, &offset)) return ERROR("Expression must return an integer");
                    pawscript_pop_scope(context);
                    token = CURR;
                }
                if (token->type != TOKEN_SEMICOLON) return ERROR("Expected ';'");
                if (!field->name) {
                    pawscript_destroy_type(field);
                    return ERROR("Nameless field\n");
                }
                else {
                    type->struct_info.num_fields++;
                    type->struct_info.field_offsets = realloc(type->struct_info.field_offsets, sizeof(size_t) * type->struct_info.num_fields);
                    type->struct_info.fields = realloc(type->struct_info.fields, sizeof(size_t) * type->struct_info.num_fields);
                    type->struct_info.field_offsets[type->struct_info.num_fields - 1] = offset;
                    type->struct_info.fields[type->struct_info.num_fields - 1] = field;
                    if (field->kind == TYPE_VOID || field->is_incomplete) return ERROR("Cannot define field with incomplete or void type");
                }
                token = NEXT;
            }
            token = NEXT;
            type->is_incomplete = false;
            pawscript_replace_incompletes(type, type);
            if (struct_name) {
                type->name = struct_name;
                pawscript_declare_type(context, type);
                type->name = NULL;
            }
        } break;
        case TOKEN_IDENTIFIER: {
            type = pawscript_find_type(context, token->value.string);
            if (!type) {
                if (is_const) return ERROR("Undefined type '%s'", token->value.string);
                else if (pawscript_find_variable(context, token->value.string, NULL)) return FAILED;
                else return ERROR("Undefined identifier '%s'", token->value.string);
            }
            type = pawscript_copy_type(type);
            type->is_const = is_const;
            free(type->name);
            type->name = NULL;
            token = NEXT;
        } break;
        default: if (is_const) return ERROR("Invalid token"); return FAILED;
    }
    while (true) {
        if (token->type == TOKEN_DOUBLE_ASTERISK) {
            Type* ptrptr = pawscript_create_type(TYPE_POINTER);
            Type* ptr = pawscript_create_type(TYPE_POINTER);
            ptrptr->pointer_info.base = ptr;
            ptr->pointer_info.base = type;
            type = ptrptr;
            token = NEXT;
            continue;
        }
        if (token->type == TOKEN_ASTERISK) {
            Type* ptr = pawscript_create_type(TYPE_POINTER);
            ptr->pointer_info.base = type;
            type = ptr;
            token = NEXT;
            continue;
        }
        if (token->type == TOKEN_const) {
            if (type->is_const) return ERROR("Already const");
            type->is_const = true;
            token = NEXT;
            continue;
        }
        if (token->type == TOKEN_PARENTHESIS_OPEN) {
            if (type->kind == TYPE_STRUCT) return ERROR("Cannot define a function that returns a non-pointer struct");
            token = NEXT;
            Type* func = pawscript_create_type(TYPE_FUNCTION);
            func->function_info.return_value = type;
            type = func;
            if (token->type == TOKEN_PARENTHESIS_CLOSE) {
                token = NEXT;
                continue;
            }
            while (true) {
                if (token->type == TOKEN_TRIPLE_DOT) {
                    Type* varargs = pawscript_create_type(TYPE_VARARGS);
                    varargs->name = strdup("...");
                    token = NEXT;
                    type->function_info.num_args++;
                    type->function_info.arg_types = realloc(type->function_info.arg_types, sizeof(Type*) * type->function_info.num_args);
                    type->function_info.arg_types[type->function_info.num_args - 1] = varargs;
                    if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
                    break;
                }
                Type* arg = pawscript_parse_type(context, tokens, num_tokens, i);
                if (!arg) {
                    if (pawscript_any_errors(context)) return FAILED;
                    if (token->type == TOKEN_IDENTIFIER) return ERROR("Undefined type '%s'", token->value.string);
                    else return ERROR("Invalid token");
                }
                type->function_info.num_args++;
                type->function_info.arg_types = realloc(type->function_info.arg_types, sizeof(Type*) * type->function_info.num_args);
                type->function_info.arg_types[type->function_info.num_args - 1] = arg;
                if (arg->kind == TYPE_VOID) return ERROR("Cannot define argument with void type");
                if (arg->kind == TYPE_STRUCT) return ERROR("Cannot define argument with struct type");
                token = CURR;
                if (!arg->name) return ERROR("Expected identifier");
                if (token->type == TOKEN_COMMA) {
                    token = NEXT;
                    continue;
                }
                if (token->type == TOKEN_PARENTHESIS_CLOSE) break;
                return ERROR("Expected ',' or ')'");
            }
            token = NEXT;
            continue;
        }
        if (token->type == TOKEN_IDENTIFIER) {
            type->name = strdup(token->value.string);
            token = NEXT;
        }
        break;
    }
    return type;
#undef FAILED
#undef CREATE_TYPE
}

#define FAILED NULL

static bool pawscript_can_dereference(PawScriptContext* context, Variable* variable) {
    void* addr = *(void**)variable->address;
    if (addr == NULL) return false;
    if (variable->type->is_native) return true;
    return pawscript_find_allocation(context, addr, variable->type->kind == TYPE_FUNCTION
        ? pawscript_find_allocation(context, addr, 1) ? sizeof(Function) : sizeof(void*)
        : pawscript_sizeof(variable->type->pointer_info.base)
    );
}

static bool pawscript_matches_value_type(ValueType type, Variable* var) {
    if (!var) return type == Null;
    switch (type) {
        case Null:    return var == NULL;
        case Any:     return var != NULL;
        case Number:  return var->type->kind >= TYPE_8BIT && var->type->kind <= TYPE_64FLT;
        case Integer: return var->type->kind >= TYPE_8BIT && var->type->kind <= TYPE_64BIT;
        case Pointer: return var->type->kind == TYPE_POINTER || var->type->kind == TYPE_FUNCTION;
        case Assignable: return var->type->name != NULL;
    }
    return false;
}

static Variable* pawscript_rvalue_to_lvalue(Variable* variable) {
    if (variable->type->name) free(variable->type->name);
    variable->type->name = NULL;
    return variable;
}

static Variable* pawscript_lvalue_to_rvalue(Variable* variable) {
    pawscript_rvalue_to_lvalue(variable);
    variable->type->name = strdup("");
    return variable;
}

static bool pawscript_get_integer(Variable* variable, uint64_t* out) {
    if (!pawscript_matches_value_type(Integer, variable)) return false;
    *out = 0;
    size_t size = pawscript_sizeof(variable->type);
    memcpy(out, variable->address, size);
    bool negative = !variable->type->is_unsigned && *out >> (size * 8 - 1);
    if (negative) memset((char*)out + size, 0xFF, 8 - size);
    return true;
}

static Variable* pawscript_cast(PawScriptContext* context, Type* target, Variable* src, bool bitcast) {
    Variable* dst = pawscript_create_variable(context, target, NULL);
    pawscript_make_native(target, dst->type->is_native);
    if (dst->type->kind == TYPE_VOID) return dst;
    if (bitcast) {
        uint64_t value = 0;
        memcpy(&value, src->address, pawscript_sizeof(src->type));
        memcpy(dst->address, &value, pawscript_sizeof(dst->type));
    }
    else if (pawscript_matches_value_type(Integer, dst)) {
        if (pawscript_matches_value_type(Integer, src)) {
            uint64_t value;
            pawscript_get_integer(src, &value);
            memcpy(dst->address, &value, pawscript_sizeof(dst->type));
        }
        else if (pawscript_matches_value_type(Number, src)) {
            int64_t value;
            if (src->type->kind == TYPE_32FLT) value = *( float*)src->address;
            if (src->type->kind == TYPE_64FLT) value = *(double*)src->address;
            memcpy(dst->address, &value, pawscript_sizeof(dst->type));
        }
        else if (pawscript_matches_value_type(Pointer, src)) {
            memcpy(dst->address, src->address, pawscript_sizeof(dst->type));
        }
    }
    else if (pawscript_matches_value_type(Number, dst)) {
        if (pawscript_matches_value_type(Integer, src)) {
            int64_t value;
            pawscript_get_integer(src, (uint64_t*)&value);
            if (dst->type->kind == TYPE_32FLT) *( float*)dst->address = value;
            if (dst->type->kind == TYPE_64FLT) *(double*)dst->address = value;
        }
        else if (pawscript_matches_value_type(Number, src)) {
            if (dst->type->kind == src->type->kind) memcpy(dst->address, src->address, pawscript_sizeof(dst->type));
            else if (dst->type->kind == TYPE_32FLT) *( float*)dst->address = *(double*)src->address;
            else if (dst->type->kind == TYPE_64FLT) *(double*)dst->address = *( float*)src->address;
        }
        else if (pawscript_matches_value_type(Pointer, src)) {
            int64_t value = *(int64_t*)src->address;
            if (dst->type->kind == TYPE_32FLT) *( float*)dst->address = value;
            if (dst->type->kind == TYPE_64FLT) *(double*)dst->address = value;
        }
    }
    else if (pawscript_matches_value_type(Pointer, dst)) {
        if (pawscript_matches_value_type(Integer, src)) {
            pawscript_get_integer(src, dst->address);
        }
        else if (pawscript_matches_value_type(Number, src)) {
            int64_t value;
            if (src->type->kind == TYPE_32FLT) value = *( float*)src->address;
            if (src->type->kind == TYPE_64BIT) value = *(double*)src->address;
            *(int64_t*)dst->address = value;
        }
        else if (pawscript_matches_value_type(Pointer, src)) {
            *(void**)dst->address = *(void**)src->address;
        }
    }
    return dst;
}

static FuncCall* pawscript_create_call(PawScriptContext* context) {
    FuncCall* call = calloc(sizeof(FuncCall), 1);
    call->context = context;
    return call;
}

static void pawscript_destroy_call(FuncCall* call) {
    for (size_t i = 0; i < call->num_parameters; i++) {
        pawscript_destroy_variable(call->context, call->parameters[i]);
    }
    free(call->parameters);
    free(call);
}

static void pawscript_this(FuncCall* call, Variable* this) {
    free(this->type->name);
    this->type->name = strdup("this");
    call->this = this;
}

static void pawscript_call_push(FuncCall* call, Variable* variable) {
    call->num_parameters++;
    call->parameters = realloc(call->parameters, sizeof(void*) * call->num_parameters);
    call->parameters[call->num_parameters - 1] = variable;
}

static void pawscript_ffi_push_variable(FFI* ffi, Variable* variable) {
    uint64_t value;
    if      (variable->type->kind == TYPE_32FLT) ffi_push_float (ffi, *( float*)variable->address);
    else if (variable->type->kind == TYPE_64FLT) ffi_push_double(ffi, *(double*)variable->address);
    else if (pawscript_get_integer(variable, &value)) ffi_push_int(ffi, value);
    else ffi_push_int(ffi, *(uint64_t*)variable->address);
}

static Variable* pawscript_call(FuncCall* call, PawScriptContext* context, Token* token, Variable* func) {
    bool has_varargs = func->type->function_info.num_args != 0 && func->type->function_info.arg_types[func->type->function_info.num_args - 1]->kind == TYPE_VARARGS;
    if (has_varargs) {
        if (call->num_parameters < func->type->function_info.num_args - 1)
            return ERROR("Function expects at least %d parameter%s, but %d provided",
                func->type->function_info.num_args - 1,
                func->type->function_info.num_args == 2 ? "" : "s",
                call->num_parameters);
    }
    else if (func->type->function_info.num_args != call->num_parameters)
        return ERROR("Function expects exactly %d parameter%s, but %d provided",
            func->type->function_info.num_args,
            func->type->function_info.num_args == 1 ? "" : "s",
            call->num_parameters);
    void* addr = *(void**)func->address;
    Variable* params[func->type->function_info.num_args];
    PawScriptVarargs* varargs = NULL;
    for (size_t i = 0; i < func->type->function_info.num_args; i++) {
        if (func->type->function_info.arg_types[i]->kind == TYPE_VARARGS) {
            varargs = calloc(sizeof(PawScriptVarargs), 1);
            params[i] = pawscript_create_variable(context, func->type->function_info.arg_types[i], varargs);
            for (; i < call->num_parameters; i++) {
                varargs->count++;
                varargs->args = realloc(varargs->args, sizeof(Variable*) * varargs->count);
                varargs->args[varargs->count - 1] = pawscript_rvalue_to_lvalue(call->parameters[i]);
            }
            break;
        }
        params[i] = pawscript_cast(context, func->type->function_info.arg_types[i], call->parameters[i], false);
        if (params[i]->type->name) free(params[i]->type->name);
        params[i]->type->name = strdup(func->type->function_info.arg_types[i]->name);
        pawscript_make_native(params[i]->type, call->parameters[i]->type->is_native);
    }
    Variable* retval;
    if (pawscript_find_allocation_scope(context, addr)) {
        Function* f = addr;
        pawscript_push_scope(context);
        context->scope->type = SCOPE_FUNCTION;
        if (call->this) pawscript_declare_variable(context, call->this->type, call->this->address);
        for (size_t i = 0; i < func->type->function_info.num_args; i++) {
            pawscript_declare_variable(context, params[i]->type, params[i]->address);
        }
        pawscript_evaluate(context, f->start, f->num_tokens);
        pawscript_pop_scope(context);
        retval = pawscript_create_variable(context, func->type->function_info.return_value, NULL);
        memcpy(retval->address, &context->state_var, pawscript_sizeof(retval->type));
        pawscript_set_state(context, STATE_RUNNING, NULL);
    }
    else {
        FFI* ffi = ffi_create();
        for (size_t i = 0; i < func->type->function_info.num_args; i++) {
            uint64_t value;
            if (params[i]->type->kind == TYPE_VARARGS) {
                ffi_varargs(ffi);
                for (size_t j = 0; j < varargs->count; j++) pawscript_ffi_push_variable(ffi, varargs->args[j]);
            }
            else pawscript_ffi_push_variable(ffi, params[i]);
        }
        ffi_call(ffi, addr);
        Type* type = pawscript_create_type(TYPE_64BIT);
        if (func->type->function_info.return_value->kind == TYPE_32FLT) type->kind = TYPE_32FLT;
        if (func->type->function_info.return_value->kind == TYPE_64FLT) type->kind = TYPE_64FLT;
        Variable* out = pawscript_create_variable(context, type, NULL);
        if      (type->kind == TYPE_32FLT) *( float*)out->address = ffi_get_float (ffi);
        else if (type->kind == TYPE_64FLT) *(double*)out->address = ffi_get_double(ffi);
        else *(uint64_t*)out->address = ffi_get_int(ffi);
        retval = pawscript_cast(context, func->type->function_info.return_value, out, false);
        pawscript_make_native(retval->type, true);
        pawscript_destroy_type(type);
        pawscript_destroy_variable(context, out);
        ffi_destroy(ffi);
    }
    if (call->this) pawscript_destroy_variable(context, call->this);
    for (size_t i = 0; i < func->type->function_info.num_args; i++) {
        if (func->type->function_info.arg_types[i]->kind == TYPE_VARARGS) {
            PawScriptVarargs* varargs = params[i]->address;
            free(varargs->args);
            free(varargs);
        }
        pawscript_destroy_variable(context, params[i]);
    }
    return retval;
}

static void pawscript_serialize_type_inner(Type* type, char** dynarr, size_t* num_visited, void*** visited) {
#define write(...) dynarr_append(_arr, (char[]){ __VA_ARGS__ }, sizeof((char[]){ __VA_ARGS__ }))
#define writev(type, x) ({ type val = x; dynarr_append(_arr, &val, sizeof(type)); })
#define writes(x) dynarr_append(_arr, x, strlen(x) + 1)
#define _arr dynarr
    for (size_t i = 0; i < *num_visited; i++) {
        if ((*visited)[i * 2 + 0] == type) {
            write(0xFF);
            writev(uint32_t, (uint64_t)(*visited)[i * 2 + 1]);
            return;
        }
    }
    (*num_visited)++;
    *visited = realloc(*visited, sizeof(void*) * 2 * *num_visited);
    (*visited)[(*num_visited - 1) * 2 + 0] = type;
    (*visited)[(*num_visited - 1) * 2 + 1] = (void*)(uint64_t)dynarr_length(*dynarr);
    write(type->kind | (!!type->name << 7) | (type->is_const << 6) | (type->is_unsigned << 5));
    if (type->name) writes(type->name);
    if (type->kind == TYPE_POINTER) pawscript_serialize_type_inner(type->pointer_info.base, dynarr, num_visited, visited);
    if (type->kind == TYPE_STRUCT) {
        writev(uint32_t, type->struct_info.num_fields);
        for (size_t i = 0; i < type->struct_info.num_fields; i++) {
            writev(uint32_t, type->struct_info.field_offsets[i]);
            pawscript_serialize_type_inner(type->struct_info.fields[i], dynarr, num_visited, visited);
        }
    }
    if (type->kind == TYPE_FUNCTION) {
        pawscript_serialize_type_inner(type->function_info.return_value, dynarr, num_visited, visited);
        writev(uint32_t, type->function_info.num_args);
        for (size_t i = 0; i < type->function_info.num_args; i++) {
            pawscript_serialize_type_inner(type->function_info.arg_types[i], dynarr, num_visited, visited);
        }
    }
#undef _arr
}

static char* pawscript_serialize_type(Type* type, size_t* length) {
    void** visited = NULL;
    size_t num_visited = 0;
    char* dynarr = dynarr_create();
    pawscript_serialize_type_inner(type, &dynarr, &num_visited, &visited);
    free(visited);
    *length = dynarr_length(dynarr);
    return dynarr_regular(dynarr);
}

static Type* pawscript_deserialize_type_inner(char* array, size_t* ptr, size_t* num_visited, void*** visited) {
    if (array[*ptr] == -1) {
        (*ptr)++;
        uint32_t offset = *(uint32_t*)(array + *ptr); *ptr += 4;
        for (size_t i = 0; i < offset; i++) {
            if ((uint64_t)(*visited)[i * 2 + 1] == offset) return (*visited)[i * 2 + 0];
        }
        return NULL;
    }
    Type* type = pawscript_create_type(array[*ptr] & 31);
    uint8_t type_id = array[(*ptr)++];
    type->is_const = (type_id >> 6) & 1;
    type->is_unsigned = (type_id >> 5) & 1;
    (*num_visited)++;
    *visited = realloc(*visited, sizeof(void*) * 2 * *num_visited);
    (*visited)[(*num_visited - 1) * 2 + 0] = type;
    (*visited)[(*num_visited - 1) * 2 + 1] = (void*)(*ptr - 1);
    if ((type_id >> 7) & 1) {
        type->name = strdup(array + *ptr);
        *ptr += strlen(type->name) + 1;
    }
    if (type->kind == TYPE_POINTER) type->pointer_info.base = pawscript_deserialize_type_inner(array, ptr, num_visited, visited);
    if (type->kind == TYPE_STRUCT) {
        type->struct_info.num_fields = *(uint32_t*)(array + *ptr); *ptr += 4;
        type->struct_info.fields = malloc(sizeof(Type*) * type->struct_info.num_fields);
        type->struct_info.field_offsets = malloc(sizeof(size_t) * type->struct_info.num_fields);
        for (size_t i = 0; i < type->struct_info.num_fields; i++) {
            type->struct_info.field_offsets[i] = *(uint32_t*)(array + *ptr); *ptr += 4;
            type->struct_info.fields[i] = pawscript_deserialize_type_inner(array, ptr, num_visited, visited);
        }
    }
    if (type->kind == TYPE_FUNCTION) {
        type->function_info.return_value = pawscript_deserialize_type_inner(array, ptr, num_visited, visited);
        type->function_info.num_args = *(uint32_t*)(array + *ptr); *ptr += 4;
        type->function_info.arg_types = malloc(sizeof(Type*) * type->function_info.num_args);
        for (size_t i = 0; i < type->function_info.num_args; i++) {
            type->function_info.arg_types[i] = pawscript_deserialize_type_inner(array, ptr, num_visited, visited);
        }
    }
    return type;
}

static Type* pawscript_deserialize_type(char* array) {
    void** visited = NULL;
    size_t num_visited = 0;
    size_t ptr = 0;
    Type* type = pawscript_deserialize_type_inner(array, &ptr, &num_visited, &visited);
    free(visited);
    return type;
}

static uint64_t pawscript_call_driver(PawScriptContext* context, char* type_data, uint64_t* arguments, Function* function) {
    Type* type = pawscript_deserialize_type(type_data);
    Variable* func = pawscript_create_variable(context, type, NULL);
    FuncCall* call = pawscript_create_call(context);
    *(void**)func->address = function;
    for (size_t i = 0; i < func->type->function_info.num_args; i++) {
        if (func->type->function_info.arg_types[i]->kind == TYPE_VARARGS) {
            uint64_t num_varargs = arguments[-(i + 1)];
            PawScriptVarargItem* items = (void*)arguments[-(i + 2)];
            for (size_t j = 0; j < num_varargs; j++) {
                size_t num_tokens, iter = 0;
                Token** tokens = pawscript_lex(context, items[j].type_str, NULL, &num_tokens);
                Type* type = pawscript_parse_type(context, tokens, num_tokens, &iter);
                if (!type) {
                    if (tokens[0]->type == TOKEN_IDENTIFIER)
                        pawscript_push_error(context, NULL, tokens[0]->row, tokens[0]->col, "Undefined type '%s'", tokens[0]->value.string);
                    pawscript_destroy_tokens(tokens, num_tokens);
                    return 0;
                }
                if (type->kind == TYPE_STRUCT || type->kind == TYPE_VOID) {
                    pawscript_destroy_type(type);
                    pawscript_push_error(context, NULL, tokens[0]->row, tokens[0]->col, "Cannot define vararg of struct or void type");
                    return 0;
                }
                pawscript_destroy_tokens(tokens, num_tokens);
                Variable* variable = pawscript_create_variable(context, type, &items[j].data);
                pawscript_call_push(call, variable);
            }
        }
        else {
            Variable* param = pawscript_create_variable(context, func->type->function_info.arg_types[i], &arguments[-(i + 1)]); // this is intentional i swear
            pawscript_call_push(call, param);
        }
    }
    uint64_t value;
    Variable* out = pawscript_call(call, context, NULL, func);
    memcpy(&value, out->address, pawscript_sizeof(out->type));
    pawscript_destroy_type(type);
    pawscript_destroy_call(call);
    pawscript_destroy_variable(context, func);
    pawscript_destroy_variable(context, out);
    return value;
}

static void pawscript_generate_args_assembly(char** assembly, int* int_reg, int* flt_reg, int* stack_ptr, bool is_float) {
#define _arr assembly
    write(0x48, 0x83, 0xEC, 0x08);                                                     // sub rsp, 8
    if (is_float) {
        if (*flt_reg < NUM_FLT_REGS) {
            write(0xF2, 0x0F, 0x11, *flt_reg * 8 + 4, 0x24);                           // movsd [rsp], xmmN
            flt_reg++;
            return;
        }
    }
    else {
        if (*int_reg < NUM_INT_REGS) {
            switch (*int_reg) {
                case 0: write(0x48, 0x89, 0x3C, 0x24); break;                          // mov [rsp], rdi
                case 1: write(0x48, 0x89, 0x34, 0x24); break;                          // mov [rsp], rsi
                case 2: write(0x48, 0x89, 0x14, 0x24); break;                          // mov [rsp], rdx
                case 3: write(0x48, 0x89, 0x0C, 0x24); break;                          // mov [rsp], rcx
                case 4: write(0x4C, 0x89, 0x04, 0x24); break;                          // mov [rsp], r8
                case 5: write(0x4C, 0x89, 0x0C, 0x24); break;                          // mov [rsp], r9
            }
            (*int_reg)++;
            return;
        }
    }
    //         we skip values pushed by 'call' and 'push rbx' v
    write(0x48, 0x8B, 0x83); writev(uint32_t, ((*stack_ptr) + 2) * 8);                 // mov rax, [rbx]+n
    write(0x48, 0x89, 0x04, 0x24);                                                     // mov [rsp], rax
    (*stack_ptr)++;
#undef _arr
}

static void* pawscript_generate_function_trampoline(PawScriptContext* context, Type* type, size_t* trampoline_size, size_t* funcptr_offset) {
#define _arr &assembly
    char* assembly = dynarr_create();
    size_t size;
    char* data = pawscript_serialize_type(type, &size);
    write(0x53);                                                                       // push rbx
    write(0x48, 0x89, 0xE3);                                                           // mov rbx, rsp
    int int_reg = 0, flt_reg = 0, stack_ptr = 0, args = 0;
    for (size_t i = 0; i < type->function_info.num_args; i++) {
        if (type->function_info.arg_types[i]->kind == TYPE_VARARGS) {
            pawscript_generate_args_assembly(&assembly, &int_reg, &flt_reg, &stack_ptr, false);
            pawscript_generate_args_assembly(&assembly, &int_reg, &flt_reg, &stack_ptr, false);
            args++;
        }
        else pawscript_generate_args_assembly(&assembly, &int_reg, &flt_reg, &stack_ptr,
            type->function_info.arg_types[i]->kind == TYPE_32FLT ||
            type->function_info.arg_types[i]->kind == TYPE_64FLT
        );
        args++;
    }
    if (args % 2 == 1) write(0x48, 0x83, 0xEC, 0x08);                                  // sub rsp, 8 (fix stack misalignment)
    write(0x48, 0x81, 0xEC); writev(uint32_t, size);                                   // sub rsp, size
    for (size_t i = 0; i < size; i++) {
        write(0xC6, 0x84, 0x24); writev(uint32_t, i); write(data[i]);                  // movb [rsp]+i, data[i]
    }
    write(0x48, 0xB8); writev(void*, pawscript_call_driver);                           // mov rax, pawscript_call_driver
    write(0x48, 0xBF); writev(void*, context);                                         // mov rdi, context
    write(0x48, 0x89, 0xE6);                                                           // mov rsi, rsp
    write(0x48, 0x89, 0xDA);                                                           // mov rdx, rbx
    write(0x48, 0xB9); *funcptr_offset = dynarr_length(assembly); writev(uint64_t, 0); // mov rcx, function
    if (size % 16 != 0) write(0x48, 0x83, 0xEC, 16 - size % 16);                       // mov rsp, x (fix stack alignment, again)
    write(0xFF, 0xD0);                                                                 // call rax
    write(0x66, 0x48, 0x0F, 0x6E, 0xC0);                                               // movq xmm0, rax
    write(0x48, 0x89, 0xDC);                                                           // mov rsp, rbx
    write(0x5B);                                                                       // pop rbx
    write(0xC3);                                                                       // ret
    *trampoline_size = dynarr_length(assembly);
    return dynarr_regular(assembly);
#undef _arr
#undef write
#undef writev
#undef writes
}

static Function* pawscript_create_function(PawScriptContext* context, Type* type, Token** tokens, size_t num_tokens, size_t* i) {
    size_t trampoline_size, funcptr_offset;
    void* trampoline = pawscript_generate_function_trampoline(context, type, &trampoline_size, &funcptr_offset);
    Function* func = pawscript_allocate(context->scope, sizeof(Function) + trampoline_size, true, false);
    uint32_t jmp_bytes = sizeof(Function) - 5;
    memcpy(func->jmp + 1, &jmp_bytes, 4);
    func->jmp[0] = 0xE9;
    func->start = tokens;
    func->num_tokens = num_tokens;
    memcpy((char*)trampoline + funcptr_offset, &func, sizeof(Function*));
    memcpy(func + 1, trampoline, trampoline_size);
    free(trampoline);
    return func;
}

static Variable* pawscript_handle_pointer_arithmetic(PawScriptContext* context, Token* token, Variable* left, Variable* right, bool negative) {
    Variable* ptr = NULL;
    Variable* off = NULL;
    if (pawscript_matches_value_type(Pointer, left)) {
        if (left->type->kind == TYPE_FUNCTION) return ERROR("Cannot perform pointer arithmetic on a function");
        if (!pawscript_matches_value_type(Integer, right)) return ERROR("Cannot find matching operator");
        ptr = left;
        off = right;
    }
    else if (pawscript_matches_value_type(Pointer, right)) {
        if (right->type->kind == TYPE_FUNCTION) return ERROR("Cannot perform pointer arithmetic on a function");
        if (!pawscript_matches_value_type(Integer, left)) return ERROR("Cannot find matching operator");
        ptr = right;
        off = left;
    }
    if (ptr) {
        Variable* out = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, ptr->type, NULL));
        pawscript_make_native(out->type, ptr->type->is_native);
        uint64_t value = 0;
        pawscript_get_integer(off, &value);
        *(void**)out->address = *(char**)ptr->address + pawscript_sizeof(ptr->type->pointer_info.base) * value * (negative ? -1 : 1);
        return out;
    }
    return NULL;
}

static Variable* pawscript_arithmetic_promotion(PawScriptContext* context, Variable** left, Variable** right) {
    Type* target = pawscript_create_type(TYPE_32BIT);
    if (
        ((* left)->type->kind == TYPE_32BIT && (* left)->type->is_unsigned) ||
        ((*right)->type->kind == TYPE_32BIT && (*right)->type->is_unsigned)
    ) target->is_unsigned = true;
    if ((*left)->type->kind == TYPE_64BIT || (*right)->type->kind == TYPE_64BIT) {
        target->kind = TYPE_64BIT;
        target->is_unsigned = (*left)->type->is_unsigned || (*right)->type->is_unsigned;
    }
    if (
        (*left)->type->kind == TYPE_POINTER  || (*right)->type->kind == TYPE_POINTER ||
        (*left)->type->kind == TYPE_FUNCTION || (*right)->type->kind == TYPE_FUNCTION
    ) {
        target->kind = TYPE_64BIT;
        target->is_unsigned = true;
    }
    if ((*left)->type->kind == TYPE_32FLT || (*right)->type->kind == TYPE_32FLT) target->kind = TYPE_32FLT;
    if ((*left)->type->kind == TYPE_64FLT || (*right)->type->kind == TYPE_64FLT) target->kind = TYPE_64FLT;
    if (target->kind == TYPE_32BIT || target->kind == TYPE_64BIT) target->is_unsigned = false;
    *left  = pawscript_cast(context, target, *left, false);
    *right = pawscript_cast(context, target, *right, false);
    Variable* out = pawscript_create_variable(context, target, NULL);
    pawscript_destroy_type(target);
    return out;
}

static Variable* pawscript_assign(PawScriptContext* context, Token* token, Variable* left, Variable* right) {
    if (left->type->is_const) return ERROR("Cannot mutate a constant");
    pawscript_make_native(left->type->orig, right->type->is_native);
    Variable* casted = pawscript_rvalue_to_lvalue(pawscript_cast(context, left->type, right, false));
    memcpy(left->address, casted->address, pawscript_sizeof(casted->type));
    return casted;
}

#define VERIFY(valtype) if (!pawscript_matches_value_type(valtype, left) || !pawscript_matches_value_type(valtype, right)) return ERROR("Cannot find matching operator")
#define OPERATOR(name) static Variable* pawscript_##name(PawScriptContext* context, Token* token, Variable* left, Variable* right)
#define OPERATOR_ASSIGNMENT(name) \
OPERATOR(name); \
static Variable* pawscript_##name##_assign(PawScriptContext* context, Token* token, Variable* left, Variable* right) { \
    Variable* result = pawscript_##name(context, token, left, right); \
    if (!result) return NULL; \
    Variable* out = pawscript_assign(context, token, left, result); \
    if (!out) return NULL; \
    pawscript_destroy_variable(context, result); \
    return out; \
} \
OPERATOR(name)

#define ARITH(op) ARITH2(op, op)
#define ARITH2(int_op, flt_op) \
    out = pawscript_arithmetic_promotion(context, &left, &right); \
    if (pawscript_matches_value_type(Integer, out)) { \
        uint64_t a, b, o; \
        pawscript_get_integer(left,  &a); \
        pawscript_get_integer(right, &b); \
        o = int_op(a, b); \
        memcpy(out->address, &o, pawscript_sizeof(out->type)); \
    } \
    else { \
        double a, b, o; \
        if (out->type->kind == TYPE_32FLT) { \
            a = *(float*) left->address; \
            b = *(float*)right->address; \
            o = flt_op(a, b); \
            *(float*)out->address = o; \
        } \
        else { \
            a = *(double*) left->address; \
            b = *(double*)right->address; \
            o = flt_op(a, b); \
            *(double*)out->address = o; \
        } \
    } \
    pawscript_destroy_variable(context, left); \
    pawscript_destroy_variable(context, right); \
    return out

#define BINARY(op) \
    uint64_t a, b; \
    pawscript_get_integer(left,  &a); \
    pawscript_get_integer(right, &b); \
    Variable* out = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, left->type, NULL)); \
    a op##= b; \
    memcpy(out->address, &a, pawscript_sizeof(left->type)); \
    return out

#define LOGIC(op) \
    Type* type = pawscript_create_type(TYPE_8BIT); \
    type->is_unsigned = true; \
    bool  left_unsigned =  left->type->is_unsigned; \
    bool right_unsigned = right->type->is_unsigned; \
    Variable* out = pawscript_create_variable(context, type, NULL); \
    pawscript_destroy_type(type); \
    pawscript_destroy_variable(context, pawscript_arithmetic_promotion(context, &left, &right)); \
    if (pawscript_matches_value_type(Integer, out)) { \
        uint64_t a, b; \
        pawscript_get_integer(left,  &a); \
        pawscript_get_integer(right, &b); \
        bool neg_left  =  !left_unsigned && (a >> 63) & 1; \
        bool neg_right = !right_unsigned && (b >> 63) & 1; \
        if      (neg_left != neg_right) *(uint8_t*)out->address = neg_right op neg_left; \
        else if (neg_left && neg_right) *(uint8_t*)out->address = b op a; \
        else *(uint8_t*)out->address = a op b; \
    } \
    else { \
        if (out->type->kind == TYPE_32BIT) *(uint8_t*)out->address = *(float*)left->address op *(float*)right->address; \
        else *(uint8_t*)out->address = *(double*)left->address op *(double*)right->address; \
    } \
    pawscript_destroy_variable(context, left); \
    pawscript_destroy_variable(context, right); \
    return out;

#define ADD(a, b) a + b
OPERATOR_ASSIGNMENT(add) {
    Variable* out = pawscript_handle_pointer_arithmetic(context, token, left, right, false);
    if (out) return out;
    if (pawscript_any_errors(context)) return FAILED;
    VERIFY(Number);
    ARITH(ADD);
}

#define SUBTRACT(a, b) a - b
OPERATOR_ASSIGNMENT(subtract) {
    Variable* out = pawscript_handle_pointer_arithmetic(context, token, left, right, true);
    if (out) return out;
    if (pawscript_any_errors(context)) return FAILED;
    VERIFY(Number);
    ARITH(SUBTRACT);
}

#define MULTIPLY(a, b) a * b
OPERATOR_ASSIGNMENT(multiply) {
    Variable* out;
    VERIFY(Number);
    ARITH(MULTIPLY);
}

#define DIVIDE(a, b) a / b
OPERATOR_ASSIGNMENT(divide) {
    Variable* out;
    VERIFY(Number);
    ARITH(DIVIDE);
}

#define MODULO(a, b) a % b
#define REMAINDER(a, b) remainder(a, b)
OPERATOR_ASSIGNMENT(modulo) {
    Variable* out;
    VERIFY(Number);
    ARITH2(MODULO, REMAINDER);
}

#define POWER(a, b) pow(a, b)
OPERATOR_ASSIGNMENT(power) {
    Variable* out;
    VERIFY(Number);
    ARITH(POWER);
}

OPERATOR_ASSIGNMENT(bitshift_left) {
    VERIFY(Integer);
    BINARY(<<);
}

OPERATOR_ASSIGNMENT(bitshift_right) {
    VERIFY(Integer);
    BINARY(>>);
}

OPERATOR_ASSIGNMENT(binary_and) {
    VERIFY(Integer);
    BINARY(&);
}

OPERATOR_ASSIGNMENT(binary_or) {
    VERIFY(Integer);
    BINARY(|);
}

OPERATOR_ASSIGNMENT(binary_xor) {
    VERIFY(Integer);
    BINARY(^);
}

OPERATOR(logic_and) {
    LOGIC(&&);
}

OPERATOR(logic_or) {
    LOGIC(||);
}

OPERATOR(logic_xor) {
    LOGIC(!=);
}

OPERATOR(equals) {
    LOGIC(==);
}

OPERATOR(less_than) {
    LOGIC(<);
}

OPERATOR(greater_than) {
    LOGIC(>);
}

OPERATOR(less_than_equals) {
    LOGIC(<=);
}

OPERATOR(greater_than_equals) {
    LOGIC(>=);
}

OPERATOR(promote_native) {
    pawscript_make_native(right->type->orig, true);
    Variable* out = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, right->type->orig, NULL));
    memcpy(out->address, right->address, pawscript_sizeof(out->type));
    return out;
}

OPERATOR(cast_native) {
    Variable* out = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, right->type, NULL));
    pawscript_make_native(out->type, true);
    memcpy(out->address, right->address, pawscript_sizeof(out->type));
    return out;
}

#define EVALUATE(var) \
    value = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, var->type, NULL)); \
    memcpy(value->address, var->address, pawscript_sizeof(value->type));

#define INCREMENT(var, op) \
    if (var->type->is_const) { \
        if (value) pawscript_destroy_variable(context, value); \
        return ERROR("Cannot mutate a constant"); \
    } \
    if (pawscript_matches_value_type(Integer, var)) { \
        uint64_t value; \
        pawscript_get_integer(var, &value); \
        value op##= op 1; \
        memcpy(var->address, &value, pawscript_sizeof(var->type)); \
    } \
    else if (pawscript_matches_value_type(Pointer, var)) { \
        if (var->type->kind == TYPE_FUNCTION) { \
            if (value) pawscript_destroy_variable(context, value); \
            return ERROR("Cannot perform pointer arithmetic on a function"); \
        } \
        *(char**)var->address op##= pawscript_sizeof(var->type->pointer_info.base); \
    } \
    else if (var->type->kind == TYPE_32BIT) *( float*)var->address op##= 1; \
    else if (var->type->kind == TYPE_64BIT) *(double*)var->address op##= 1;

OPERATOR(assign_increment) {
    Variable* value = NULL;
    EVALUATE(right);
    INCREMENT(right, +)
    return value;
}

OPERATOR(assign_decrement) {
    Variable* value = NULL;
    EVALUATE(right);
    INCREMENT(right, -);
    return value;
}

OPERATOR(increment_assign) {
    Variable* value = NULL;
    INCREMENT(left, +);
    EVALUATE(left);
    return value;
}

OPERATOR(decrement_assign) {
    Variable* value = NULL;
    INCREMENT(left, -);
    EVALUATE(left);
    return value;
}

OPERATOR(address_of) {
    Type* type = pawscript_create_type(TYPE_POINTER);
    type->pointer_info.base = pawscript_copy_type(left->type);
    Variable* out = pawscript_create_variable(context, type, NULL);
    pawscript_destroy_type(type);
    *(void**)out->address = left->address;
    return out;
}

OPERATOR(dereference) {
    if (!pawscript_can_dereference(context, left)) return ERROR("Invalid dereference of pointer %p", *(void**)left->address);
    if (
        left->type->pointer_info.base->kind == TYPE_STRUCT ||
        left->type->kind == TYPE_FUNCTION
    ) return ERROR("Cannot dereference a struct pointer or a function");
    return pawscript_lvalue_to_rvalue(pawscript_create_variable(context, left->type->pointer_info.base, *(void**)left->address));
}

OPERATOR(unary_plus) {
    Variable* out = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, left->type, NULL));
    memcpy(out->address, left->address, pawscript_sizeof(out->type));
    return out;
}

OPERATOR(arithmetic_negate) {
    Variable* out = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, left->type, NULL));
    if (pawscript_matches_value_type(Integer, left)) {
        uint64_t value;
        pawscript_get_integer(left, &value);
        value = -value;
        memcpy(out->address, &value, pawscript_sizeof(out->type));
    }
    else if (left->type->kind == TYPE_32FLT) *( float*)out->address = -*( float*)left->address;
    else if (left->type->kind == TYPE_64FLT) *(double*)out->address = -*(double*)left->address;
    return out;
}

OPERATOR(logic_negate) {
    Type* type = pawscript_create_type(TYPE_8BIT);
    type->is_unsigned = true;
    Variable* out = pawscript_create_variable(context, type, NULL);
    uint8_t value = !pawscript_is_truthy(left);
    memcpy(out->address, &value, pawscript_sizeof(out->type));
    return out;
}

OPERATOR(binary_negate) {
    Variable* out = pawscript_rvalue_to_lvalue(pawscript_create_variable(context, left->type, NULL));
    uint64_t value;
    pawscript_get_integer(left, &value);
    value = ~value;
    memcpy(out->address, &value, pawscript_sizeof(out->type));
    return out;
}

#undef ARITH
#undef ARITH2
#undef BINARY
#undef LOGIC
#undef ADD
#undef SUBTRACT
#undef MULTIPLY
#undef DIVIDE
#undef MODULO
#undef REMAINDER
#undef POWER
#undef EVALUATE
#undef INCREMENT
#undef OPERATOR
#undef OPERATOR_ASSIGNMENT

struct {
    TokenKind operator;
    ValueType left, right;
    Variable*(*evaluator)(PawScriptContext* context, Token* token, Variable* left, Variable* right);
} operator_table[] = {
    // unary suffix
    { TOKEN_DOUBLE_PLUS,          Null, Assignable, pawscript_assign_increment },
    { TOKEN_DOUBLE_MINUS,         Null, Assignable, pawscript_assign_decrement },
    { TOKEN_DOUBLE_QUESTION_MARK, Null, Assignable, pawscript_promote_native },
    { TOKEN_QUESTION_MARK,        Null, Any,        pawscript_cast_native },

    // unary prefix
    { TOKEN_DOUBLE_PLUS,      Assignable, Null, pawscript_increment_assign },
    { TOKEN_DOUBLE_MINUS,     Assignable, Null, pawscript_decrement_assign },
    { TOKEN_AMPERSAND,        Assignable, Null, pawscript_address_of },
    { TOKEN_ASTERISK,         Pointer,    Null, pawscript_dereference },
    { TOKEN_PLUS,             Number,     Null, pawscript_unary_plus },
    { TOKEN_MINUS,            Number,     Null, pawscript_arithmetic_negate },
    { TOKEN_EXCLAMATION_MARK, Any,        Null, pawscript_logic_negate },
    { TOKEN_TILDE,            Integer,    Null, pawscript_binary_negate },

    // assignments
    { TOKEN_EQUALS,                     Assignable, Any,     pawscript_assign },
    { TOKEN_PLUS_EQUALS,                Assignable, Number,  pawscript_add_assign },
    { TOKEN_MINUS_EQUALS,               Assignable, Number,  pawscript_subtract_assign },
    { TOKEN_ASTERISK_EQUALS,            Assignable, Number,  pawscript_multiply_assign },
    { TOKEN_DOUBLE_ASTERISK_EQUALS,     Assignable, Number,  pawscript_power_assign },
    { TOKEN_SLASH_EQUALS,               Assignable, Number,  pawscript_divide_assign },
    { TOKEN_PERCENT_EQUALS,             Assignable, Number,  pawscript_modulo_assign },
    { TOKEN_DOUBLE_LESS_THAN_EQUALS,    Assignable, Integer, pawscript_bitshift_left_assign },
    { TOKEN_DOUBLE_GREATER_THAN_EQUALS, Assignable, Integer, pawscript_bitshift_right_assign },
    { TOKEN_AMPERSAND_EQUALS,           Assignable, Integer, pawscript_binary_and_assign },
    { TOKEN_PIPE_EQUALS,                Assignable, Integer, pawscript_binary_or_assign },
    { TOKEN_HAT_EQUALS,                 Assignable, Integer, pawscript_binary_xor_assign },

    // binary
    { TOKEN_PLUS,                Number,  Number,  pawscript_add },
    { TOKEN_PLUS,                Pointer, Integer, pawscript_add },
    { TOKEN_PLUS,                Integer, Pointer, pawscript_add },
    { TOKEN_MINUS,               Number,  Number,  pawscript_subtract },
    { TOKEN_MINUS,               Pointer, Integer, pawscript_subtract },
    { TOKEN_MINUS,               Integer, Pointer, pawscript_subtract },
    { TOKEN_ASTERISK,            Number,  Number,  pawscript_multiply },
    { TOKEN_DOUBLE_ASTERISK,     Number,  Number,  pawscript_power },
    { TOKEN_SLASH,               Number,  Number,  pawscript_divide },
    { TOKEN_PERCENT,             Number,  Number,  pawscript_modulo },
    { TOKEN_DOUBLE_LESS_THAN,    Integer, Integer, pawscript_bitshift_left },
    { TOKEN_DOUBLE_GREATER_THAN, Integer, Integer, pawscript_bitshift_right },
    { TOKEN_AMPERSAND,           Integer, Integer, pawscript_binary_and },
    { TOKEN_PIPE,                Integer, Integer, pawscript_binary_or },
    { TOKEN_HAT,                 Integer, Integer, pawscript_binary_xor },
    { TOKEN_DOUBLE_AMPERSAND,    Any,     Any,     pawscript_logic_and },
    { TOKEN_DOUBLE_PIPE,         Any,     Any,     pawscript_logic_or },
    { TOKEN_NOT_EQUALS,          Any,     Any,     pawscript_logic_xor },
    { TOKEN_DOUBLE_EQUALS,       Any,     Any,     pawscript_equals },
    { TOKEN_LESS_THAN,           Any,     Any,     pawscript_less_than },
    { TOKEN_GREATER_THAN,        Any,     Any,     pawscript_greater_than },
    { TOKEN_LESS_THAN_EQUALS,    Any,     Any,     pawscript_less_than_equals },
    { TOKEN_GREATER_THAN_EQUALS, Any,     Any,     pawscript_greater_than_equals },
};

static int pawscript_get_precedence(TokenKind op) {
    switch (op) {
        case TOKEN_EQUALS:
        case TOKEN_PLUS_EQUALS:
        case TOKEN_MINUS_EQUALS:
        case TOKEN_ASTERISK_EQUALS:
        case TOKEN_SLASH_EQUALS:
        case TOKEN_PERCENT_EQUALS:
        case TOKEN_DOUBLE_LESS_THAN_EQUALS:
        case TOKEN_DOUBLE_GREATER_THAN_EQUALS:
        case TOKEN_AMPERSAND_EQUALS:
        case TOKEN_HAT_EQUALS:
        case TOKEN_PIPE_EQUALS:
            return 1;
        case TOKEN_DOUBLE_PIPE:
            return 2;
        case TOKEN_DOUBLE_AMPERSAND:
            return 3;
        case TOKEN_PIPE:
            return 4;
        case TOKEN_HAT:
            return 5;
        case TOKEN_AMPERSAND:
            return 6;
        case TOKEN_DOUBLE_EQUALS:
        case TOKEN_NOT_EQUALS:
            return 7;
        case TOKEN_LESS_THAN:
        case TOKEN_GREATER_THAN:
        case TOKEN_LESS_THAN_EQUALS:
        case TOKEN_GREATER_THAN_EQUALS:
            return 8;
        case TOKEN_DOUBLE_LESS_THAN:
        case TOKEN_DOUBLE_GREATER_THAN:
            return 9;
        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return 10;
        case TOKEN_ASTERISK:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
            return 11;
        case TOKEN_DOUBLE_ASTERISK:
            return 12;
        default:
            return 0;
    }
}

static Associativity pawscript_get_associativity(int precedence) {
    if (precedence == 1) return RightToLeft;
    return LeftToRight;
}

static Variable* pawscript_evaluate_operation(PawScriptContext* context, Variable* left, Token* token, Variable* right) {
    for (size_t i = 0; i < sizeof(operator_table) / sizeof(*operator_table); i++) {
        if (
            operator_table[i].operator != token->type ||
            !pawscript_matches_value_type(operator_table[i].left, left) ||
            !pawscript_matches_value_type(operator_table[i].right, right)
        ) continue;
        Variable* out = operator_table[i].evaluator(context, token, left, right);
        if (left)  pawscript_destroy_variable(context, left);
        if (right) pawscript_destroy_variable(context, right);
        return out;
    }
    return ERROR("Cannot find matching operator");
}

static ExprNode* pawscript_parse_expression(ExprItem* items, int count) {
#define REALLOC(stack) stack = realloc(stack, sizeof(*stack) * stack##_len)
#define PEEK(stack) stack[stack##_len - 1]
#define PUSH(stack, ...) { stack##_len++; REALLOC(stack); PEEK(stack) = __VA_ARGS__; }
#define POP(stack) ({ typeof(*stack) value = PEEK(stack); stack##_len--; REALLOC(stack); value; })
    ExprNode** node_stack = NULL;
    Token** op_stack = NULL;
    int node_stack_len = 0, op_stack_len = 0;
    for (int i = 0; i < count; ++i) {
        if (i % 2 == 1) { // every odd index is an operator
            int precedence = pawscript_get_precedence(items[i].operator->type);
            Associativity assoc = pawscript_get_associativity(precedence);
            while (op_stack_len != 0) {
                int top_precedence = pawscript_get_precedence(PEEK(op_stack)->type);
                if (!(
                    (assoc == LeftToRight && top_precedence >= precedence) ||
                    (assoc == RightToLeft && top_precedence >  precedence)
                )) break;
                ExprNode* node = malloc(sizeof(ExprNode));
                node->is_leaf = false;
                node->branch.operator = POP(op_stack);
                node->branch.b = POP(node_stack);
                node->branch.a = POP(node_stack);
                PUSH(node_stack, node);
            }
            PUSH(op_stack, items[i].operator);
        }
        else {
            ExprNode* node = malloc(sizeof(ExprNode));
            node->is_leaf = true;
            node->leaf.tokens = items[i].operand.tokens;
            node->leaf.num_tokens = items[i].operand.num_tokens;
            PUSH(node_stack, node);
        }
    }
    while (op_stack_len != 0) {
        ExprNode* node = malloc(sizeof(ExprNode));
        node->is_leaf = false;
        node->branch.operator = POP(op_stack);
        node->branch.b = POP(node_stack);
        node->branch.a = POP(node_stack);
        PUSH(node_stack, node);
    }
    ExprNode* node = POP(node_stack);
    free(node_stack);
    free(op_stack);
    return node;
}

static Variable* pawscript_evaluate_tokens(PawScriptContext* context, Token** tokens, size_t num_tokens) {
    size_t _i = 0; size_t* i = &_i;
    Token* token = CURR;
    Token** unary_stack = NULL;
    int unary_stack_len = 0;
    while (
        token->type == TOKEN_PLUS ||
        token->type == TOKEN_MINUS ||
        token->type == TOKEN_DOUBLE_PLUS ||
        token->type == TOKEN_DOUBLE_MINUS ||
        token->type == TOKEN_AMPERSAND ||
        token->type == TOKEN_ASTERISK ||
        token->type == TOKEN_DOUBLE_ASTERISK ||
        token->type == TOKEN_EXCLAMATION_MARK ||
        token->type == TOKEN_TILDE
    ) {
        // edge case: 2 consecutive dereferences get lexed as a power operator
        // we push TOKEN_DOUBLE_ASTERISK twice and treat it as TOKEN_ASTERISK afterwards
        if (token->type == TOKEN_DOUBLE_ASTERISK) PUSH(unary_stack, token);
        PUSH(unary_stack, token);
        token = NEXT;
    }
    Variable* variable = NULL;
    if (token->type == TOKEN_INTEGER) {
        uint64_t num = token->value.integer;
        Type* type;
        if      (num < 2147483648UL)          { type = pawscript_create_type(TYPE_32BIT); }
        else if (num < 9223372036854775808UL) { type = pawscript_create_type(TYPE_64BIT); }
        else {
            type = pawscript_create_type(TYPE_64BIT);
            type->is_unsigned = true;
        }
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &num, pawscript_sizeof(type));
        pawscript_destroy_type(type);
        token = NEXT;
    }
    else if (token->type == TOKEN_FLOAT) {
        Type* type = pawscript_create_type(TYPE_64FLT);
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &token->value.floating, pawscript_sizeof(type));
        pawscript_destroy_type(type);
        token = NEXT;
    }
    else if (token->type == TOKEN_STRING) {
        Type* type = pawscript_create_type(TYPE_POINTER);
        type->pointer_info.base = pawscript_create_type(TYPE_8BIT);
        type->pointer_info.base->is_const = true;
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &token->value.string, pawscript_sizeof(type));
        pawscript_destroy_type(type);
        token = NEXT;
    }
    else if (token->type == TOKEN_true) {
        uint8_t value = 1;
        Type* type = pawscript_create_type(TYPE_8BIT);
        type->is_unsigned = true;
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &value, pawscript_sizeof(type));
        pawscript_destroy_type(type);
        token = NEXT;
    }
    else if (token->type == TOKEN_false) {
        uint8_t value = 0;
        Type* type = pawscript_create_type(TYPE_8BIT);
        type->is_unsigned = true;
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &value, pawscript_sizeof(type));
        pawscript_destroy_type(type);
        token = NEXT;
    }
    else if (token->type == TOKEN_null) {
        void* value = NULL;
        Type* type = pawscript_create_type(TYPE_POINTER);
        type->pointer_info.base = pawscript_create_type(TYPE_VOID);
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &value, pawscript_sizeof(type));
        pawscript_destroy_type(type);
        token = NEXT;
    }
    else if (token->type == TOKEN_IDENTIFIER) {
        Variable* temp = pawscript_find_variable(context, token->value.string, NULL);
        if (!temp) return ERROR("Undefined variable '%s'", token->value.string);
        variable = pawscript_create_variable(context, temp->type, temp->address);
        token = NEXT;
    }
    else if (token->type == TOKEN_this) {
        Variable* this = pawscript_find_variable(context, "this", NULL);
        if (!this) return ERROR("Undefined variable 'this'");
        variable = pawscript_create_variable(context, this->type, this->address);
        token = NEXT;
    }
    else if (token->type == TOKEN_TRIPLE_DOT) {
        Variable* variable = pawscript_find_variable(context, "...", NULL);
        if (!variable) return ERROR("Function doesn't take any variadic arguments");
        token = NEXT;
        if (token->type != TOKEN_BRACKET_OPEN) return ERROR("Expected '['");
        token = NEXT;
        Variable* index = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!index) return FAILED;
        uint64_t value;
        if (!pawscript_get_integer(index, &value)) {
            pawscript_destroy_variable(context, index);
            return ERROR("Expression must return an integer");
        }
        pawscript_destroy_variable(context, index);
        PawScriptVarargs* varargs = variable->address;
        if (value >= varargs->count) return ERROR("Index out of bounds");
        token = CURR;
        if (token->type != TOKEN_BRACKET_CLOSE) return ERROR("Expected ']'");
        token = NEXT;
        return pawscript_cast(context, varargs->args[value]->type, varargs->args[value], false);
    }
    else if (token->type == TOKEN_PARENTHESIS_OPEN) {
        token = NEXT;
        variable = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!variable) return FAILED;
        token = CURR;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;
    }
    else if (token->type == TOKEN_cast || token->type == TOKEN_bitcast) {
        bool bitcast = token->type == TOKEN_bitcast;
        token = NEXT;
        if (token->type != TOKEN_LESS_THAN) return ERROR("Expected '<'");
        token = NEXT;
        Type* target_type = pawscript_parse_type(context, tokens, num_tokens, i);
        if (!target_type) {
            if (pawscript_any_errors(context)) return FAILED;
            if (token->type == TOKEN_IDENTIFIER) return ERROR("Undefined type '%s'", token->value.string);
            return ERROR("Expected type");
        }
        token = CURR;
        if (token->type != TOKEN_GREATER_THAN) return ERROR("Expected '>'");
        token = NEXT;
        if (token->type != TOKEN_PARENTHESIS_OPEN) return ERROR("Expected '('");
        token = NEXT;
        Variable* input = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!input) return FAILED;
        token = CURR;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;
        variable = pawscript_cast(context, target_type, input, bitcast);
        pawscript_destroy_type(target_type);
        pawscript_destroy_variable(context, input);
    }
    else if (token->type == TOKEN_sizeof) {
        token = NEXT;
        if (token->type != TOKEN_PARENTHESIS_OPEN) return ERROR("Expected '('");
        token = NEXT;
        size_t size = 0;
        Type* type = pawscript_parse_type(context, tokens, num_tokens, i);
        if (!type) {
            if (token->type == TOKEN_IDENTIFIER) {
                Variable* variable = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
                if (!variable) return FAILED;
                type = pawscript_copy_type(variable->type);
                pawscript_destroy_variable(context, variable);
            }
            else return ERROR("Expected expression or type");
        }
        size = pawscript_sizeof(type);
        pawscript_destroy_type(type);
        token = CURR;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;
        type = pawscript_create_type(TYPE_64BIT);
        type->is_unsigned = true;
        variable = pawscript_create_variable(context, type, NULL);
        *(uint64_t*)variable->address = size;
        pawscript_destroy_type(type);
    }
    else if (token->type == TOKEN_offsetof) {
        token = NEXT;
        if (token->type != TOKEN_LESS_THAN) return ERROR("Expected '<'");
        token = NEXT;
        Type* type = pawscript_parse_type(context, tokens, num_tokens, i);
        if (!type) {
            if (pawscript_any_errors(context)) return FAILED;
            if (token->type == TOKEN_IDENTIFIER) return ERROR("Undefined type '%s'", token->value.string);
            return ERROR("Expected type");
        }
        if (type->kind != TYPE_STRUCT) {
            pawscript_destroy_type(type);
            return ERROR("Type must specify a struct");
        }
        token = CURR;
        if (token->type != TOKEN_GREATER_THAN) {
            pawscript_destroy_type(type);
            return ERROR("Expected '>'");
        }
        token = NEXT;
        if (token->type != TOKEN_DOT) {
            pawscript_destroy_type(type);
            return ERROR("Expected '.'");
        }
        token = NEXT;
        if (token->type != TOKEN_IDENTIFIER) {
            pawscript_destroy_type(type);
            return ERROR("Expected identifier");
        }
        size_t offset = 0;
        bool found = false;
        for (size_t j = 0; j < type->struct_info.num_fields; j++) {
            if (strcmp(type->struct_info.fields[j]->name, token->value.string) == 0) {
                found = true;
                offset = type->struct_info.field_offsets[j];
                break;
            }
        }
        pawscript_destroy_type(type);
        if (!found) return ERROR("Unknown field name '%s'", token->value.string);
        type = pawscript_create_type(TYPE_64BIT);
        type->is_unsigned = true;
        variable = pawscript_create_variable(context, type, NULL);
        *(uint64_t*)variable->address = offset;
        pawscript_destroy_type(type);
        token = NEXT;
    }
    else if (token->type == TOKEN_scopeof) {
        token = NEXT;
        if (token->type != TOKEN_PARENTHESIS_OPEN) return ERROR("Expected '('");
        token = NEXT;
        PawScriptScope* scope = NULL;
        if (token->type == TOKEN_this) scope = context->scope;
        else if (token->type == TOKEN_IDENTIFIER) {
            Variable* var = pawscript_find_variable(context, token->value.string, &scope);
            if (!var) return ERROR("Undefined variable '%s'", token->value.string);
        }
        else return ERROR("Expected identifier or 'this'");
        int scope_id = 0;
        while (scope && scope->parent) {
            scope_id++;
            scope = scope->parent;
        }
        token = NEXT;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;
        Type* type = pawscript_create_type(TYPE_32BIT);
        variable = pawscript_create_variable(context, type, NULL);
        pawscript_destroy_type(type);
        *(int*)variable->address = scope_id;
    }
    else if (token->type == TOKEN_infoof) {
        token = NEXT;
        if (token->type != TOKEN_PARENTHESIS_OPEN) return ERROR("Expected '('");
        token = NEXT;
        struct {
            void* pointer;
            uint64_t bytes;
            uint64_t length;
            int32_t scope;
            bool is_valid;
        } alloc_info;
        memset(&alloc_info, 0, sizeof(alloc_info));
        Variable* var = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!var) return FAILED;
        if (var->type->kind != TYPE_POINTER && var->type->kind != TYPE_FUNCTION) {
            pawscript_destroy_variable(context, var);
            return ERROR("Expression must return a pointer");
        }
        PawScriptAllocation* alloc = pawscript_find_allocation(context, *(void**)var->address, 1);
        if (alloc) {
            PawScriptScope* scope = pawscript_find_allocation_scope(context, *(void**)var->address);
            alloc_info.is_valid = true;
            alloc_info.pointer = alloc->ptr;
            alloc_info.bytes = alloc->size;
            alloc_info.length = alloc->size / pawscript_sizeof(var->type->pointer_info.base);
            while (scope->parent) {
                alloc_info.scope++;
                scope = scope->parent;
            }
        }
        pawscript_destroy_variable(context, var);
        token = CURR;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;
        Type* type = pawscript_create_type(TYPE_POINTER);
        Type* info = pawscript_create_type(TYPE_STRUCT);
        Type* pointer_type = pawscript_create_type(TYPE_POINTER);
        Type* void_type = pawscript_create_type(TYPE_VOID);
        Type* bytes_type = pawscript_create_type(TYPE_64BIT);
        Type* length_type = pawscript_create_type(TYPE_64BIT);
        Type* scope_type = pawscript_create_type(TYPE_32BIT);
        Type* is_valid_type = pawscript_create_type(TYPE_8BIT);
        pointer_type->pointer_info.base = void_type;
        bytes_type->is_unsigned = true;
        length_type->is_unsigned = true;
        is_valid_type->is_unsigned = true;
        pointer_type->name = strdup("pointer");
        bytes_type->name = strdup("bytes");
        length_type->name = strdup("length");
        scope_type->name = strdup("scope");
        is_valid_type->name = strdup("is_valid");
        info->struct_info.num_fields = 5;
        info->struct_info.fields = malloc(sizeof(void*) * info->struct_info.num_fields);
        info->struct_info.field_offsets = malloc(sizeof(void*) * info->struct_info.num_fields);
        info->struct_info.fields[0] = pointer_type;  info->struct_info.field_offsets[0] = 0;
        info->struct_info.fields[1] = bytes_type;    info->struct_info.field_offsets[1] = 8;
        info->struct_info.fields[2] = length_type;   info->struct_info.field_offsets[2] = 16;
        info->struct_info.fields[3] = scope_type;    info->struct_info.field_offsets[3] = 24;
        info->struct_info.fields[4] = is_valid_type; info->struct_info.field_offsets[4] = 28;
        type->pointer_info.base = info;
        variable = pawscript_create_variable(context, type, NULL);
        *(void**)variable->address = pawscript_allocate(context->scope, sizeof(alloc_info), false, false);
        memcpy(*(void**)variable->address, &alloc_info, sizeof(alloc_info));
        pawscript_destroy_type(type);
    }
    else if (token->type == TOKEN_new) {
#undef FAILED
#define FAILED ({ if (target_type) pawscript_destroy_type(target_type); for (size_t j = 0; j < num_variables; j++) pawscript_destroy_variable(context, variables[j]); free(variables); NULL; })
        bool scoped = false;
        Type* target_type = NULL;
        uint64_t size = 0;
        Function* function = NULL;
        size_t num_variables = 0;
        Variable** variables = NULL;
        token = NEXT;
        if (token->type == TOKEN_scoped) {
            scoped = true;
            token = NEXT;
        }
        if (token->type == TOKEN_PARENTHESIS_OPEN) {
            token = NEXT;
            Variable* sizevar = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
            if (!sizevar) return FAILED;
            if (!pawscript_matches_value_type(Integer, sizevar)) {
                pawscript_destroy_variable(context, sizevar);
                return ERROR("Expression must return an integer");
            }
            memcpy(&size, sizevar->address, pawscript_sizeof(sizevar->type));
            pawscript_destroy_variable(context, sizevar);
            token = CURR;
            if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
            token = NEXT;
        }
        else if (token->type == TOKEN_LESS_THAN) {
            token = NEXT;
            target_type = pawscript_parse_type(context, tokens, num_tokens, i);
            if (!target_type) {
                if (pawscript_any_errors(context)) return FAILED;
                if (token->type == TOKEN_IDENTIFIER) return ERROR("Undefined type '%s'", token->value.string);
                return ERROR("Expected type");
            }
            size = pawscript_sizeof(target_type);
            token = CURR;
            if (token->type != TOKEN_GREATER_THAN) return ERROR("Expected '>'");
            token = NEXT;
            if (token->type == TOKEN_BRACE_OPEN) {
                if (target_type->kind != TYPE_FUNCTION) return ERROR("Cannot attach code to a non-function type");
                token = NEXT;
                Token** start = &tokens[*i];
                size_t start_index = (*i)--;
                if (!pawscript_scan_until(context, tokens, num_tokens, i, TOKEN_BRACE_CLOSE)) return FAILED;
                if (pawscript_any_errors(context)) return FAILED;
                token = CURR;
                function = pawscript_create_function(context, target_type, start, *i - start_index - 1, i);
                if (!function) return FAILED;
            }
            else if (token->type == TOKEN_PARENTHESIS_OPEN) {
                token = NEXT;
                bool unset = true;
                if (token->type != TOKEN_PARENTHESIS_CLOSE) {
                    unset = false;
                    Variable* sizevar = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
                    if (!sizevar) return FAILED;
                    uint64_t multiplier;
                    if (!pawscript_get_integer(sizevar, &multiplier)) {
                        pawscript_destroy_variable(context, sizevar);
                        return ERROR("Expression must return an integer");
                    }
                    pawscript_destroy_variable(context, sizevar);
                    token = CURR;
                    if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
                    size *= multiplier;
                }
                token = NEXT;
                if (token->type == TOKEN_BRACE_OPEN) {
                    token = NEXT;
                    while (true) {
                        if (token->type == TOKEN_BRACE_CLOSE) break;
                        Variable* entry = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
                        if (!entry) return FAILED;
                        num_variables++;
                        variables = realloc(variables, sizeof(Variable*) * num_variables);
                        variables[num_variables - 1] = entry;
                    }
                }
            }
        }
        else return ERROR("Expected '(' or '<'");
        if (!target_type) target_type = pawscript_create_type(TYPE_VOID);
        PawScriptScope* scope = context->scope;
        while (!scoped && scope->parent) scope = scope->parent;
        void* alloc = function ? function : pawscript_allocate(scope, size, false, false);
        if (function) pawscript_move_allocation(context, scope, function);
        else {
            Type* ptr = pawscript_create_type(TYPE_POINTER);
            ptr->pointer_info.base = target_type;
            target_type = ptr;
        }
        variable = pawscript_create_variable(context, target_type, NULL);
        pawscript_destroy_type(target_type);
        *(void**)variable->address = alloc;
#undef FAILED
#define FAILED NULL
    }
    else if (token->type == TOKEN_delete) {
        token = NEXT;
        if (token->type != TOKEN_PARENTHESIS_OPEN) return ERROR("Expected '('");
        token = NEXT;
        Variable* ptrvar = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!ptrvar) return FAILED;
        if (ptrvar->type->kind != TYPE_POINTER && ptrvar->type->kind != TYPE_FUNCTION) {
            pawscript_destroy_variable(context, ptrvar);
            return ERROR("Expression must return a pointer");
        }
        pawscript_free_allocation(context, *(void**)ptrvar->address, false);
        pawscript_destroy_variable(context, ptrvar);
        token = CURR;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;

        void* value = NULL;
        Type* type = pawscript_create_type(TYPE_POINTER);
        type->pointer_info.base = pawscript_create_type(TYPE_VOID);
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &value, pawscript_sizeof(type));
        pawscript_destroy_type(type);
    }
    else if (token->type == TOKEN_promote) {
        token = NEXT;
        size_t num_scopes = 1;
        bool global = false;
        if (token->type == TOKEN_global) {
            num_scopes = -1;
            global = true;
            token = NEXT;
        }
        else if (token->type == TOKEN_INTEGER) {
            num_scopes = token->value.integer;
            token = NEXT;
        }
        if (token->type != TOKEN_PARENTHESIS_OPEN) return ERROR("Expected '('");
        token = NEXT;
        Variable* ptrvar = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!ptrvar) return FAILED;
        if (ptrvar->type->kind != TYPE_POINTER && ptrvar->type->kind != TYPE_FUNCTION) {
            pawscript_destroy_variable(context, ptrvar);
            return ERROR("Expression must return a pointer");
        }
        void* value = *(void**)ptrvar->address;
        PawScriptScope* target = pawscript_find_allocation_scope(context, value);
        pawscript_destroy_variable(context, ptrvar);
        token = CURR;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;
        if (token->type == TOKEN_ARROW) {
            if (global) return ERROR("Cannot mix 'global' and '->'");
            token = NEXT;
            if (token->type != TOKEN_BRACKET_OPEN) return ERROR("Expected '['");
            token = NEXT;
            int64_t scope_id;
            Variable* scopevar = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
            if (!scopevar) return FAILED;
            if (!pawscript_get_integer(scopevar, (uint64_t*)&scope_id)) {
                pawscript_destroy_variable(context, scopevar);
                return ERROR("Expression must return an integer");
            }
            pawscript_destroy_variable(context, scopevar);
            token = CURR;
            if (token->type != TOKEN_BRACKET_CLOSE) return ERROR("Expected ']'");
            token = NEXT;
            int num_scopes = 0;
            target = context->scope;
            while (target->parent) {
                target = target->parent;
                num_scopes++;
            }
            if (scope_id > num_scopes) target = context->scope;
            else if (scope_id >= 0) {
                int counter = num_scopes - scope_id;
                target = context->scope;
                while (counter--) target = target->parent;
            }
        }
        else while (num_scopes && target->parent) {
            if (target->type == SCOPE_FUNCTION) {
                while (target->parent) target = target->parent;
                break;
            }
            target = target->parent;
            num_scopes--;
        }
        if (target) pawscript_move_allocation(context, target, value);

        Type* type = pawscript_create_type(TYPE_POINTER);
        type->pointer_info.base = pawscript_create_type(TYPE_VOID);
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &value, pawscript_sizeof(type));
        pawscript_destroy_type(type);
    }
    else if (token->type == TOKEN_adopt) {
        token = NEXT;
        if (token->type != TOKEN_PARENTHESIS_OPEN) return ERROR("Expected '('");
        token = NEXT;
        Variable* ptrvar = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!ptrvar) return FAILED;
        if (ptrvar->type->kind != TYPE_POINTER && ptrvar->type->kind != TYPE_FUNCTION) {
            pawscript_destroy_variable(context, ptrvar);
            return ERROR("Expression must return a pointer");
        }
        void* value = *(void**)ptrvar->address;
        pawscript_move_allocation(context, context->scope, value);
        pawscript_destroy_variable(context, ptrvar);
        token = CURR;
        if (token->type != TOKEN_PARENTHESIS_CLOSE) return ERROR("Expected ')'");
        token = NEXT;

        Type* type = pawscript_create_type(TYPE_POINTER);
        type->pointer_info.base = pawscript_create_type(TYPE_VOID);
        variable = pawscript_create_variable(context, type, NULL);
        memcpy(variable->address, &value, pawscript_sizeof(type));
        pawscript_destroy_type(type);
    }
    else if (token->type == TOKEN_if) {
        token = NEXT;
        Variable* condition = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
        if (!condition) return FAILED;
        bool truthy = pawscript_is_truthy(condition);
        pawscript_destroy_variable(context, condition);
        token = CURR;
        if (token->type != TOKEN_ARROW) return ERROR("Expected '->'");
        token = NEXT;
        if (token->type != TOKEN_BRACKET_OPEN) return ERROR("Expected '['");
        token = NEXT;

        if (!truthy) context->dry_run = true;
        Variable* value1 = pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
        if (!value1 && !context->dry_run) return FAILED;
        if (!context->dry_run) variable = value1;
        context->dry_run = false;

        token = CURR;
        if (token->type != TOKEN_SEMICOLON) return ERROR("Expected ';'");
        token = NEXT;

        if (truthy) context->dry_run = true;
        Variable* value2 = pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
        if (!value2 && !context->dry_run) return FAILED;
        if (!context->dry_run) variable = value2;
        context->dry_run = false;

        token = CURR;
        if (token->type != TOKEN_BRACKET_CLOSE) return ERROR("Expected ']'");
        token = NEXT;
    }
    else return ERROR("Expected expression");
#undef FAILED
#define FAILED ({ if (variable) pawscript_destroy_variable(context, variable); if (this) pawscript_destroy_variable(context, this); NULL; })
    Variable* this = NULL;
    while (true) {
        if (token->type == TOKEN_DOT) {
            if (variable->type->kind != TYPE_POINTER || variable->type->pointer_info.base->kind != TYPE_STRUCT) return ERROR("Expected pointer to struct");
            if (!pawscript_can_dereference(context, variable)) return ERROR("Invalid dereference of pointer %p", *(void**)variable->address);
            token = NEXT;
            if (token->type != TOKEN_IDENTIFIER) return ERROR("Identifier expected");
            size_t offset = 0;
            Type* field_type = NULL;
            Type* structure = variable->type->pointer_info.base;
            for (size_t i = 0; i < structure->struct_info.num_fields; i++) {
                if (strcmp(structure->struct_info.fields[i]->name, token->value.string) == 0) {
                    offset = structure->struct_info.field_offsets[i];
                    field_type = pawscript_copy_type(structure->struct_info.fields[i]);
                    break;
                }
            }
            if (!field_type) return ERROR("Struct doesn't have field '%s'", token->value.string);
            void* addr = *(char**)variable->address + offset;
            if (field_type->kind == TYPE_STRUCT) {
                pawscript_destroy_type(variable->type);
                variable->type = pawscript_create_type(TYPE_POINTER);
                variable->type->pointer_info.base = field_type;
                variable->type->is_native = field_type->is_native;
                *(void**)variable->address = addr;
            }
            else {
                if (field_type->kind == TYPE_FUNCTION) this = variable;
                else pawscript_destroy_variable(context, variable);
                variable = pawscript_create_variable(context, field_type, addr);
                pawscript_destroy_type(field_type);
            }
        }
        else if (
            token->type == TOKEN_DOUBLE_PLUS || token->type == TOKEN_DOUBLE_MINUS ||
            token->type == TOKEN_QUESTION_MARK || token->type == TOKEN_DOUBLE_QUESTION_MARK
        ) {
            variable = pawscript_evaluate_operation(context, NULL, token, variable);
        }
        else if (token->type == TOKEN_PARENTHESIS_OPEN) {
            if (variable->type->kind != TYPE_FUNCTION) return ERROR("Cannot call a non-function type");
            if (!pawscript_can_dereference(context, variable)) return ERROR("Invalid dereference of pointer %p", *(void**)variable->address);
            token = NEXT;
            FuncCall* call = pawscript_create_call(context);
            if (this) pawscript_this(call, this);
            this = NULL;
            if (token->type != TOKEN_PARENTHESIS_CLOSE) while (true) {
                if (token->type == TOKEN_TRIPLE_DOT) {
                    size_t prev = *i;
                    token = NEXT;
                    if (token->type == TOKEN_BRACKET_OPEN) *i = prev;
                    else {
                        if (
                            call->num_parameters >= variable->type->function_info.num_args ||
                            variable->type->function_info.arg_types[call->num_parameters]->kind != TYPE_VARARGS
                        ) {
                            pawscript_destroy_call(call);
                            return ERROR("Variadic argument forward not allowed at this position");
                        }
                        Variable* var = pawscript_find_variable(context, "...", NULL);
                        if (!var) {
                            pawscript_destroy_call(call);
                            return ERROR("Function doesn't take any variadic arguments");
                        }
                        PawScriptVarargs* varargs = var->address;
                        for (size_t j = 0; j < varargs->count; j++) {
                            Variable* copy = pawscript_create_variable(context, varargs->args[j]->type, NULL);
                            memcpy(copy->address, varargs->args[j]->address, pawscript_sizeof(copy->type));
                            pawscript_call_push(call, copy);
                        }
                        if (token->type != TOKEN_PARENTHESIS_CLOSE) {
                            pawscript_destroy_call(call);
                            return ERROR("Expected ')'");
                        }
                        break;
                    }
                }
                Variable* argument = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
                if (!argument) {
                    pawscript_destroy_call(call);
                    return FAILED;
                }
                pawscript_call_push(call, argument);
                token = CURR;
                if (token->type == TOKEN_COMMA) {
                    token = NEXT;
                    continue;
                }
                if (token->type == TOKEN_PARENTHESIS_CLOSE) break;
                pawscript_destroy_call(call);
                return ERROR("Expected ',' or ')'");
            }
            Variable* func = variable;
            variable = pawscript_call(call, context, token, func);
            pawscript_destroy_variable(context, func);
            pawscript_destroy_call(call);
        }
        else if (token->type == TOKEN_BRACKET_OPEN) {
            if (variable->type->kind != TYPE_POINTER) return ERROR("Cannot dereference a non-pointer type");
            if (!pawscript_can_dereference(context, variable)) return ERROR("Invalid dereference of pointer %p", *(void**)variable->address);
            token = NEXT;
            Variable* offset = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
            if (!offset) return FAILED;
            if (!pawscript_matches_value_type(Integer, offset)) {
                pawscript_destroy_variable(context, offset);
                return ERROR("Expression must return an integer");
            }
            token = CURR;
            if (token->type != TOKEN_BRACKET_CLOSE) return ERROR("Expected ']'");
            token->type = TOKEN_PLUS;
            variable = pawscript_evaluate_operation(context, variable, token, offset);
            token->type = TOKEN_ASTERISK;
            variable = pawscript_evaluate_operation(context, variable, token, NULL);
            token->type = TOKEN_BRACKET_CLOSE;
        }
        else break;
        token = NEXT;
    }
    while (unary_stack_len) {
        token = POP(unary_stack);
        bool power_token = token->type == TOKEN_DOUBLE_ASTERISK;
        if (power_token) token->type = TOKEN_ASTERISK;
        variable = pawscript_evaluate_operation(context, variable, token, NULL);
        if (!power_token) token->type = TOKEN_ASTERISK;
        if (!variable) return FAILED;
    }
    return variable;
#undef FAILED
}

static bool pawscript_scan_until(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i, TokenKind type) {
#define FAILED false
    Token* token = NEXT;
    char* stack = NULL;
    int stack_len = 0;
    while (*i < num_tokens) {
        if (stack_len == 0 && token->type == type) {
            token = NEXT;
            return true;
        }
        if (token->type == TOKEN_PARENTHESIS_OPEN) PUSH(stack, '(');
        if (token->type == TOKEN_BRACKET_OPEN)     PUSH(stack, '[');
        if (token->type == TOKEN_BRACE_OPEN)       PUSH(stack, '{');
        if (token->type == TOKEN_PARENTHESIS_CLOSE && (stack_len == 0 || POP(stack) != '(')) return ERROR("Unexpected ')'");
        if (token->type == TOKEN_BRACKET_CLOSE     && (stack_len == 0 || POP(stack) != '[')) return ERROR("Unexpected ']'");
        if (token->type == TOKEN_BRACE_CLOSE       && (stack_len == 0 || POP(stack) != '{')) return ERROR("Unexpected '}'");
        token = NEXT;
    }
    return ERROR("Unexpected end of expression");
#undef FAILED
}

static size_t pawscript_scan_operand(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i) {
#define FAILED -1
#define SCAN(type) { if (!pawscript_scan_until(context, tokens, num_tokens, i, type)) return FAILED; token = CURR; }
    size_t start = *i;
    Token* token = CURR;
    while (true) {
        bool is_unary =
            token->type == TOKEN_PLUS ||
            token->type == TOKEN_MINUS ||
            token->type == TOKEN_DOUBLE_PLUS ||
            token->type == TOKEN_DOUBLE_MINUS ||
            token->type == TOKEN_AMPERSAND ||
            token->type == TOKEN_ASTERISK ||
            token->type == TOKEN_EXCLAMATION_MARK ||
            token->type == TOKEN_TILDE;
        if (is_unary) {
            token = NEXT;
            continue;
        }
        break;
    }
    if (
        token->type == TOKEN_INTEGER ||
        token->type == TOKEN_FLOAT ||
        token->type == TOKEN_STRING ||
        token->type == TOKEN_IDENTIFIER ||
        token->type == TOKEN_false ||
        token->type == TOKEN_true ||
        token->type == TOKEN_null ||
        token->type == TOKEN_this
    ) token = NEXT;
    else if (token->type == TOKEN_PARENTHESIS_OPEN) {
        SCAN(TOKEN_PARENTHESIS_CLOSE)
    }
    else if (token->type == TOKEN_TRIPLE_DOT) {
        token = NEXT;
        if (token->type == TOKEN_BRACKET_OPEN) SCAN(TOKEN_BRACKET_CLOSE)
    }
    else if (token->type == TOKEN_new) {
        token = NEXT;
        if (token->type == TOKEN_scoped) token = NEXT;
        if (token->type == TOKEN_LESS_THAN)        SCAN(TOKEN_GREATER_THAN)
        if (token->type == TOKEN_PARENTHESIS_OPEN) SCAN(TOKEN_PARENTHESIS_CLOSE)
        if (token->type == TOKEN_BRACE_OPEN)       SCAN(TOKEN_BRACE_CLOSE)
    }
    else if (token->type == TOKEN_promote) {
        token = NEXT;
        if (token->type == TOKEN_INTEGER || token->type == TOKEN_global) token = NEXT;
        if (token->type == TOKEN_PARENTHESIS_OPEN) SCAN(TOKEN_PARENTHESIS_CLOSE)
        if (token->type == TOKEN_ARROW) token = NEXT;
        if (token->type == TOKEN_BRACKET_OPEN) SCAN(TOKEN_BRACKET_CLOSE)
    }
    else if (token->type == TOKEN_delete || token->type == TOKEN_adopt) {
        token = NEXT;
        if (token->type == TOKEN_PARENTHESIS_OPEN) SCAN(TOKEN_PARENTHESIS_CLOSE)
    }
    else if (token->type == TOKEN_cast || token->type == TOKEN_bitcast) {
        token = NEXT;
        if (token->type == TOKEN_LESS_THAN)  SCAN(TOKEN_GREATER_THAN)
        if (token->type == TOKEN_BRACE_OPEN) SCAN(TOKEN_BRACE_CLOSE)
    }
    else if (token->type == TOKEN_sizeof || token->type == TOKEN_scopeof || token->type == TOKEN_infoof) {
        token = NEXT;
        if (token->type == TOKEN_PARENTHESIS_OPEN) SCAN(TOKEN_PARENTHESIS_CLOSE)
    }
    else if (token->type == TOKEN_offsetof) {
        token = NEXT;
        if (token->type == TOKEN_LESS_THAN) SCAN(TOKEN_GREATER_THAN)
        if (token->type == TOKEN_DOT) token = NEXT;
        if (token->type == TOKEN_IDENTIFIER) token = NEXT;
    }
    else if (token->type == TOKEN_if) {
        token = NEXT;
        SCAN(TOKEN_ARROW)
        if (token->type == TOKEN_ARROW) token = NEXT;
        if (token->type == TOKEN_BRACKET_OPEN) SCAN(TOKEN_BRACKET_CLOSE)
    }
    while (true) {
        token = CURR;
        if (
            token->type == TOKEN_DOUBLE_PLUS || token->type == TOKEN_DOUBLE_MINUS ||
            token->type == TOKEN_QUESTION_MARK || token->type == TOKEN_DOUBLE_QUESTION_MARK
        ) token = NEXT;
        else if (token->type == TOKEN_DOT) {
            token = NEXT;
            if (token->type == TOKEN_IDENTIFIER) token = NEXT;
        }
        else if (token->type == TOKEN_BRACKET_OPEN)     SCAN(TOKEN_BRACKET_CLOSE)
        else if (token->type == TOKEN_PARENTHESIS_OPEN) SCAN(TOKEN_PARENTHESIS_CLOSE)
        else break;
    }
    return *i - start;
#undef SCAN
#undef FAILED
}

static Variable* pawscript_evaluate_exprtree(PawScriptContext* context, ExprNode* node) {
#define FAILED NULL
    if (node->is_leaf) return pawscript_evaluate_tokens(context, node->leaf.tokens, node->leaf.num_tokens);
    else {
        Variable* left = pawscript_evaluate_exprtree(context, node->branch.a);
        if (!left) return NULL;
        if (node->branch.operator->type == TOKEN_DOUBLE_AMPERSAND && !pawscript_is_truthy(left)) return left;
        if (node->branch.operator->type == TOKEN_DOUBLE_PIPE      &&  pawscript_is_truthy(left)) return left;
        Variable* right = pawscript_evaluate_exprtree(context, node->branch.b);
        if (!right) return NULL;
        return pawscript_evaluate_operation(context, left, node->branch.operator, right);
    }
}

static void pawscript_destroy_exprtree(ExprNode* node) {
    if (!node->is_leaf) {
        pawscript_destroy_exprtree(node->branch.a);
        pawscript_destroy_exprtree(node->branch.b);
    }
    free(node);
}

static Variable* pawscript_evaluate_expression(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i, bool no_void) {
    int items_len = 0;
    ExprItem* items = NULL;
    Token* token = CURR;
    Token* start = token;
    while (true) {
        Token** expr_start = &CURR;
        size_t expr_size = pawscript_scan_operand(context, tokens, num_tokens, i);
        if (expr_size == -1) return FAILED;
        PUSH(items, (ExprItem){ .operand = { .tokens = expr_start, .num_tokens = expr_size }});
        token = CURR;
        if (pawscript_get_precedence(token->type) != 0) PUSH(items, (ExprItem){ .operator = token })
        else break;
        token = NEXT;
    }
    if (context->dry_run) {
        free(items);
        return NULL;
    }
    ExprNode* expr = pawscript_parse_expression(items, items_len);
    free(items);
    Variable* out = pawscript_evaluate_exprtree(context, expr);
    pawscript_destroy_exprtree(expr);
    if (!out) return FAILED;
    if (out->type->kind == TYPE_VOID && no_void) {
        token = start;
        pawscript_destroy_variable(context, out);
        return ERROR("Expression cannot return void");
    }
    return out;
#undef REALLOC
#undef PEEK
#undef PUSH
#undef POP
}

static bool pawscript_run_codeblock(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i) {
    Token* token = CURR;
    bool result = true;
    size_t start = *i;
    if (token->type == TOKEN_BRACE_OPEN) {
        pawscript_push_scope(context);
        token = NEXT;
        int level = 0;
        while (true) {
            token = CURR;
            if (token->type == TOKEN_BRACE_CLOSE) {
                if (level == 0) break;
                level--;
            }
            if (context->dry_run) {
                if (token->type == TOKEN_BRACE_OPEN) level++;
                token = NEXT;
            }
            else {
                if (!pawscript_evaluate_statement(context, tokens, num_tokens, i)) return false;
                if (context->state != STATE_RUNNING) break;
            }
        }
        pawscript_pop_scope(context);
        token = NEXT;
        return result;
    }
    else if (token->type == TOKEN_ARROW) {
        pawscript_push_scope(context);
        token = NEXT;
        if (!pawscript_evaluate_statement(context, tokens, num_tokens, i)) result = false;
        pawscript_pop_scope(context);
        return result;
    }
    else if (token->type == TOKEN_SEMICOLON) {
        token = NEXT;
        return true;
    }
    return ERROR("Expected code block");
#undef FAILED
}

static bool pawscript_evaluate_statement(PawScriptContext* context, Token** tokens, size_t num_tokens, size_t* i) {
#define FAILED ({ if (type) pawscript_destroy_type(type); NULL; })
    Type* type = NULL;
    Token* token = CURR;
    bool is_extern = false, is_typedef = false;
    if (token->type == TOKEN_extern) {
        token = NEXT;
        is_extern = true;
    }
    else if (token->type == TOKEN_typedef) {
        token = NEXT;
        is_typedef = true;
    }
    type = pawscript_parse_type(context, tokens, num_tokens, i);
    if (pawscript_any_errors(context)) return false;
    if (type) {
        token = CURR;
        if (!type->name) {
            if (is_extern || is_typedef) return ERROR("Expected identifier");
            if (token->type != TOKEN_SEMICOLON) return ERROR("Expected identifier or ';'");
            pawscript_destroy_type(type);
            token = NEXT;
            return true;
        }
        if (!is_typedef) {
            if (type->kind == TYPE_VOID) return ERROR("Cannot declare void variable");
            if (type->kind == TYPE_STRUCT) return ERROR("Cannot declare non-pointer struct variable");
        }
        if (is_extern) pawscript_make_native(type, true);
        token = tokens[--(*i)];
        while (true) {
            if (token->type != TOKEN_IDENTIFIER) return ERROR("Expected identifier");
            free(type->name);
            type->name = strdup(token->value.string);
            if (is_typedef) {
                if (!pawscript_declare_type(context, type)) return ERROR("Identifier already defined");
                token = NEXT;
            }
            else {
                void* addr = is_extern ? pawscript_get_symbol(token->value.string) : NULL;
                if (is_extern) {
                    if (!addr) return ERROR("Cannot find symbol '%s' in native context", token->value.string);
                    if (!pawscript_is_symbol_allowed(context, addr)) return ERROR("The use of '%s' is disallowed", token->value.string);
                }
                Variable* var = pawscript_declare_variable(context, type, type->kind == TYPE_FUNCTION ? NULL : addr);
                if (!var) return ERROR("Identifier already defined");
                token = NEXT;
                if (!is_extern) {
                    if (token->type == TOKEN_EQUALS) {
                        Token* equals = token;
                        token = NEXT;
                        Variable* variable = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
                        if (!variable) return FAILED;
                        token = CURR;
                        bool is_const = var->type->is_const;
                        var->type->is_const = false;
                        pawscript_destroy_variable(context, pawscript_assign(context, equals, var, variable));
                        pawscript_destroy_variable(context, variable);
                        var->type->is_const = is_const;
                    }
                    else if (token->type == TOKEN_BRACE_OPEN) {
                        if (type->kind != TYPE_FUNCTION) return ERROR("Cannot attach code to a non-function type");
                        token = NEXT;
                        Token** start = &tokens[*i];
                        size_t start_index = (*i)--;
                        pawscript_scan_until(context, tokens, num_tokens, i, TOKEN_BRACE_CLOSE);
                        if (pawscript_any_errors(context)) return FAILED;
                        token = CURR;
                        *(void**)var->address = pawscript_create_function(context, var->type, start, *i - start_index - 1, i);
                        if (!*(void**)var->address) return FAILED;
                        pawscript_destroy_type(type);
                        return true;
                    }
                }
                else if (type->kind == TYPE_FUNCTION) *(void**)var->address = addr;
            }
            if (token->type == TOKEN_COMMA) {
                token = NEXT;
                continue;
            }
            if (token->type == TOKEN_SEMICOLON) {
                token = NEXT;
                pawscript_destroy_type(type);
                return true;
            }
            return ERROR("Expected ',' or ';'");
        }
    }
    else if (is_extern) return ERROR("Expected type");
    if (token->type == TOKEN_IDENTIFIER) {
        Variable* var = pawscript_find_variable(context, token->value.string, NULL);
        token = NEXT;
        if (token->type == TOKEN_BRACE_OPEN) {
            if (var->type->kind != TYPE_FUNCTION) return ERROR("Cannot attach code to a non-function type");
            token = NEXT;
            Token** start = &tokens[*i];
            size_t start_index = (*i)--;
            pawscript_scan_until(context, tokens, num_tokens, i, TOKEN_BRACE_CLOSE);
            if (pawscript_any_errors(context)) return FAILED;
            token = CURR;
            *(void**)var->address = pawscript_create_function(context, var->type, start, *i - start_index - 1, i);
            if (!*(void**)var->address) return FAILED;
            return true;
        }
        (*i)--;
        token = CURR;
    }
    if (token->type == TOKEN_if) {
        token = NEXT;
        bool prev_state = context->dry_run;
        while (true) {
            if (context->dry_run) {
                pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
                pawscript_run_codeblock(context, tokens, num_tokens, i);
            }
            else {
                Variable* expr = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
                if (!expr) return false;
                bool truthy = pawscript_is_truthy(expr);
                context->dry_run = !truthy;
                if (!pawscript_run_codeblock(context, tokens, num_tokens, i) && !context->dry_run) return false;
                context->dry_run ^= 1; // flip the value to skip all the elif and else blocks
                if (context->state != STATE_RUNNING) break;
            }
            token = CURR;
            if (token->type == TOKEN_elif) {
                token = NEXT;
                continue;
            }
            if (token->type == TOKEN_else) {
                token = NEXT;
                if (!pawscript_run_codeblock(context, tokens, num_tokens, i) && !context->dry_run) return false;
                if (context->state != STATE_RUNNING) break;
            }
            break;
        }
        context->dry_run = prev_state;
    }
    else if (token->type == TOKEN_while) {
        token = NEXT;
        if (context->dry_run) {
            pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
            pawscript_run_codeblock(context, tokens, num_tokens, i);
            return true;
        }
        size_t start = *i;
        while (true) {
            *i = start;
            Variable* expr = pawscript_evaluate_expression(context, tokens, num_tokens, i, true);
            if (!expr) return false;
            bool truthy = pawscript_is_truthy(expr);
            context->dry_run = !truthy;
            if (!pawscript_run_codeblock(context, tokens, num_tokens, i)) return false;
            context->dry_run = false;
            if (context->state == STATE_CONTINUE) pawscript_set_state(context, STATE_RUNNING, NULL);
            if (context->state == STATE_BREAK) {
                pawscript_set_state(context, STATE_RUNNING, NULL);
                break;
            }
            if (context->state == STATE_RETURN) break;
        }
        return true;
    }
    else if (token->type == TOKEN_for) {
        token = NEXT;
        TypeKind kind = TYPE_VOID;
        bool is_unsigned = false;
        switch (token->type) {
            case TOKEN_bool:
            case TOKEN_u8:  kind = TYPE_8BIT;  is_unsigned = true; break;
            case TOKEN_u16: kind = TYPE_16BIT; is_unsigned = true; break;
            case TOKEN_u32: kind = TYPE_32BIT; is_unsigned = true; break;
            case TOKEN_u64: kind = TYPE_64BIT; is_unsigned = true; break;
            case TOKEN_s8:  kind = TYPE_8BIT;                      break;
            case TOKEN_s16: kind = TYPE_16BIT;                     break;
            case TOKEN_s32: kind = TYPE_32BIT;                     break;
            case TOKEN_s64: kind = TYPE_64BIT;                     break;
            default: return ERROR("Expected non-const integer type");
        }
        token = NEXT;
        if (token->type != TOKEN_IDENTIFIER) return ERROR("Expected identifier");
        const char* name = token->value.string;
        token = NEXT;
        if (token->type != TOKEN_in) return ERROR("Expected 'in'");
        token = NEXT;
        bool left_exclusive, right_exclusive;
        if (token->type == TOKEN_PARENTHESIS_OPEN) left_exclusive = true;
        else if (token->type == TOKEN_BRACKET_OPEN) left_exclusive = false;
        else return ERROR("Expected '(' or '['");
        token = NEXT;
        Variable* left = pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
        if (!left && !context->dry_run) return FAILED;
        token = CURR;
        if (token->type != TOKEN_COMMA) {
            pawscript_destroy_variable(context, left);
            return ERROR("Expected ','");
        }
        token = NEXT;
        Variable* right = pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
        if (!right && !context->dry_run) {
            pawscript_destroy_variable(context, left);
            return FAILED;
        }
        token = CURR;
        if (token->type == TOKEN_PARENTHESIS_CLOSE) right_exclusive = true;
        else if (token->type == TOKEN_BRACKET_CLOSE) right_exclusive = false;
        else {
            pawscript_destroy_variable(context, left);
            pawscript_destroy_variable(context, right);
            return ERROR("Expected ')' or ']'");
        }
        token = NEXT;
        size_t start = *i;
        if (context->dry_run) {
            pawscript_run_codeblock(context, tokens, num_tokens, i);
            return true;
        }
        else {
            context->dry_run = true;
            pawscript_run_codeblock(context, tokens, num_tokens, i);
            context->dry_run = false;
        }
        size_t end = *i;
        Type* iter_type = pawscript_create_type(kind);
        iter_type->is_unsigned = is_unsigned;
        iter_type->name = strdup(name);
        Variable* casted_left  = pawscript_cast(context, iter_type, left,  false);
        Variable* casted_right = pawscript_cast(context, iter_type, right, false);
        pawscript_destroy_variable(context, left);
        pawscript_destroy_variable(context, right);
        left  = casted_left;
        right = casted_right;
        int direction = 0;
        Variable* comparsion;
        if      ((comparsion =    pawscript_less_than(context, token, left, right)) && pawscript_is_truthy(comparsion)) direction =  1;
        else if ((comparsion = pawscript_greater_than(context, token, left, right)) && pawscript_is_truthy(comparsion)) direction = -1;
        else direction = 0;
        pawscript_destroy_variable(context, comparsion);
        if (direction == 0 && (left_exclusive || right_exclusive)) {
            pawscript_destroy_variable(context, left);
            pawscript_destroy_variable(context, right);
            return true;
        }
        uint64_t from, to, iter;
        pawscript_get_integer(left, &from);
        pawscript_get_integer(right, &to);
        if (left_exclusive) from += direction;
        if (!right_exclusive) to += direction;
        pawscript_destroy_variable(context, left);
        pawscript_destroy_variable(context, right);
        iter = from;
        pawscript_push_scope(context);
        Variable* iter_var = pawscript_declare_variable(context, iter_type, NULL);
        memcpy(iter_var->address, &iter, pawscript_sizeof(iter_var->type));
        pawscript_destroy_type(iter_type);
        while (true) {
            if (iter == to) break;
            *i = start;
            if (!pawscript_run_codeblock(context, tokens, num_tokens, i)) return FAILED;
            if (context->state == STATE_CONTINUE) pawscript_set_state(context, STATE_RUNNING, NULL);
            if (context->state == STATE_BREAK) {
                pawscript_set_state(context, STATE_RUNNING, NULL);
                break;
            }
            if (context->state == STATE_RETURN) break;
            iter += direction;
            memcpy(iter_var->address, &iter, pawscript_sizeof(iter_var->type));
        }
        *i = end;
        pawscript_pop_scope(context);
        return true;
    }
    else if (token->type == TOKEN_continue) {
        token = NEXT;
        if (token->type != TOKEN_SEMICOLON) return ERROR("Expected ';'");
        token = NEXT;
        if (!context->dry_run) pawscript_set_state(context, STATE_CONTINUE, NULL);
    }
    else if (token->type == TOKEN_break) {
        token = NEXT;
        if (token->type != TOKEN_SEMICOLON) return ERROR("Expected ';'");
        token = NEXT;
        if (!context->dry_run) pawscript_set_state(context, STATE_BREAK, NULL);
    }
    else if (token->type == TOKEN_return) {
        token = NEXT;
        if (token->type == TOKEN_SEMICOLON) {
            token = NEXT;
            return true;
        }
        Variable* expr = pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
        if (!expr) return FAILED;
        if (expr->type->kind == TYPE_VOID) {
            pawscript_destroy_variable(context, expr);
            expr = NULL;
        }
        token = CURR;
        if (token->type != TOKEN_SEMICOLON) {
            if (expr) pawscript_destroy_variable(context, expr);
            return ERROR("Expected ';'");
        }
        token = NEXT;
        pawscript_set_state(context, STATE_RETURN, expr);
    }
    else if (token->type == TOKEN_include) {
        token = NEXT;
        if (token->type != TOKEN_STRING) return ERROR("Expected string literal");
        char* filename = pawscript_get_filename(token->filename, token->value.string);
        if (context->dry_run) {
            free(filename);
            return true;
        }

        PawScriptScope* scope = context->scope;
        PawScriptScope* global = scope;
        while (global->parent) global = global->parent;
        context->scope = global;
        bool result = pawscript_run_file_inner(context, filename, token);
        free(filename);
        context->scope = scope;
        token = NEXT;
        return result;
    }
    else if (token->type == TOKEN_BRACE_OPEN || token->type == TOKEN_ARROW || token->type == TOKEN_SEMICOLON)
        return pawscript_run_codeblock(context, tokens, num_tokens, i);
    else {
        Variable* variable = pawscript_evaluate_expression(context, tokens, num_tokens, i, false);
        if (!variable && !context->dry_run) return false;
        if (variable) pawscript_destroy_variable(context, variable);
        token = NEXT;
    }
    return true;
}

static bool pawscript_evaluate(PawScriptContext* context, Token** tokens, size_t num_tokens) {
    size_t i = 0;
    while (i + 1 < num_tokens) {
        if (!pawscript_evaluate_statement(context, tokens, num_tokens, &i)) return false;
        if (context->state != STATE_RUNNING) break;
    }
    return true;
#undef ERROR
#undef NEXT
#undef CURR
}

static void pawscript_add_builtin(PawScriptContext* context, const char* name, uint64_t value) {
    Type* type = pawscript_create_type(TYPE_64BIT);
    type->is_unsigned = true;
    type->is_const = true;
    type->name = strdup(name);
    Variable* var = pawscript_declare_variable(context, type, NULL);
    *(uint64_t*)var->address = value;
    pawscript_destroy_type(type);
}

PawScriptContext* pawscript_create_context() {
    PawScriptContext* context = calloc(sizeof(PawScriptContext), 1);
    pawscript_push_scope(context);
#if defined(__linux__)
    pawscript_add_builtin(context, "__builtin_PLATFORM", 0);
#elif defined(_WIN32)
    pawscript_add_builtin(context, "__builtin_PLATFORM", 1);
#elif defined(__APPLE__)
    pawscript_add_builtin(context, "__builtin_PLATFORM", 2);
#elif defined(__FreeBSD__)
    pawscript_add_builtin(context, "__builtin_PLATFORM", 3);
#elif defined(__OpenBSD__)
    pawscript_add_builtin(context, "__builtin_PLATFORM", 4);
#else
    pawscript_add_builtin(context, "__builtin_PLATFORM", 5);
#endif
    pawscript_add_builtin(context, "__builtin_SIGABRT",  SIGABRT);
    pawscript_add_builtin(context, "__builtin_SIGFPE",   SIGFPE);
    pawscript_add_builtin(context, "__builtin_SIGILL",   SIGILL);
    pawscript_add_builtin(context, "__builtin_SIGINT",   SIGINT);
    pawscript_add_builtin(context, "__builtin_SIGSEGV",  SIGSEGV);
    pawscript_add_builtin(context, "__builtin_SIGTERM",  SIGTERM);
    pawscript_add_builtin(context, "__builtin_stdin",    (uint64_t)stdin);
    pawscript_add_builtin(context, "__builtin_stdout",   (uint64_t)stdout);
    pawscript_add_builtin(context, "__builtin_stderr",   (uint64_t)stderr);
    pawscript_add_builtin(context, "__builtin_EOF",      EOF);
    pawscript_add_builtin(context, "__builtin_SEEK_SET", SEEK_SET);
    pawscript_add_builtin(context, "__builtin_SEEK_CUR", SEEK_CUR);
    pawscript_add_builtin(context, "__builtin_SEEK_END", SEEK_END);
    pawscript_add_builtin(context, "__builtin_errno",    errno);
    return context;
}

void pawscript_destroy_context(PawScriptContext* context) {
    while (context->scope) pawscript_pop_scope(context);
    pawscript_destroy_tokens(context->tokens, context->num_tokens);
    for (size_t i = 0; i < context->num_errors; i++) {
        free(context->errors[i]->msg);
        free(context->errors[i]);
    }
    free(context->errors);
    free(context);
}

bool pawscript_run(PawScriptContext* context, const char* code) {
    size_t num_tokens;
    Token** tokens = pawscript_lex(context, code, NULL, &num_tokens);
    bool result = pawscript_evaluate(context, tokens, num_tokens);
    pawscript_append_tokens(&context->tokens, &context->num_tokens, tokens, num_tokens);
    return result;
}

void pawscript_symbol_visibility(PawScriptContext* context, PawScriptVisibility visibility) {
    context->address_visibility = visibility;
}

void pawscript_register_symbol(PawScriptContext* context, void* symbol) {
    context->num_addresses++;
    context->addresses = realloc(context->addresses, sizeof(void*) * context->num_addresses);
    context->addresses[context->num_addresses - 1] = symbol;
}

bool pawscript_get(PawScriptContext* context, const char* name, void* out) {
    Variable* variable = pawscript_find_variable(context, name, NULL);
    if (!variable) return false;
    memcpy(out, variable->address, pawscript_sizeof(variable->type));
    return true;
}

bool pawscript_set(PawScriptContext* context, const char* name, void* in) {
    Variable* variable = pawscript_find_variable(context, name, NULL);
    if (!variable) return false;
    if (variable->type->is_const) return false;
    if (variable->type->kind == TYPE_FUNCTION) memcpy(variable->address, &in, 8);
    else memcpy(variable->address, in, pawscript_sizeof(variable->type));
    pawscript_make_native(variable->type, true);
    return true;
}

Type* pawscript_get_type(PawScriptContext* context, const char* name) {
    Variable* variable = pawscript_find_variable(context, name, NULL);
    if (!variable) return NULL;
    return pawscript_make_original(pawscript_copy_type(variable->type));
}
