#include "pawscript.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#define BUFFER_SIZE 8192

char* create_buffer() {
    return malloc(BUFFER_SIZE);
}

void buffer_append_byte(char** buffer, int* ptr, char c) {
    (*buffer)[(*ptr)++] = c;
    if (*ptr % BUFFER_SIZE == 0) *buffer = realloc(*buffer, *ptr + BUFFER_SIZE);
}

void buffer_append(char** buffer, int* ptr, char c) {
    buffer_append_byte(buffer, ptr, c);
    buffer_append_byte(buffer, ptr, 0);
    (*ptr)--;
}

void buffer_read_line(char** buffer, int* ptr) {
    char c;
    while ((c = getchar()) != EOF && c != '\n') {
        buffer_append_byte(buffer, ptr, c);
    }
    buffer_append_byte(buffer, ptr, '\n');
    buffer_append_byte(buffer, ptr, 0);
    (*ptr)--;
}

bool can_exec(char* code) {
    int depth = 0;
    char string_char = 0;
    bool backslash = false;
    bool valid_end = false;
    int ptr = 0;
    char c;
    while ((c = code[ptr++])) {
        valid_end = false;
        if (string_char) {
            if (!backslash && c == string_char) string_char = 0;
            backslash = c == '\\';
        }
        else {
            if (c == '"' || c == '\'') string_char = c;
            if (c == '(' || c == '[' || c == '{') depth++;
            if (c == ')' || c == ']' || c == '}') depth--;
        }
    }
    return depth <= 0 && string_char == 0;
}

int main(int argc, char** argv) {
    bool interactive = false;
    if (argc == 1) {
        printf("Paws - The PawScript interpreter\n");
        printf("Usage:\n");
        printf("-f <file>   execute a file\n");
        printf("-f -        run from stdin\n");
        printf("-i          interactive mode\n");
        printf("\n");
        printf("When using -i and -f at the same time,\nthe interpreter goes to interactive mode on exit.\n");
        printf("You can chain multiple -f's.\n");
        return 0;
    }
    PawScriptContext* context = pawscript_create_context();
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) interactive = true;
        else if (strcmp(argv[i], "-f") == 0) {
            i++;
            if (i == argc) {
                fprintf(stderr, "Expected file\n");
                return 1;
            }
            if (strcmp(argv[i], "-") == 0) {
                char* buf = create_buffer();
                int c, ptr = 0;
                while ((c = getchar()) != EOF) {
                    buffer_append(&buf, &ptr, c);
                }
                PawScriptError* error;
                if ((error = pawscript_run(context, buf))) pawscript_log_error(error, stderr);
                else {
                    printf("<stdin>: ");
                    pawscript_print_variable(context, stdout, PAWSCRIPT_RESULT);
                }
                free(buf);
            }
            else {
                PawScriptError* error;
                if ((error = pawscript_run_file(context, argv[i]))) pawscript_log_error(error, stderr);
                else {
                    printf("%s: ", argv[i]);
                    pawscript_print_variable(context, stdout, PAWSCRIPT_RESULT);
                }
            }
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    char* buf = create_buffer();
    int ptr = 0;
    while (interactive) {
        if (!isatty(STDIN_FILENO)) {
            fprintf(stderr, "stdin is not an interactive terminal. Skipping interactive shell.\n");
            return 1;
        }
        printf("%c ", ptr == 0 ? '>' : '+');
        buffer_read_line(&buf, &ptr);
        if (can_exec(buf)) {
            PawScriptError* error;
            if ((error = pawscript_run(context, buf))) pawscript_log_error(error, stderr);
            else pawscript_print_variable(context, stdout, PAWSCRIPT_RESULT);
            ptr = 0;
        }
    }
    free(buf);
    pawscript_destroy_context(context);
    return 0;
}
