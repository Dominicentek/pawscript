#ifndef PAWSCRIPT_H
#define PAWSCRIPT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct PawScriptContext PawScriptContext;
typedef struct PawScriptError PawScriptError;

typedef enum {
    PawScriptVarargs_End,
    PawScriptVarargs_Integer,
    PawScriptVarargs_FloatingPoint,
} PawScriptVarargsType;

typedef struct {
    PawScriptVarargsType type;
    union {
        uint64_t integer;
        double floating_point;
    };
} PawScriptVarargs;

#define PAWSCRIPT_VARARGS(...) (PawScriptVarargs[]){ __VA_ARGS__ __VA_OPT__(,) (PawScriptVarargs){ .type = PawScriptVarargs_End }}
#define PAWSCRIPT_VARARGS_INT(x) (PawScriptVarargs){ .type = PawScriptVarargs_Integer, .integer = (x) }
#define PAWSCRIPT_VARARGS_FLOAT(x) (PawScriptVarargs){ .type = PawScriptVarargs_FloatingPoint, .floating_point = (x) }

#define PAWSCRIPT_RESULT "@RESULT@"

#ifdef __cplusplus
extern "C" {
#endif

PawScriptContext* pawscript_create_context();
PawScriptError* pawscript_run(PawScriptContext* context, const char* code);
PawScriptError* pawscript_run_file(PawScriptContext* context, const char* filename);
bool pawscript_get(PawScriptContext* context, const char* name, void* ptr);
bool pawscript_set(PawScriptContext* context, const char* name, void* ptr);
bool pawscript_print_variable(PawScriptContext* context, FILE* f, const char* name);
void pawscript_log_error(PawScriptError* error, FILE* f);
void pawscript_destroy_error(PawScriptError* error);
void pawscript_destroy_context(PawScriptContext* context);

void on_segfault(void(*handler)(void* addr));

#ifdef __cplusplus
}
#endif

#endif
