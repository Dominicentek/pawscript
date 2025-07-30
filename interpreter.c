#include "pawscript.h"

#include <stdio.h>
#include <stdlib.h>

void report_errors(PawScriptContext* context) {
    if (pawscript_any_errors(context)) {
        PawScriptError* error;
        printf("Errors:\n");
        while (pawscript_error(context, &error)) {
            printf("(%s%s%d:%d) %s\n", error->file ? error->file : "", error->file ? " " : "", error->row, error->col, error->msg);
        }
    }
    else printf("No errors reported\n");
}

int main() {
    FILE* f = fopen("test.paw", "r");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = malloc(size + 1);
    fread(text, size, 1, f);
    fclose(f);

    PawScriptContext* context = pawscript_create_context();
    pawscript_run(context, text);
    report_errors(context);
    pawscript_destroy_context(context);

    free(text);
    return 0;
}
