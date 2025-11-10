#ifndef PAWSCRIPT_IMPLEMENTATION
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
#endif

#ifdef PAWSCRIPT_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/types.h>

#if _WIN32
#define PATH_SEPARATOR '\\'
#include <windows.h>
#define NUM_INT_REGS 4
#define NUM_FLT_REGS 4
#else
#define PATH_SEPARATOR '/'
#include <unistd.h>
#include <sys/mman.h>
#define NUM_INT_REGS 6
#define NUM_FLT_REGS 8
#endif

#define API extern "C"

typedef int(*Compare)(const void* a, const void* b);
typedef uint64_t(*HashFunc)(void* ptr);

// == COMPARATORS ==

int compare_strings(const void* a, const void* b) {
    return strcmp(*(char**)a, *(char**)b);
}

int compare_int8(const void* a, const void* b) {
    return *(int8_t*)b - *(int8_t*)a;
}

int compare_int16(const void* a, const void* b) {
    return *(int16_t*)b - *(int16_t*)a;
}

int compare_int32(const void* a, const void* b) {
    return *(int32_t*)b - *(int32_t*)a;
}

int compare_int64(const void* a, const void* b) {
    return *(int64_t*)b - *(int64_t*)a;
}

int compare_float32(const void* a, const void* b) {
    return (*(float*)a > *(float*)b) - (*(float*)a < *(float*)b);
}

int compare_float64(const void* a, const void* b) {
    return (*(double*)a > *(double*)b) - (*(double*)a < *(double*)b);
}

// == CLASS IMPLEMENTATIONS ==

template<typename T> struct Set {
    int size = 0, capacity = 4;
    T* items = (T*)malloc(sizeof(T) * capacity);
    Compare compare;
    Set(Compare compare): compare(compare) {}
    ~Set() { free(items); }
    T add(T item) {
        if (size == capacity) {
            capacity *= 2;
            items = (T*)realloc(items, sizeof(T) * capacity);
        }
        items[size++] = item;
        qsort(items, size, sizeof(T), compare);
        return item;
    }
    T* find(T item) {
        return (T*)bsearch(&item, items, size, sizeof(T), compare);
    }
    void remove(T item) {
        T* ptr = find(item);
        if (!ptr) return;
        int index = ((uintptr_t)ptr - (uintptr_t)items) / sizeof(T);
        size--;
        if (index != size) memmove(
            items + index, items + index + 1,
            sizeof(T) * (size - index)
        );
    }
};

struct Allocator {
    Set<void*> allocs = Set<void*>(compare_int64);
    bool watch_next = false;
    ~Allocator() {
        for (int i = 0; i < allocs.size; i++) std::free(allocs.items[i]);
    }
    template<typename T> T* malloc(size_t count = 1) {
        if (count == 0) return NULL;
        void* ptr = std::malloc(sizeof(T) * count);
        memset(ptr, 0, sizeof(T) * count);
        allocs.add(ptr);
        return (T*)ptr;
    }
    template<typename T> T* realloc(T* ptr, size_t count) {
        if (!allocs.find(ptr)) return NULL;
        allocs.remove(ptr);
        allocs.add(ptr = (T*)std::realloc(ptr, sizeof(T) * count));
        return (T*)ptr;
    }
    bool free(void* ptr) {
        if (!allocs.find(ptr)) return false;
        allocs.remove(ptr);
        std::free(ptr);
        return true;
    }
    char* strdup(const char* src) {
        int len = strlen(src);
        char* dup = malloc<char>(len + 1);
        memcpy(dup, src, len);
        return dup;
    }
    template<typename T> T* copy(T* ptr, size_t count = 1) {
        T* data = malloc<T>(count);
        memcpy((void*)data, ptr, sizeof(T) * count);
        return data;
    }
};

static Allocator* alloc = new Allocator;

template<typename T> struct List {
    int size = 0, capacity = 4;
    T* items = alloc->malloc<T>(capacity);
    ~List() { alloc->free(items); }
    T& get(int i) {
        return items[i];
    }
    T add(T item) {
        if (size == capacity) {
            capacity *= 2;
            items = alloc->realloc(items, capacity);
        }
        items[size++] = item;
        return item;
    }
    void removeat(int index) {
        if (index < 0 || index >= size) return;
        size--;
        if (index != size) memmove(
            items + index, items + index + 1,
            sizeof(T) * (size - index)
        );
    }
    void remove(T item, Compare comp = NULL) {
        removeat(indexof(item, comp));
    }
    int indexof(T item, Compare comp = NULL) {
        for (int i = 0; i < size; i++) {
            if (comp ? comp(&items[i], &item) == 0 : items[i] == item) return i;
        }
        return -1;
    }
};

template<typename K, typename V> struct Map {
    int size = 0, capacity = 4;
    Compare compare = NULL;
    struct KeyValuePair { K key; V value; }* pairs = alloc->malloc<KeyValuePair>(capacity);
    Map(Compare compare): compare(compare) {}
    ~Map() { alloc->free(pairs); }
    V& get(K key) {
        KeyValuePair* pair = (KeyValuePair*)bsearch(&key, pairs, size, sizeof(KeyValuePair), compare);
        if (!pair) {
            add(key, V{});  // Insert a default-constructed value into the map
            pair = (KeyValuePair*)bsearch(&key, pairs, size, sizeof(KeyValuePair), compare);
        }
        return pair->value;
    }
    V add(K key, V value) {
        if (size == capacity) {
            capacity *= 2;
            pairs = alloc->realloc<KeyValuePair>(pairs, capacity);
        }
        pairs[size].key = key;
        pairs[size].value = value;
        qsort(pairs, ++size, sizeof(KeyValuePair), compare);
        return value;
    }
    void remove(K key) {
        void* ptr = bsearch(&key, pairs, size, sizeof(KeyValuePair), compare);
        if (!ptr) return;
        int index = ((uintptr_t)ptr - (uintptr_t)pairs) / sizeof(KeyValuePair);
        size--;
        if (index != size) memmove(
            pairs + index, pairs + index + 1,
            sizeof(KeyValuePair) * (size - index - 1)
        );
    }
    bool has(K key) {
        return bsearch(&key, pairs, size, sizeof(KeyValuePair), compare) != NULL;
    }
    V getdef(K key, V def) {
        KeyValuePair* pair = (KeyValuePair*)bsearch(&key, pairs, size, sizeof(KeyValuePair), compare);
        if (!pair) return def;
        return pair->value;
    }
};

struct String {
    int length = 0, capacity = 64;
    char* data;
    ~String() { alloc->free(data); }
    String(): data(alloc->malloc<char>(capacity)) {}
    String(const char* str) {
        length = strlen(str);
        capacity = (length + 1) / 64 * 64 + 64;
        data = alloc->malloc<char>(capacity);
        memcpy(data, str, length + 1);
    }
    String(const String& other) {
        length = other.length;
        capacity = other.capacity;
        data = alloc->malloc<char>(capacity);
        memcpy(data, other.data, capacity);
    }
    String(String&& other) {
        data = other.data;
        length = other.length;
        capacity = other.capacity;
        other.length = 0;
        other.capacity = 64;
        other.data = alloc->malloc<char>(capacity);
    }
    String* add(char c) {
        if (length + 1 == capacity) {
            capacity += 64;
            data = alloc->realloc(data, capacity);
        }
        data[length++] = c;
        data[length] = 0;
        return this;
    }
    template<typename... Args> String& format(const char* fmt, Args... args) {
        int size = snprintf(NULL, 0, fmt, convert_arg(args)...);
        char str[size + 1];
        sprintf(str, fmt, convert_arg(args)...);
        return real_concat(str, size);
    }
    String& concat(const char* str) {
        return real_concat(str, strlen(str));
    }
    String& concat(String other) {
        return real_concat(other.data, other.length);
    }
    String& concat_if(bool cond, String str) {
        if (cond) return concat(str);
        return *this;
    }
    template<typename T> String& iterate(int n, T body) {
        for (int i = 0; i < n; i++) {
            concat(body(i));
        }
        return *this;
    }
    String& clear() {
        length = 0;
        if (capacity > 64) {
            capacity = 64;
            data = alloc->realloc(data, capacity);
        }
        data[0] = 0;
        return *this;
    }
    template<typename... Args> static String new_format(const char* fmt, Args... args) {
        return String().format(fmt, args...);
    }
private:
    template<typename T> T convert_arg(T t) { return t; }
    const char* convert_arg(String str) { return str.data; }
    String& real_concat(const char* str, int len) {
        int prev_cap = capacity;
        while (length + len + 1 >= capacity) capacity += 64;
        if (capacity != prev_cap) data = alloc->realloc(data, capacity);
        strcpy(data + length, str);
        length += len;
        data[length] = 0;
        return *this;
    }
};

template<typename T> struct Stack {
    int size = 0, capacity = 4;
    T* items = alloc->malloc<T>(capacity);
    ~Stack() { alloc->free(items); }
    Stack* push(T item) {
        if (size == capacity) {
            capacity *= 2;
            items = alloc->realloc(items, capacity);
        }
        items[size++] = item;
        return this;
    }
    T pop() {
        if (size == 0) return (T){};
        return items[--size];
    }
    T peek() {
        if (size == 0) return (T){};
        return items[size - 1];
    }
};

template<typename T> struct Queue {
    int size = 0, capacity = 4;
    int head = 0, tail = 0;
    T* items = alloc->malloc<T>(capacity);
    ~Queue() { alloc->free(items); }
    Queue* push(T item) {
        if (size == capacity) {
            capacity *= 2;
            T* new_items = (T*)alloc->malloc<T>(capacity);
            memcpy(new_items, items + tail, sizeof(T) * (size - tail));
            memcpy(new_items + size - tail, items, sizeof(T) * tail);
            alloc->free(items);
            tail = 0;
            head = size;
            items = new_items;
        }
        items[head++] = item;
        size++;
        return this;
    }
    T pop() {
        if (size == 0) return (T){};
        size--;
        return items[tail++];
    }
    T peek() {
        if (size == 0) return (T){};
        return items[tail];
    }
};

struct ByteReader {
    int size = 0, ptr = 0;
    uint8_t* bytes = NULL;
    bool do_free = false;
    ~ByteReader() { do_free ? alloc->free(bytes) : false; }
    ByteReader(uint8_t* bytes, uint64_t size, bool do_free = false): bytes(bytes), size(size), do_free(do_free) {}
    template<typename T> T read() {
        if (ptr + sizeof(T) > size) return (T)0;
        T value = *(T*)(bytes + ptr);
        ptr += sizeof(T);
        return value;
    }
    /*const char* read() {
        if (ptr == size) return "";
        const char* str = (char*)bytes + ptr;
        int len = strlen(str);
        ptr += len + 1;
        return str;
    }*/
    ByteReader* enter() {
        return skip(4);
    }
    ByteReader* skip() {
        return skip(read<int32_t>());
    }
    ByteReader* skip(int num_bytes) {
        return seek(ptr + num_bytes);
    }
    ByteReader* seek(int num_bytes) {
        ptr = num_bytes;
        if (ptr < 0) ptr = 0;
        if (ptr > size) ptr = size;
        return this;
    }
};

struct ByteWriter {
    int size = 0, capacity = 256;
    Stack<int> offsets = Stack<int>();
    uint8_t* bytes = alloc->malloc<uint8_t>(capacity);
    ~ByteWriter() { alloc->free(bytes); }
    template<typename T> ByteWriter* write(T item) { return write(&item, 1); }
    template<typename T> ByteWriter* write(T* array, int num_items) {
        int prev_capacity = capacity;
        while (size + sizeof(T) * num_items >= capacity) capacity *= 2;
        if (prev_capacity != capacity) bytes = alloc->realloc<uint8_t>(bytes, capacity);
        memcpy(bytes + size, array, sizeof(T) * num_items);
        size += sizeof(T) * num_items;
        return this;
    }
    /*ByteWriter* write(const char* str) { return write(str, strlen(str) + 1); }
    ByteWriter* write(char* str) { return write(str, strlen(str) + 1); }*/
    ByteWriter* merge(ByteWriter* writer) {
        write(writer->bytes, writer->size);
        delete writer;
        return this;
    }
    ByteWriter* push() {
        offsets.push(size);
        write<uint32_t>((uint32_t)0);
        return this;
    }
    ByteWriter* pop() {
        uint32_t value = size - offsets.peek() - 4;
        memcpy(bytes + offsets.pop(), &value, 4);
        return this;
    }
    ByteReader* read() {
        uint8_t* arr = alloc->malloc<uint8_t>(size);
        memcpy(arr, bytes, size);
        ByteReader* reader = new ByteReader(arr, size, true);
        delete this;
        return reader;
    }
    uint8_t* array() {
        uint8_t* arr = alloc->malloc<uint8_t>(size);
        memcpy(arr, bytes, size);
        delete this;
        return arr;
    }
};

struct FFI {
    int num_ints = 0, num_floats = 0;
    uint64_t int_regs[8];
    double   flt_regs[8];
    List<uint64_t> stack;

    uint64_t int_value = 0;
    double float_value = 0;

    bool varargs = false;

    void push_int(uint64_t value) {
        if (num_ints >= NUM_INT_REGS) stack.add(value);
        else int_regs[num_ints++] = value;
    }
    void push_f32(float value) {
        if (varargs) {
            push_f64(value);
            return;
        }
        double f64 = 0;
        memcpy(&f64, &value, sizeof(float));
        push_f64(f64);
    }
    void push_f64(double value) {
#ifdef _WIN32
        if (varargs) {
            push_int(*(uint64_t*)&value);
            return;
        }
        if (num_ints >= NUM_FLT_REGS) stack.add(*(uint64_t*)&value);
        else flt_regs[num_ints++] = value;
#else
        if (num_floats >= NUM_FLT_REGS) stack.add(*(uint64_t*)&value);
        else flt_regs[num_floats++] = value;
#endif
    }
    uint64_t get_int() {
        return int_value;
    }
    float get_f32() {
        return *(float*)&float_value;
    }
    double get_f64() {
        return float_value;
    }
    void __attribute__((noinline, optnone, naked)) call(void* ptr) { asm(
        "push %r12\n"
        "push %r13\n"
        "push %rbp\n"
        "mov %rsp, %rbp\n"

#ifdef _WIN32
        "mov %rcx, %r12\n"
        "mov %rdx, %r13\n"
#else
        "mov %rdi, %r12\n"
        "mov %rsi, %r13\n"
#endif

        "xor %rax, %rax\n"
        "mov 0x88(%r12), %eax\n"
        "inc %eax\n"
        "imull $8, %eax\n"
        "andl $0xFFFFFFF0, %eax\n"
        "sub %rax, %rsp\n"
#ifdef _WIN32
        "mov %rax, %r8\n"
        "mov %rsp, %rcx\n"
        "mov 0x90(%r12), %rdx\n"
        "sub $0x20, %rsp\n"
#else
        "mov %rax, %rdx\n"
        "mov %rsp, %rdi\n"
        "mov 0x90(%r12), %rsi\n"
#endif
        "call memcpy\n"

        "xor %rax, %rax\n"
#ifdef _WIN32
        "mov 0x08(%r12), %rcx\n"
        "mov 0x10(%r12), %rdx\n"
        "mov 0x18(%r12), %r8\n"
        "mov 0x20(%r12), %r9\n"
#else
        "mov 0x08(%r12), %rdi\n"
        "mov 0x10(%r12), %rsi\n"
        "mov 0x18(%r12), %rdx\n"
        "mov 0x20(%r12), %rcx\n"
        "mov 0x28(%r12), %r8\n"
        "mov 0x30(%r12), %r9\n"
        "movq 0x68(%r12), %xmm4\n"
        "movq 0x70(%r12), %xmm5\n"
        "movq 0x78(%r12), %xmm6\n"
        "movq 0x80(%r12), %xmm7\n"
        "mov 0x04(%r12), %eax\n"
#endif
        "movq 0x48(%r12), %xmm0\n"
        "movq 0x50(%r12), %xmm1\n"
        "movq 0x58(%r12), %xmm2\n"
        "movq 0x60(%r12), %xmm3\n"

        "call *%r13\n"
#ifdef _WIN32
        "add $0x20, %rsp\n"
#endif
        "mov %rax, 0x98(%r12)\n"
        "movq %xmm0, 0xA0(%r12)\n"

        "leave\n"
        "pop %r13\n"
        "pop %r12\n"
        "ret\n"
    ); }
};

// == UTILITY FUNCTIONS ==

static void make_executable(void* ptr, size_t size) {
#ifdef _WIN32
    DWORD old_protect;
    VirtualProtect(ptr, size, PAGE_EXECUTE_READWRITE, &old_protect);
#else
    size_t pagesize = sysconf(_SC_PAGESIZE);
    void* page_start = (void*)((uintptr_t)ptr / pagesize * pagesize);
    size_t length = ((uintptr_t)ptr + (pagesize - 1)) / pagesize * pagesize;
    mprotect((void*)page_start, length, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
}

static char* strfmt(const char* fmt, ...) {
    va_list args1, args2;
    va_start(args1, fmt);
    va_copy(args2, args1);
    int size = vsnprintf(NULL, 0, fmt, args1);
    char* str = alloc->malloc<char>(size + 1);
    vsprintf(str, fmt, args2);
    va_end(args1);
    va_end(args2);
    return str;
}

static int decode_utf8(char* seq, int* out) {
    unsigned char* bytes = (unsigned char*)seq;
    if ((bytes[0] & 0x80) == 0x00) {
        *out = bytes[0];
        return 1;
    }
    else if ((bytes[0] & 0xE0) == 0xC0) {
        *out = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        return 2;
    }
    else if ((bytes[0] & 0xF0) == 0xE0) {
        *out = ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
        return 3;
    }
    else if ((bytes[0] & 0xF8) == 0xF0) {
        *out = ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
        return 4;
    }
    return 0;
}

static String unescape_string(String str) {
    String unescaped;
    int i = 0;
    while (str.data[i]) {
        int c;
        i += decode_utf8(str.data + i, &c);
        switch (c) {
            case '\a': unescaped.concat("\\a"); break;
            case '\b': unescaped.concat("\\b"); break;
            case '\e': unescaped.concat("\\e"); break;
            case '\f': unescaped.concat("\\f"); break;
            case '\n': unescaped.concat("\\n"); break;
            case '\r': unescaped.concat("\\r"); break;
            case '\t': unescaped.concat("\\t"); break;
            case '\v': unescaped.concat("\\v"); break;
            case '\"': unescaped.concat("\"");  break;
            default:
                if (c >= ' ' && c <= '~') unescaped.add(c);
                else if (c <= 255) unescaped.format("\\x%02x", c);
                else if (c <= 65535) unescaped.format("\\u%04x", c);
                else unescaped.format("\\U%08x", c);
                break;
        }
    }
    return unescaped;
}

static void dirname(String* str) {
    for (int i = str->length - 1; i >= 0; i--) {
        if (str->data[i] == PATH_SEPARATOR) {
            str->data[i] = 0;
            str->length = i;
            break;
        }
    }
    str->data[0] = '.';
    str->data[1] = 0;
    str->length = 1;
}

template<typename T> T* mkptr(T&& val) {
    return &val;
}

// == TYPES ==

struct ErrorScope {
    struct ErrorScope* parent;
    const char* file;
    const char* name;
    int row, col;
};

struct Error {
    ErrorScope* scope;
    char* msg;
    static Error* syntax(const char* filename, int row, int col, String str);
    static Error* parser(struct Token* token, String str);
    static Error* runtime(struct Context* context, String str);
};

enum CaptureMode: uint8_t {
    CaptureMode_None,
    CaptureMode_Shared,
    CaptureMode_CopyPerCall,
    CaptureMode_CopyOnce,
};

struct Function {
    char jmp[5];
    char* name;
    char* file;
    uint8_t* entry;
    uint64_t length;
    CaptureMode capture_mode;
    Map<char*, struct Variable*>* variables;
    struct Type* this_ptr_type;
    void* this_ptr;

    bool is_native() {
         // 0xE9 is the jmp instruction, realistically no function begins with the jmp instruction besides the one we generated
        return *(uint8_t*)this != 0xE9;
    }
};

enum TypeKind: uint8_t {
    TypeKind_Void,
    TypeKind_Int8,
    TypeKind_Int16,
    TypeKind_Int32,
    TypeKind_Int64,
    TypeKind_Float32,
    TypeKind_Float64,
    TypeKind_Pointer,
    TypeKind_Struct,
    TypeKind_Function,
    TypeKind_Type,
    TypeKind_Varargs,
    TypeKind_Deferred,
    TypeKind_Parent,
};

struct Type {
    struct TypeHandle {
        Type* type;
        Type* parent;
        TypeHandle(Type* type = NULL, Type* parent = NULL): type(type), parent(parent) {}
        Type* operator->() {
            type->parent = parent;
            return type->resolve_parent();
        }
        operator Type*() {
            type->parent = parent;
            return type->resolve_parent();
        }
        Type* operator=(Type* other) {
            type = other;
            other->parent = parent;
            return type->resolve_parent();
        }
        Type* operator<<(Type* other) {
            type->parent = parent = other;
            return other->resolve_parent();
        }
    };
    struct Param {
        TypeHandle type = NULL;
        char* name = NULL;
    };
    struct Field: Param {
        size_t offset = 0;
        uintptr_t value = 0;
        bool inlined = 0;
    };
    Type() { memset((void*)this, 0, sizeof(*this)); }

    Type* parent;
    TypeKind kind;
    bool is_const;
    bool is_unsigned;
    bool lvalue_return;
    uint64_t hash;
    int size, alignment;
    union {
        struct {
            TypeHandle base;
        } pointer_info;
        struct {
            char* name;
        } defer_info;
        struct {
            int offset;
        } parent_info;
        struct {
            size_t num_params;
            TypeHandle return_type;
            Param* params;
        } function_info;
        struct {
            size_t num_fields;
            Field* fields;
        } struct_info;
    };

    Type* resolve_parent() {
        Type* type = this;
        if (type->kind == TypeKind_Parent) for (int i = 0; i < parent_info.offset; i++) type = type->parent;
        return type;
    }

    Type* unsign(struct Context* context);
    Type* constant(struct Context* context);
    Type* pointer(struct Context* context);
    Type* function(struct Context* context, List<Param>* params, bool lvalue_return);
    Type* resolve_defers(struct Context* context);
    void destroy() {
        if (kind == TypeKind_Struct) alloc->free(struct_info.fields);
        if (kind == TypeKind_Function) alloc->free(function_info.params);
        alloc->free(this);
    }
    void build_struct_layout() {
        this->size = 0; this->alignment = 1;
        size_t prev_offset = 0;
        for (int i = 0; i < struct_info.num_fields; i++) {
            Type::Field* field = &struct_info.fields[i];
            if (field->type->kind == TypeKind_Pointer) {
                if (field->offset == -1) field->offset = prev_offset;
                else if (field->offset & (1LL << 63)) field->offset = prev_offset + (field->offset & (uint64_t)(1LL << 63) - 1);
                prev_offset = field->offset;
                continue;
            }
            size_t alignment = field->inlined ? field->type->alignment : field->type->value_size();
            if (field->offset == -1) {
                if (this->size % alignment) field->offset = this->size + alignment - this->size % alignment;
                else field->offset = this->size;
            }
            else if (field->offset & (1LL << 63)) field->offset = prev_offset + (field->offset & (uint64_t)(1LL << 63) - 1);
            prev_offset = field->offset;
            size_t size = (field->inlined ? field->type->size : field->type->value_size()) + field->offset;
            if (this->size < size) this->size = size;
            if (this->alignment < alignment) this->alignment = alignment;
        }
        if (this->size % this->alignment != 0) this->size += this->alignment - this->size % this->alignment;
    }
    int value_size() {
        switch (kind) {
            case TypeKind_Void:
                return 0;
            case TypeKind_Int8:
                return 1;
            case TypeKind_Int16:
                return 2;
            case TypeKind_Int32:
            case TypeKind_Float32:
                return 4;
            default:
                return 8;
        }
    }
    String to_string(List<Type*>* visited_list) {
        int index = visited_list->indexof(this);
        if (index != -1) return String::new_format("S%d", index + 1);
        switch (kind) {
            case TypeKind_Void:     return "void";
            case TypeKind_Type:     return "type";
            case TypeKind_Int8:     return is_unsigned ? "u8"  : "s8";
            case TypeKind_Int16:    return is_unsigned ? "u16" : "s16";
            case TypeKind_Int32:    return is_unsigned ? "u32" : "s32";
            case TypeKind_Int64:    return is_unsigned ? "u64" : "s64";
            case TypeKind_Float32:  return "f32";
            case TypeKind_Float64:  return "f64";
            case TypeKind_Pointer:  return String::new_format("%s#", pointer_info.base->to_string(visited_list));
            case TypeKind_Varargs:  return "...";
            case TypeKind_Function: return function_info.return_type->to_string(visited_list).concat("<-(").iterate(function_info.num_params, [&](int i) { return String()
                .concat(function_info.params[i].type->to_string(visited_list))
                .concat_if(function_info.params[i].name, String::new_format(" %s", function_info.params[i].name))
                .concat_if(i + 1 < function_info.num_params, ", ");
            }).concat(")");
            case TypeKind_Struct:   return String::new_format("struct S%ld { ", (visited_list->add(this), visited_list->size)).iterate(struct_info.num_fields, [&](int i) { return String()
                .concat_if(struct_info.fields[i].inlined, "inline ")
                .format("%s %s @ %ld; ", struct_info.fields[i].type->to_string(visited_list), struct_info.fields[i].name, struct_info.fields[i].offset);
            }).concat("}");
            case TypeKind_Deferred: return String::new_format("(defer %s)", defer_info.name);
            case TypeKind_Parent:   return String::new_format("(parent %d)", parent_info.offset);
        }
    }
    String to_string() {
        List<Type*> list;
        return to_string(&list);
    }
};

struct Variable {
    template<typename T> struct Value {
        T* ptr;
        int size = 0;
        bool is_unsigned = false;
        Value(T* ptr, int size = sizeof(T), bool is_unsigned = false): ptr(ptr), size(size), is_unsigned(is_unsigned) {}
        T operator=(const T& other) {
            memcpy(ptr, &other, size);
            return *this;
        }
        T operator=(const Value<T>& other) {
            return *this = (T)other;
        }
        operator T() const {
            T value = {};
            memcpy(&value, ptr, size);
            bool is_negative = is_unsigned ? false : (uintptr_t)value & (1 << (size * 8 - 1));
            int negative_bytes = sizeof(T) - size;
            if (negative_bytes > 0) memset((char*)&value + size, is_negative ? 0xFF : 0x00, negative_bytes);
            return value;
        }
        Value<T>& operator  +=(const T& oth) { T val = *this; val  += oth; *this = val; return *this; }
        Value<T>& operator  -=(const T& oth) { T val = *this; val  -= oth; *this = val; return *this; }
        Value<T>& operator  *=(const T& oth) { T val = *this; val  *= oth; *this = val; return *this; }
        Value<T>& operator  /=(const T& oth) { T val = *this; val  /= oth; *this = val; return *this; }
        Value<T>& operator  %=(const T& oth) { T val = *this; val  %= oth; *this = val; return *this; }
        Value<T>& operator  &=(const T& oth) { T val = *this; val  &= oth; *this = val; return *this; }
        Value<T>& operator  |=(const T& oth) { T val = *this; val  |= oth; *this = val; return *this; }
        Value<T>& operator  ^=(const T& oth) { T val = *this; val  ^= oth; *this = val; return *this; }
        Value<T>& operator <<=(const T& oth) { T val = *this; val <<= oth; *this = val; return *this; }
        Value<T>& operator >>=(const T& oth) { T val = *this; val >>= oth; *this = val; return *this; }
        T operator->() const { return (T)*this; }
        Value<T>& operator++() { *this = (T)(*this) + 1; return *this; }
        T operator++(int) { T old = *this; ++(*this); return old; }
        Value<T>& operator--() { *this = (T)(*this) - 1; return *this; }
        T operator--(int) { T old = *this; --(*this); return old; }
    };

    Type* type = NULL;
    bool ref = false;
    int refcount = 0;
    void* _value = NULL;

    Variable(Type* type = NULL): type(type) {}
    Variable(const Variable& other): type(other.type), ref(other.ref), _value(other._value) {}

    Variable& operator<<(const Variable& other) {
        if (!type || !other.type) return *this;
        uint64_t value = 0;
        memcpy(&value, other.ptr(), other.type->value_size());
        bool is_negative = other.type->is_unsigned ? false : value & (1 << (other.type->value_size() * 8 - 1));
        int negative_size = type->value_size() - other.type->value_size();
        if (negative_size > 0) memset((char*)&value + other.type->value_size(), is_negative ? 0xFF : 0x00, negative_size);
        memcpy(ptr(), &value, type->value_size());
        return *this;
    }

    template<typename T> Value<T> as() const {
        return Value<T>(ptr<T>(), sizeof(T) < type->value_size() ? sizeof(T) : type->value_size(), type->is_unsigned);
    }
    template<typename T = void> T* ptr() const {
        return (T*)(ref ? _value : &_value);
    }
    Variable deref(int offset = 0) {
        return Variable(type->pointer_info.base).lvalue(as<char*>() + type->pointer_info.base->value_size() * offset);
    }

    Variable& rvalue() {
        if (!ref) return *this;
        _value = *(void**)_value;
        ref = false;
        return *this;
    }
    Variable& lvalue(void* storage) {
        if (ref) return *this;
        _value = storage;
        ref = true;
        return *this;
    }
    Variable& retain() {
        refcount++;
        return *this;
    }
    bool release() {
        refcount--;
        if (refcount == 0) {
            alloc->free(this);
            return true;
        }
        return false;
    }

    String to_string() {
        if (type->kind == TypeKind_Pointer && type->pointer_info.base->kind == TypeKind_Int8 && !type->pointer_info.base->is_unsigned)
            return String::new_format("\"%s\"", unescape_string((char*)as<char*>()));
        switch (type->kind) {
            case TypeKind_Void:     return "(void)";
            case TypeKind_Int8:     return String::new_format(type->is_unsigned ? "%u"  : "%d",  (uint8_t)as<uint8_t>());
            case TypeKind_Int16:    return String::new_format(type->is_unsigned ? "%u"  : "%d",  (uint16_t)as<uint16_t>());
            case TypeKind_Int32:    return String::new_format(type->is_unsigned ? "%u"  : "%d",  (uint32_t)as<uint32_t>());
            case TypeKind_Int64:    return String::new_format(type->is_unsigned ? "%lu" : "%ld", (uint64_t)as<uint64_t>());
            case TypeKind_Float32:  return String::new_format("%g", (float)as<float>());
            case TypeKind_Float64:  return String::new_format("%g", (double)as<double>());
            case TypeKind_Pointer:  return String::new_format("0x%016lx", (uint64_t)as<uint64_t>());
            case TypeKind_Type:     return as<Type*>()->to_string();
            case TypeKind_Struct:   return !as<void*>() ? "(null)" : String().concat("{").iterate(type->struct_info.num_fields, [&](int i) { return String()
                .format(" %s = %s", type->struct_info.fields[i].name, type->struct_info.fields[i].inlined
                    ? String::new_format("%s", Variable(type->struct_info.fields[i].type).lvalue(ptr<char*>() + type->struct_info.fields[i].offset).to_string())
                    : String::new_format("%s", Variable(type->struct_info.fields[i].type).lvalue(as <char*>() + type->struct_info.fields[i].offset).to_string())
                )
                .concat_if(i + 1 < type->struct_info.num_fields, ",");
            }).concat(" }");
            default: return type->to_string();
        }
    }
};

struct VarargsInfo {
    Variable* array;
    int num_args;
    VarargsInfo(Variable* array, int num_args): array(array), num_args(num_args) {}
};

struct VarargsData {
    enum {
        End,
        Integer,
        FloatingPoint,
    } type;
    union {
        uint64_t integer;
        double floating_point;
    };
};

struct TypeCache: Map<uint64_t, Type*> {
    TypeCache(): Map<uint64_t, Type*>(compare_int64) {}
    ~TypeCache() {
        for (int i = 0; i < size; i++) pairs[i].value->destroy();
    }
    static uint64_t hash_mix(uint64_t a, uint64_t b) {
        return a ^ (b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2));
    }
    static uint64_t hash_str(char* str) {
        uint64_t hash = 0xCBF29CE484222325ULL;
        while (str && *str) hash = (hash ^ (uint8_t)*str++) * 0x100000001B3ULL;
        return hash;
    }
    static uint64_t hash_ptr(void* ptr) {
        return (uintptr_t)ptr;
    }
    uint64_t hash_type(Type* type) {
        if (type->hash) return type->hash;
        uint64_t hash = 0xCBF29CE484222325ULL;
        hash = hash_mix(hash, type->kind);
        hash = hash_mix(hash, type->is_const);
        hash = hash_mix(hash, type->is_unsigned);
        hash = hash_mix(hash, type->lvalue_return);
        switch (type->kind) {
            case TypeKind_Pointer:
                hash = hash_mix(hash, hash_ptr(type->pointer_info.base));
                break;
            case TypeKind_Struct:
                hash = hash_mix(hash, type->struct_info.num_fields);
                for (size_t i = 0; i < type->struct_info.num_fields; i++) {
                    hash = hash_mix(hash, type->struct_info.fields[i].value);
                    hash = hash_mix(hash, hash_str(type->struct_info.fields[i].name));
                    hash = hash_mix(hash, hash_ptr(type->struct_info.fields[i].type));
                    hash = hash_mix(hash, type->struct_info.fields[i].inlined);
                    hash = hash_mix(hash, type->struct_info.fields[i].offset);
                }
                break;
            case TypeKind_Function:
                hash = hash_mix(hash, hash_ptr(type->function_info.return_type));
                hash = hash_mix(hash, type->function_info.num_params);
                for (size_t i = 0; i < type->function_info.num_params; ++i) {
                    hash = hash_mix(hash, hash_str(type->function_info.params[i].name));
                    hash = hash_mix(hash, hash_ptr(type->function_info.params[i].type));
                }
                break;
            case TypeKind_Deferred:
                hash = hash_mix(hash, hash_str(type->defer_info.name));
                break;
            case TypeKind_Parent:
                hash = hash_mix(hash, type->parent_info.offset);
                break;
            default: break;
        }
        type->hash = hash;
        return hash;
    }
    Type* register_type(Type* type, bool force_clean = false) {
        hash_type(type);
        if (has(type->hash)) {
            if ((!type->is_const && !type->is_unsigned) || force_clean) {
                if (type->kind == TypeKind_Struct) alloc->free(type->struct_info.fields);
                if (type->kind == TypeKind_Function) alloc->free(type->function_info.params);
            }
            return get(type->hash);
        }
        else {
            type = add(type->hash, alloc->copy<Type>(type));
            if (type->kind == TypeKind_Pointer) type->pointer_info.base << type;
            if (type->kind == TypeKind_Struct) for (int i = 0; i < type->struct_info.num_fields; i++) type->struct_info.fields[i].type << type;
            if (type->kind == TypeKind_Function) for (int i = 0; i < type->function_info.num_params; i++) type->function_info.params[i].type << type;
            return type;
        }
    }
    Type* primitive(TypeKind kind) {
        Type type;
        type.kind = kind;
        type.size = type.alignment =
            kind == TypeKind_Int8    ? 1 :
            kind == TypeKind_Int16   ? 2 :
            kind == TypeKind_Int32   ? 4 :
            kind == TypeKind_Float32 ? 4 : 8;
        return register_type(&type);
    }
    Type* unsign(Type* type) {
        Type unsigned_type = *type;
        unsigned_type.hash = 0;
        unsigned_type.is_unsigned = true;
        return register_type(&unsigned_type);
    }
    Type* constant(Type* type) {
        Type const_type = *type;
        const_type.hash = 0;
        const_type.is_const = true;
        return register_type(&const_type);
    }
    Type* pointer(Type* type) {
        Type ptr;
        ptr.kind = TypeKind_Pointer;
        ptr.size = ptr.alignment = 8;
        ptr.pointer_info.base = type;
        return register_type(&ptr);
    }
    Type* structure(List<Type::Field>* fields) {
        Type type;
        type.kind = TypeKind_Struct;
        type.struct_info.num_fields = fields->size;
        type.struct_info.fields = alloc->malloc<Type::Field>(fields->size);
        for (int i = 0; i < type.struct_info.num_fields; i++) type.struct_info.fields[i] = fields->get(i);
        type.build_struct_layout();
        return register_type(&type);
    }
    Type* function(Type* ret, List<Type::Param>* params, bool lvalue_return) {
        Type type;
        type.kind = TypeKind_Function;
        type.lvalue_return = lvalue_return;
        type.function_info.return_type = ret;
        type.function_info.num_params = params->size;
        type.function_info.params = alloc->malloc<Type::Param>(params->size);
        type.size = type.alignment = 8;
        for (int i = 0; i < params->size; i++) type.function_info.params[i] = params->get(i);
        return register_type(&type);
    }
    Type* deferred(char* name) {
        Type type;
        type.kind = TypeKind_Deferred;
        type.defer_info.name = name;
        type.size = type.alignment = 8;
        return register_type(&type);
    }
    Type* parent_ref(int level) {
        Type type;
        type.kind = TypeKind_Parent;
        type.parent_info.offset = level;
        return register_type(&type);
    }
    Type* resolve_defers_inner(Type* orig, Context* context, Stack<Type*>* parent_stack);
    Type* resolve_single_defer(Type* orig, Context* context, Set<Type*>* visited);
    Type* resolve_defers(Type* orig, Context* context) {
        Stack<Type*> parent_stack;
        Type* type = resolve_defers_inner(orig, context, &parent_stack);
        Set<Type*> visited(compare_int64);
        validate_type(context, type, &visited);
        return type;
    }
    void validate_type(Context* context, Type* type, Set<Type*>* visited) {
        if (type->kind != TypeKind_Struct) return;
        visited->add(type);
        for (int i = 0; i < type->struct_info.num_fields; i++) {
            Type::Field* field = &type->struct_info.fields[i];
            if (!field->inlined) continue;
            if (visited->find(field->type)) throw Error::runtime(context, String::new_format("Cannot resolve defers: Inline field '%s' recurses", field->name ? field->name : "<anonymous>"));
            if (field->type->kind != TypeKind_Struct && field->type->kind != TypeKind_Pointer) throw Error::runtime(context, String::new_format("Inline field '%s' is not a struct or pointer type", field->name));
            if (field->type->kind == TypeKind_Pointer && !field->name) throw Error::runtime(context, String::new_format("Inline field is a pointer but has no identifier", field->name));
            validate_type(context, field->type, visited);
        }
    }
};

struct Allocation {
    void* data;
    size_t size = -1;
    void(*cleanup)(void*, Context*, Type*);
    Type* type; Context* context;
    Allocation(void* ptr): data(ptr) {}
    Allocation(size_t size, Context* context, Type* type, void(*cleanup)(void*, Context*, Type*)):
        size(size), data(alloc->malloc<uint8_t>(size)), context(context), type(type), cleanup(cleanup) {}
    ~Allocation() {
        if (size == -1) return;
        cleanup(data, context, type);
        alloc->free(data);
    }

    static void function_cleanup(void* ptr, Context* context, Type* type);
    static void struct_cleanup(void* ptr, Context* context, Type* type);
};

struct Scope {
    char* file;
    char* name;
    int row, col;
    int scope_id;
};

enum State {
    State_Running,
    State_Return,
    State_Continue,
    State_Break,
};

struct Context {
    struct LocationHook {
        Context* context;
        int row, col;
        ~LocationHook() {
            if (row == 0 && col == 0) return;
            Scope* scope = context->call_stack->peek();
            scope->row = row;
            scope->col = col;
        }
    };

    Set<char*>* strings;
    Set<ByteReader*>* bytecodes;
    Stack<Scope*>* call_stack;
    Stack<Map<char*, Variable*>*>* variables;
    Stack<Set<Allocation*>*>* allocs;
    Map<void*, Function*>* function_cache;
    TypeCache* type_cache;
    State state = State_Running;
    Variable state_var, this_pointer;

    Variable* lookup_variable(const char* name) {
        for (int i = variables->size - 1; i >= 0; i--) {
            if (i != 0 && i < call_stack->peek()->scope_id) continue;
            if (!variables->items[i]->has((char*)name)) continue;
            Variable* var = variables->items[i]->get((char*)name);
            Function* func = var->as<Function*>();
            if (var->type->kind == TypeKind_Function && func && !func->is_native()) {
                func->name = (char*)name;
                func->this_ptr = NULL;
            }
            return var;
        }
        return NULL;
    }
    Variable load(const char* name) {
        Variable* var = lookup_variable(name);
        return var ? Variable(var->type).lvalue(var->ptr()) : Variable();
    }
    Variable store(const char* name, Variable var, void* symbol = NULL) {
        if (variables->peek()->has((char*)name)) return Variable();
        Variable* copy = &alloc->copy(&var)->rvalue();
        copy->retain();
        if (symbol) {
            if (copy->type->kind == TypeKind_Function) copy->as<Function*>() = (Function*)symbol;
            else copy = &copy->lvalue(symbol);
        }
        variables->peek()->add((char*)name, copy);
        return Variable(copy->type).lvalue(copy->ptr());
    }
    Variable store_ref(const char* name, Variable* var) {
        if (variables->peek()->has((char*)name)) return Variable();
        variables->peek()->add((char*)name, &var->retain());
        return Variable(var->type).lvalue(var->ptr());
    }
    int scopeof(const char* name) {
        for (int i = variables->size - 1; i >= 0; i--) {
            if (i != 0 && i < call_stack->peek()->scope_id) continue;
            if (variables->items[i]->has((char*)name)) return i;
        }
        return -1;
    }
    void push_stack_frame(const char* name) {
        Scope* scope = alloc->malloc<Scope>();
        scope->name = (char*)name;
        scope->scope_id = variables->size;
        scope->file = call_stack->size > 0 ? call_stack->peek()->file : NULL;
        call_stack->push(scope);
        push_codeblock();
    }
    void pop_stack_frame() {
        Scope* scope = call_stack->peek();
        while (variables->size > scope->scope_id) pop_codeblock();
        call_stack->pop();
        alloc->free(scope);
    }
    void push_codeblock() {
        variables->push(new Map<char*, Variable*>(compare_strings));
        allocs->push(new Set<Allocation*>([](const void* _a, const void* _b) -> int {
            Allocation* a = *(Allocation**)_a;
            Allocation* b = *(Allocation**)_b;
            int diff = (b->type->kind == TypeKind_Struct) - (a->type->kind == TypeKind_Struct);
            return diff == 0 ? (uintptr_t)b - (uintptr_t)a : diff;
        }));
    }
    void pop_codeblock() {
        Map<char*, Variable*>* map = variables->peek();
        Set<Allocation*>* scope = allocs->peek();
        for (int i = 0; i < scope->size; i++) delete scope->items[i];
        for (int i = 0; i < map->size; i++) map->pairs[i].value->release();
        delete variables->pop();
        delete allocs->pop();
    }
    void pop_until(int scope) {
        scope++;
        while (call_stack->peek()->scope_id > scope) pop_stack_frame();
        while (variables->size > scope) pop_codeblock();
    }
    void* new_allocation(size_t size, bool scoped, Type* type, void(*cleanup)(void*, Context*, Type*) = NULL) {
        Set<Allocation*>* scope = allocs->items[0];
        if (scoped) scope = allocs->peek();
        Allocation* alloc = new Allocation(size, this, type, cleanup);
        scope->add(alloc);
        return alloc->data;
    }
    void move_allocation(void* ptr, int new_scope) {
        if (new_scope < 0) new_scope = 0;
        if (new_scope > variables->size - 1) new_scope = variables->size - 1;
        for (int i = allocs->size - 1; i >= 0; i--) {
            Set<Allocation*>* scope = allocs->items[i];
            Allocation temp = ptr;
            Allocation** alloc_ptr = scope->find(&temp);
            if (!alloc_ptr) continue;
            scope->remove(*alloc_ptr);
            allocs->items[new_scope]->add(*alloc_ptr);
        }
    }
    void delete_allocation(void* ptr) {
        for (int i = allocs->size - 1; i >= 0; i--) {
            Set<Allocation*>* scope = allocs->items[i];
            Allocation temp = ptr;
            Allocation** alloc_ptr = scope->find(&temp);
            if (!alloc_ptr) continue;
            delete *alloc_ptr;
            scope->remove(*alloc_ptr);
        }
    }
    int alloc_size(void* ptr) {
        for (int i = allocs->size - 1; i >= 0; i--) {
            Set<Allocation*>* scope = allocs->items[i];
            Allocation temp = ptr;
            Allocation** alloc_ptr = scope->find(&temp);
            if (!alloc_ptr) continue;
            return (*alloc_ptr)->size;
        }
        return -1;
    }
    int alloc_scope(void* ptr) {
        for (int i = allocs->size - 1; i >= 0; i--) {
            Set<Allocation*>* scope = allocs->items[i];
            Allocation temp = ptr;
            Allocation** alloc_ptr = scope->find(&temp);
            if (!alloc_ptr) continue;
            return i;
        }
        return -1;
    }
    bool is_allocated(void* ptr) {
        return alloc_size(ptr) != -1;
    }
    void set_location(ByteReader* reader, LocationHook* hook) {
        Scope* scope = call_stack->peek();
        hook->context = this;
        hook->row = scope->row;
        hook->col = scope->col;
        scope->row = reader->read<int32_t>();
        scope->col = reader->read<int32_t>();
    }
    void set_file_location(char* file) {
        call_stack->peek()->file = file;
    }
    String resource(String name) {
        String path = String().concat(call_stack->peek()->file ? call_stack->peek()->file : ".");
        dirname(&path);
        return path.format("%c%s", PATH_SEPARATOR, name);
    }
};

Type* Type::unsign(Context* context) {
    return context->type_cache->unsign(this);
}

Type* Type::constant(Context* context) {
    return context->type_cache->constant(this);
}

Type* Type::pointer(Context* context) {
    return context->type_cache->pointer(this);
}

Type* Type::function(Context* context, List<Type::Param>* params, bool lvalue_return) {
    return context->type_cache->function(this, params, lvalue_return);
}

Type* Type::resolve_defers(Context* context) {
    return context->type_cache->resolve_defers(this, context);
}

Type* TypeCache::resolve_defers_inner(Type* orig, Context* context, Stack<Type*>* parent_stack) {
    for (int i = parent_stack->size - 1; i >= 0; i--) {
        if (parent_stack->items[i] == orig) return parent_ref(parent_stack->size - i);
    }
    switch (orig->kind) {
        case TypeKind_Deferred: {
            Set<Type*> visited(compare_int64);
            Type* type = resolve_defers_inner(resolve_single_defer(orig, context, &visited), context, parent_stack);
            for (int i = parent_stack->size - 1; i >= 0; i--) {
                if (parent_stack->items[i] == type) return parent_ref(parent_stack->size - i);
            }
            return type;
        } break;
        case TypeKind_Pointer: {
            parent_stack->push(orig);
            Type ptr = *orig;
            ptr.hash = 0;
            ptr.pointer_info.base = resolve_defers_inner(orig->pointer_info.base, context, parent_stack);
            parent_stack->pop();
            return register_type(&ptr);
        } break;
        case TypeKind_Function: {
            parent_stack->push(orig);
            Type func = *orig;
            func.hash = 0;
            func.function_info.params = alloc->copy(orig->function_info.params, orig->function_info.num_params);
            func.function_info.return_type = resolve_defers_inner(orig->function_info.return_type, context, parent_stack);
            for (int i = 0; i < func.function_info.num_params; i++) {
                func.function_info.params[i].type = resolve_defers_inner(orig->function_info.params[i].type, context, parent_stack);
            }
            parent_stack->pop();
            return register_type(&func, true);
        } break;
        case TypeKind_Struct: {
            parent_stack->push(orig);
            Type str = *orig;
            str.hash = 0;
            str.struct_info.fields = alloc->copy(orig->struct_info.fields, orig->struct_info.num_fields);
            for (int i = 0; i < str.struct_info.num_fields; i++) {
                str.struct_info.fields[i].type = resolve_defers_inner(orig->struct_info.fields[i].type, context, parent_stack);
            }
            parent_stack->pop();
            return register_type(&str, true);
        } break;
        default: return orig;
    }
}

Type* TypeCache::resolve_single_defer(Type* type, Context* context, Set<Type*>* visited) {
    if (type->kind != TypeKind_Deferred) return type;
    if (visited->find(type)) throw Error::runtime(context, String::new_format("Cannot resolve defers: Defer '%s' recurses", type->defer_info.name));
    visited->add(type);
    Variable var = context->load(type->defer_info.name);
    if (!var.type) throw Error::runtime(context, String::new_format("Cannot resolve defers: Variable '%s' not found", type->defer_info.name));
    if (var.type->kind != TypeKind_Type) throw Error::runtime(context, String::new_format("Cannot resolve defers: Variable '%s' is not a type", type->defer_info.name));
    return resolve_single_defer(var.as<Type*>(), context, visited);
}

// == LEXER ==

#define PROCESS_TOKENS(type) TOKENS(type##_KEYWORD, type##_SYMBOL, type##_SPECIAL)

#define ENUM_KEYWORD(x) TOKEN_##x,
#define ENUM_SPECIAL(x) TOKEN_##x,
#define ENUM_SYMBOL(x, y) TOKEN_##y,
#define DECL_KEYWORD(x) #x,
#define DECL_SPECIAL(x) NULL,
#define DECL_SYMBOL(x, y) x,

#define TOKENS(KEYWORD, SYMBOL, SPECIAL) \
    SPECIAL(END_OF_FILE) \
    KEYWORD(if) \
    KEYWORD(else) \
    KEYWORD(while) \
    KEYWORD(for) \
    KEYWORD(incl) \
    KEYWORD(excl) \
    KEYWORD(step) \
    KEYWORD(return) \
    KEYWORD(continue) \
    KEYWORD(break) \
    KEYWORD(try) \
    KEYWORD(catch) \
    KEYWORD(silently) \
    KEYWORD(as) \
    KEYWORD(throw) \
    KEYWORD(sizeof) \
    KEYWORD(typeof) \
    KEYWORD(scopeof) \
    KEYWORD(include) \
    KEYWORD(new) \
    KEYWORD(scoped) \
    KEYWORD(delete) \
    KEYWORD(move) \
    KEYWORD(defer) \
    KEYWORD(this) \
    KEYWORD(s8) \
    KEYWORD(s16) \
    KEYWORD(s32) \
    KEYWORD(s64) \
    KEYWORD(u8) \
    KEYWORD(u16) \
    KEYWORD(u32) \
    KEYWORD(u64) \
    KEYWORD(f32) \
    KEYWORD(f64) \
    KEYWORD(bool) \
    KEYWORD(void) \
    KEYWORD(type) \
    KEYWORD(struct) \
    KEYWORD(const) \
    KEYWORD(extern) \
    KEYWORD(inline) \
    KEYWORD(true) \
    KEYWORD(false) \
    KEYWORD(null) \
    SPECIAL(IDENTIFIER) \
    SPECIAL(STRING) \
    SPECIAL(INTEGER) \
    SPECIAL(FLOAT) \
    SYMBOL("(", PARENTHESIS_OPEN) \
    SYMBOL(")", PARENTHESIS_CLOSE) \
    SYMBOL("[", BRACKET_OPEN) \
    SYMBOL("]", BRACKET_CLOSE) \
    SYMBOL("{", BRACE_OPEN) \
    SYMBOL("}", BRACE_CLOSE) \
    SYMBOL("->", MINUS_ARROW) \
    SYMBOL("~>", TILDE_ARROW) \
    SYMBOL("=>", EQUALS_ARROW) \
    SYMBOL("<-", REVERSE_ARROW) \
    SYMBOL(",", COMMA) \
    SYMBOL(":", COLON) \
    SYMBOL(";", SEMICOLON) \
    SYMBOL(".", DOT) \
    SYMBOL("+", PLUS) \
    SYMBOL("-", MINUS) \
    SYMBOL("/", SLASH) \
    SYMBOL("%", PERCENT) \
    SYMBOL("*", ASTERISK) \
    SYMBOL("^", HAT) \
    SYMBOL("&", AMPERSAND) \
    SYMBOL("|", PIPE) \
    SYMBOL("?", QUESTION_MARK) \
    SYMBOL("++", DOUBLE_PLUS) \
    SYMBOL("--", DOUBLE_MINUS) \
    SYMBOL("^^", DOUBLE_HAT) \
    SYMBOL("&&", DOUBLE_AMPERSAND) \
    SYMBOL("||", DOUBLE_PIPE) \
    SYMBOL("==", DOUBLE_EQUALS) \
    SYMBOL("!=", NOT_EQUALS) \
    SYMBOL("<", LESS_THAN) \
    SYMBOL(">", GREATER_THAN) \
    SYMBOL("<=", LESS_THAN_EQUALS) \
    SYMBOL(">=", GREATER_THAN_EQUALS) \
    SYMBOL("<<", DOUBLE_LESS_THAN) \
    SYMBOL(">>", DOUBLE_GREATER_THAN) \
    SYMBOL("=", EQUALS) \
    SYMBOL("+=", PLUS_EQUALS) \
    SYMBOL("-=", MINUS_EQUALS) \
    SYMBOL("*=", ASTERISK_EQUALS) \
    SYMBOL("/=", SLASH_EQUALS) \
    SYMBOL("%=", PERCENT_EQUALS) \
    SYMBOL("&=", AMPERSAND_EQUALS) \
    SYMBOL("|=", PIPE_EQUALS) \
    SYMBOL("^=", HAT_EQUALS) \
    SYMBOL("^^=", DOUBLE_HAT_EQUALS) \
    SYMBOL("<<=", DOUBLE_LESS_THAN_EQUALS) \
    SYMBOL(">>=", DOUBLE_GREATER_THAN_EQUALS) \
    SYMBOL("?=", QUESTION_MARK_EQUALS) \
    SYMBOL("~", TILDE) \
    SYMBOL("!", EXCLAMATION_MARK) \
    SYMBOL("...", TRIPLE_DOT) \
    SYMBOL("@", AT) \
    SYMBOL("+@", PLUS_AT) \
    SYMBOL("?~", QUESTION_MARK_TILDE) \
    SYMBOL("#", HASHTAG) \
    SYMBOL("$", DOLLAR) \
    SYMBOL("::", DOUBLE_COLON) \

enum TokenKind: uint8_t {
    PROCESS_TOKENS(ENUM)
};

const char* token_table[] = {
    PROCESS_TOKENS(DECL)
};
int num_token_table_entries = sizeof(token_table) / sizeof(*token_table);

struct Token {
    int row, col;
    TokenKind type;
    char* filename;
    union {
        char* string;
        uint64_t integer;
        double floating;
    } value;
};

struct TokenQueue: Queue<Token*> {
    Token* push(char* filename, int row, int col) {
        Token* token = alloc->malloc<Token>();
        token->row = row;
        token->col = col;
        token->filename = filename;
        Queue::push(token);
        return token;
    }
    Token* expect(TokenKind type) {
        if (size == 0) return NULL;
        if (peek()->type == type) return pop();
        return NULL;
    }
};

Error* Error::syntax(const char* filename, int row, int col, String str) {
    Error* err = alloc->malloc<Error>();
    err->scope = alloc->malloc<ErrorScope>();
    err->scope->name = alloc->strdup("<syntax>");
    err->scope->file = alloc->strdup(filename);
    err->scope->row = row;
    err->scope->col = col;
    err->msg = alloc->strdup(str.data);
    return err;
}

Error* Error::parser(Token* token, String str) {
    return syntax(token->filename, token->row, token->col, str);
}

Error* Error::runtime(Context* context, String str) {
    Error* err = alloc->malloc<Error>();
    ErrorScope* last = NULL;
    for (int i = context->call_stack->size - 1; i >= 0; i--) {
        Scope* scope = context->call_stack->items[i];
        ErrorScope* errscope = alloc->malloc<ErrorScope>();
        if (!err->scope) err->scope = errscope;
        if (last) last->parent = errscope;
        errscope->row = scope->row;
        errscope->col = scope->col;
        errscope->name = scope->name;
        errscope->file = scope->file;
        last = errscope;
    }
    err->msg = alloc->strdup(str.data);
    context->state_var = Variable(context->type_cache->primitive(TypeKind_Void));
    return err;
}

static bool is_alphanumeric(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_numeric(char c) {
    return c >= '0' && c <= '9';
}

static bool is_symbol(char c) {
    return c > ' ' && c <= '~' && !is_alphanumeric(c);
}

static bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

static bool get_octal(char c, int* out) {
    if (c >= '0' && c <= '7') *out = c - '0';
    else return false;
    return true;
}

static bool get_decimal(char c, int* out) {
    if (c >= '0' && c <= '9') *out = c - '0';
    else return false;
    return true;
}

static bool get_hexadecimal(char c, int* out) {
    if      (c >= '0' && c <= '9') *out = c - '0';
    else if (c >= 'a' && c <= 'f') *out = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') *out = c - 'A' + 10;
    else return false;
    return true;
}

static void encode_utf8(String* str, uint32_t value) {
    if (value <= 0x7F) {
        str->add(value);
    }
    else if (value <= 0x7FF) {
        str->add(0xC0 | (value >> 6));
        str->add(0x80 | (value & 0x3F));
    }
    else if (value <= 0xFFFF) {
        str->add(0xE0 | (value >> 12));
        str->add(0x80 | ((value >> 6) & 0x3F));
        str->add(0x80 | (value & 0x3F));
    }
    else if (value <= 0x10FFFF) {
        str->add(0xF0 | (value >> 18));
        str->add(0x80 | ((value >> 12) & 0x3F));
        str->add(0x80 | ((value >> 6) & 0x3F));
        str->add(0x80 | (value & 0x3F));
    }
}

static bool try_parse_int(String* string, uint64_t* out) {
    int base = 10;
    int ptr = 0;
    if (string->data[0] == '0') {
        ptr = 2;
        switch (string->data[1]) {
            case 0:             *out = 0;  return true;
            case 'x': case 'X': base = 16; break;
            case 'b': case 'B': base = 2;  break;
            case '0'...'7':     base = 8; ptr = 1; break;
            default: return false;
        }
    }
    if (string->data[ptr] == 0) return false;
    bool suffix_end = false;
    *out = 0;
    for (; string->data[ptr]; ptr++) {
        int digit;
        if (suffix_end) return false;
        if (!get_hexadecimal(string->data[ptr], &digit)) return false;
        if (digit >= base) return false;
        *out = *out * base + digit;
    }
    return true;
}

static bool try_parse_flt(String* string, double* out) {
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
    if (string->data[0] == '0' && (string->data[1] == 'x' || string->data[1] == 'X')) {
        ptr = 2;
        base = 16;
        hex = true;
    }
    if (string->data[ptr] == 0) return false;
    *out = 0;
    for (; string->data[ptr]; ptr++) {
        int digit = 0;
        bool valid = get_hexadecimal(string->data[ptr], &digit) && digit < base;
        switch (state) {
            case Integer:
                if (string->data[ptr] == '.') state = Fraction;
                else if (((string->data[ptr] == 'e' || string->data[ptr] == 'E') && !hex) || ((string->data[ptr] == 'p' || string->data[ptr] == 'P') && hex)) {
                    state = ExponentFirstChar;
                    base = 10;
                }
                else if (valid) *out = *out * base + digit;
                else return false;
                break;
            case Fraction:
                if (((string->data[ptr] == 'e' || string->data[ptr] == 'E') && !hex) || ((string->data[ptr] == 'p' || string->data[ptr] == 'P') && hex)) {
                    state = ExponentFirstChar;
                    base = 10;
                }
                else if (valid) *out += digit * pow(base, -(++frac));
                else return false;
                break;
            case Exponent:
            case ExponentFirstChar:
            case ExponentFirstCharNoSign:
                if ((string->data[ptr] == '-' || string->data[ptr] == '+') && state == ExponentFirstChar) {
                    neg_exp = string->data[ptr] == '-';
                    state = ExponentFirstCharNoSign;
                    continue;
                }
                if (!get_decimal(string->data[ptr], &digit)) return false;
                if (valid) exp = exp * base + digit;
                else return false;
                state = Exponent;
        }
    }
    *out *= pow(hex ? 2 : 10, neg_exp ? -exp : exp);
    return (!hex && (state == Integer || state == Fraction || state == Exponent)) || (hex && state == Exponent);
}

static char* append_string(Context* context, const char* str) {
    char** ptr = (char**)bsearch(&str, context->strings->items, context->strings->size, sizeof(char*), compare_strings);
    if (ptr) return *ptr;
    char* string = alloc->strdup(str);
    context->strings->add(string);
    qsort(context->strings->items, context->strings->size, sizeof(char*), compare_strings);
    return string;
}

static TokenQueue* lex(Context* context, const char* code, const char* filename) {
    char c;
    size_t ptr = 0;
    bool no_increment = false;
    bool zero = false;
    int digit = 0;
    int row = 1, col = 0;
    char* file = append_string(context, filename);
    TokenQueue* tokens = new TokenQueue;
    struct {
        enum State {
            Idle,
            ParsingWord,
            ParsingSymbol,
            ParsingStringLiteral,
            ParsingCharLiteral,
        } parse_state;
        String* buffer;
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
    String str;
    state.buffer = &str;
    while (ptr == 0 || code[ptr - 1]) {
        c = code[no_increment ? ptr - 1 : ptr++];
        if (c == 0) c = '\n'; // basically inserts a new line at the end of files
        if (!no_increment) {
            col++;
            if (c == '\n') {
                col = 0;
                row++;
            }
        }
        no_increment = false;
        if (state.parse_state == state.Idle) {
            if      (c == '"' )                  state.parse_state = state.ParsingStringLiteral;
            else if (c == '\'')                  state.parse_state = state.ParsingCharLiteral;
            else if (is_alphanumeric(c) || (c == '.' && is_numeric(code[ptr]))) state.parse_state = state.ParsingWord;
            else if (is_symbol      (c)) state.parse_state = state.ParsingSymbol;
            else if (is_whitespace  (c)) continue;
            else throw Error::syntax(file, row, col, String::new_format("Invalid codepoint: \\x%02x", c));
            state.buffer->clear();
            state.row = row;
            state.col = col;
            state.string_state.state = state.parse_state == state.ParsingCharLiteral ? state.string_state.CharStart : state.string_state.None;
            if (state.parse_state == state.ParsingWord || state.parse_state == state.ParsingSymbol) no_increment = true;
            if (state.parse_state == state.ParsingWord) state.word_state.first_char = true;
        }
        else if (state.parse_state == state.ParsingStringLiteral || state.parse_state == state.ParsingCharLiteral) {
            switch (state.string_state.state) {
                case state.string_state.CharStart:
                    if (c == '\'') throw Error::syntax(file, row, col, "Empty char literal");
                    state.string_state.state = state.string_state.None;
                case state.string_state.None:
                    if (c == '\\') state.string_state.state = state.string_state.Backslash;
                    else if (c == '"' && state.parse_state == state.ParsingStringLiteral) {
                        Token* token = tokens->push(file, state.row, state.col);
                        token->type = TOKEN_STRING;
                        token->value.string = append_string(context, state.buffer->data);
                        state.parse_state = state.Idle;
                    }
                    else if (c == '\'' && state.parse_state == state.ParsingCharLiteral) {
                        char* data = state.buffer->data;
                        Token* token = tokens->push(file, state.row, state.col);
                        token->type = TOKEN_INTEGER;
                        data += decode_utf8(state.buffer->data, (int*)&token->value.integer);
                        if (*data != 0) throw Error::syntax(file, row, col, "Multiple characters in char literal");
                        state.parse_state = state.Idle;
                    }
                    else state.buffer->add(c);
                    break;
                case state.string_state.Backslash:
                    state.string_state.num_digits = 0;
                    state.string_state.value = 0;
                    switch (c) {
                        case 'a':  c = '\a'; break;
                        case 'b':  c = '\b'; break;
                        case 'e':  c = '\e'; break;
                        case 'f':  c = '\f'; break;
                        case 'n':  c = '\n'; break;
                        case 'r':  c = '\r'; break;
                        case 't':  c = '\t'; break;
                        case 'v':  c = '\v'; break;
                        case '"':  c = '"';  break;
                        case '\\': c = '\\'; break;
                        case '\'': c = '\''; break;
                        case 'x':       state.string_state.state = state.string_state.Hex2;  break;
                        case 'u':       state.string_state.state = state.string_state.Hex4;  break;
                        case 'U':       state.string_state.state = state.string_state.Hex8;  break;
                        case '0'...'7': state.string_state.state = state.string_state.Octal; no_increment = true; break;
                        case '\n': c = '\n'; break;
                        default: throw Error::syntax(file, row, col, "Invalid escape code");
                    }
                    if (state.string_state.state == state.string_state.Backslash) {
                        state.buffer->add(c);
                        state.string_state.state = state.string_state.None;
                    }
                    break;
                case state.string_state.Octal:
                case state.string_state.Hex2:
                case state.string_state.Hex4:
                case state.string_state.Hex8:
                if ((state.string_state.state == state.string_state.Octal ? get_octal : get_hexadecimal)(c, &digit))
                    state.string_state.value = state.string_state.value * (state.string_state.state == state.string_state.Octal ? 8 : 16) + digit;
                else if (state.string_state.state == state.string_state.Octal) state.string_state.num_digits = 2;
                else throw Error::syntax(file, row, col, "Not a valid digit");
                state.string_state.num_digits++;
                if (state.string_state.num_digits == (
                    state.string_state.state == state.string_state.Octal ? 3 :
                    state.string_state.state == state.string_state.Hex2  ? 2 :
                    state.string_state.state == state.string_state.Hex4  ? 4 :
                    state.string_state.state == state.string_state.Hex8  ? 8 : 1
                )) {
                    encode_utf8(state.buffer, state.string_state.value);
                    state.string_state.state = state.string_state.None;
                }
                break;
            }
        }
        else if (state.parse_state == state.ParsingWord) {
            if (state.word_state.first_char && (is_numeric(c) || c == '.')) {
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
            if (is_alphanumeric(c) || (c == '.' && state.word_state.dot_in_word) || ((c == '+' || c == '-') && state.word_state.can_be_sign)) {
                if (c == '.') state.word_state.dot_in_word = false;
                if (state.word_state.can_be_sign && !just_set) state.word_state.can_be_sign = false;
                state.buffer->add(c);
            }
            else {
                Token* token = tokens->push(file, state.row, state.col);
                if      (try_parse_int(state.buffer, &token->value.integer))  token->type = TOKEN_INTEGER;
                else if (try_parse_flt(state.buffer, &token->value.floating)) token->type = TOKEN_FLOAT;
                else {
                    token->type = TOKEN_IDENTIFIER;
                    for (int i = 0; i < num_token_table_entries; i++) {
                        if (!token_table[i]) continue;
                        if (strcmp(token_table[i], state.buffer->data) == 0) token->type = (TokenKind)i;
                    }
                    if (token->type == TOKEN_IDENTIFIER) {
                        Token* curr_token = token;
                        int ptr = 0;
                        char buf[state.buffer->length + 1];
                        for (int i = 0; i < state.buffer->length; i++) {
                            if (state.buffer->data[i] == '.') {
                                if (ptr != 0) {
                                    buf[ptr] = 0;
                                    if (!curr_token) curr_token = tokens->push(file, state.row, state.col + i - strlen(buf));
                                    curr_token->type = TOKEN_IDENTIFIER;
                                    curr_token->value.string = append_string(context, buf);
                                    curr_token = NULL;
                                }
                                if (!curr_token) curr_token = tokens->push(file, state.row, state.col + i);
                                curr_token->type = TOKEN_DOT;
                                curr_token = NULL;
                                ptr = 0;
                                continue;
                            }
                            buf[ptr++] = state.buffer->data[i];
                        }
                        if (ptr != 0) {
                            buf[ptr] = 0;
                            if (!curr_token) curr_token = tokens->push(file, state.row, state.col + state.buffer->length - strlen(buf));
                            curr_token->type = TOKEN_IDENTIFIER;
                            curr_token->value.string = append_string(context, buf);
                            curr_token = NULL;
                        }
                    }
                }
                state.parse_state = state.Idle;
                no_increment = true;
            }
        }
        else if (state.parse_state == state.ParsingSymbol) {
            int starts_with = 0;
            int exact_match = -1;
            for (int i = 0; i < num_token_table_entries; i++) {
                if (!token_table[i]) continue;
                if (strncmp(state.buffer->data, token_table[i], state.buffer->length) == 0) starts_with++;
                if (strcmp(state.buffer->data, token_table[i]) == 0) exact_match = i;
            }
            if (exact_match != -1 && (starts_with == 1 || !is_symbol(c))) {
                Token* token = tokens->push(file, state.row, state.col);
                token->type = (TokenKind)exact_match;
                state.parse_state = state.Idle;
                no_increment = true;
                continue;
            }
            if (is_symbol(c) && starts_with != 0) {
                state.buffer->add(c);
                starts_with = 0;
                for (int i = 0; i < num_token_table_entries; i++) {
                    if (!token_table[i]) continue;
                    if (strncmp(state.buffer->data, token_table[i], state.buffer->length) == 0) starts_with++;
                }
                if (starts_with == 0 && exact_match != -1) {
                    Token* token = tokens->push(file, state.row, state.col);
                    token->type = (TokenKind)exact_match;
                    state.parse_state = state.Idle;
                    no_increment = true;
                }
                continue;
            }
            if (exact_match == -1) throw Error::syntax(filename, row, col - state.buffer->length, "Invalid token");
        }
    }
    Token* token = tokens->push(file, state.row, state.col);
    token->type = TOKEN_END_OF_FILE;
    return tokens;
}

// == PARSER ==

typedef enum: uint8_t {
    AST_END,

    // commands
    AST_IF,
    AST_ELSE,
    AST_WHILE,
    AST_FOR,
    AST_RETURN,
    AST_CONTINUE,
    AST_BREAK,
    AST_TRY,
    AST_THROW,
    AST_CODEBLOCK,
    AST_EXPR,

    // suffix unary operators
    AST_CONST,
    AST_SUFFIX_INCREMENT,
    AST_SUFFIX_DECREMENT,
    AST_POINTER,
    AST_ARRAY,
    AST_CALL,
    AST_FUNCTION,
    AST_GET_SIZE,
    AST_GET_LENGTH,
    AST_GET_SCOPE,
    AST_WALK_STRUCT,

    // prefix unary operators
    AST_PREFIX_INCREMENT,
    AST_PREFIX_DECREMENT,
    AST_ADDRESS,
    AST_DEREFERENCE,
    AST_ARITH_PLUS,
    AST_ARITH_NEGATE,
    AST_LOGIC_NEGATE,
    AST_BINARY_NEGATE,

    // operands
    AST_INTEGER,
    AST_FLOAT,
    AST_STRING,
    AST_TYPE,
    AST_TRUTHY,
    AST_NULL,
    AST_VARIABLE,
    AST_PAREN,
    AST_DEFER,
    AST_VARARGS,
    AST_SIZEOF,
    AST_TYPEOF,
    AST_SCOPEOF,
    AST_NEW,
    AST_DELETE,
    AST_MOVE,
    AST_TERNARY,
    AST_DECL,
    AST_INCLUDE,

    // operators
    AST_POWER,
    AST_MULTIPLICATION,
    AST_DIVISION,
    AST_MODULO,
    AST_ADDITION,
    AST_SUBTRACTION,
    AST_BITSHIFT_LEFT,
    AST_BITSHIFT_RIGHT,
    AST_ELVIS,
    AST_LESS_THAN,
    AST_GREATER_THAN,
    AST_LESS_THAN_OR_EQUAL_TO,
    AST_GREATER_THAN_OR_EQUAL_TO,
    AST_EQUALS,
    AST_NOT_EQUALS,
    AST_BITWISE_AND,
    AST_BITWISE_OR,
    AST_BITWISE_XOR,
    AST_LOGICAL_AND,
    AST_LOGICAL_OR,
    AST_ASSIGN,
    AST_ADD_ASSIGN,
    AST_SUBTRACT_ASSIGN,
    AST_MULTIPLY_ASSIGN,
    AST_DIVIDE_ASSIGN,
    AST_POWER_ASSIGN,
    AST_MODULO_ASSIGN,
    AST_BITSHIFT_LEFT_ASSIGN,
    AST_BITSHIFT_RIGHT_ASSIGN,
    AST_BITWISE_AND_ASSIGN,
    AST_BITWISE_OR_ASSIGN,
    AST_BITWISE_XOR_ASSIGN,
    AST_ELVIS_ASSIGN,
    AST_CAST,
    AST_BITCAST,

    AST_COUNT,
} AST_Node;

enum AllocType: uint8_t {
    AllocType_None,
    AllocType_Scalar,
    AllocType_Struct,
    AllocType_Function,
    AllocType_Array,
};

static bool parse_expression(Context* context, ByteWriter* buf, TokenQueue* tokens, bool operand_only = false);
static void parse_command(Context* context, ByteWriter* buf, TokenQueue* tokens);
static void parse_codeblock(Context* context, ByteWriter* buf, TokenQueue* tokens, Token* start);

static void parse_operand(Context* context, ByteWriter* buf, TokenQueue* tokens) {
    Stack<ByteWriter*>* prefix_stack = new Stack<ByteWriter*>;
    Token* token = NULL;
    while (true) {
        ByteWriter* prefix = new ByteWriter;
        if      ((token = tokens->expect(TOKEN_DOUBLE_PLUS)))      prefix->write(AST_PREFIX_INCREMENT)->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if ((token = tokens->expect(TOKEN_DOUBLE_MINUS)))     prefix->write(AST_PREFIX_DECREMENT)->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if ((token = tokens->expect(TOKEN_DOLLAR)))           prefix->write(AST_ADDRESS         )->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if ((token = tokens->expect(TOKEN_HASHTAG)))          prefix->write(AST_DEREFERENCE     )->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if ((token = tokens->expect(TOKEN_PLUS)))             prefix->write(AST_ARITH_PLUS      )->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if ((token = tokens->expect(TOKEN_MINUS)))            prefix->write(AST_ARITH_NEGATE    )->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if ((token = tokens->expect(TOKEN_EXCLAMATION_MARK))) prefix->write(AST_LOGIC_NEGATE    )->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if ((token = tokens->expect(TOKEN_TILDE)))            prefix->write(AST_BINARY_NEGATE   )->write<int32_t>(token->row)->write<int32_t>(token->col);
        else {
            delete prefix;
            break;
        }
        prefix_stack->push(prefix);
    }
    if ((token = tokens->expect(TOKEN_INTEGER))) {
        buf->write(AST_INTEGER)->write<int32_t>(token->row)->write<int32_t>(token->col);
        buf->write(token->value.integer);
    }
    else if ((token = tokens->expect(TOKEN_FLOAT))) {
        buf->write(AST_FLOAT)->write<int32_t>(token->row)->write<int32_t>(token->col);
        buf->write(token->value.floating);
    }
    else if ((token = tokens->expect(TOKEN_STRING))) {
        buf->write(AST_STRING)->write<int32_t>(token->row)->write<int32_t>(token->col);
        buf->write(token->value.string);
    }
    else if ((token = tokens->expect(TOKEN_IDENTIFIER)) || (token = tokens->expect(TOKEN_this))) {
        buf->write(AST_VARIABLE)->write<int32_t>(token->row)->write<int32_t>(token->col);
        buf->write(token->type == TOKEN_IDENTIFIER ? token->value.string : "this");
    }
    else if (
        (token = tokens->expect(TOKEN_true)) ||
        (token = tokens->expect(TOKEN_false))
    ) {
        buf->write(AST_TRUTHY)->write<int32_t>(token->row)->write<int32_t>(token->col);
        buf->write(token->type == TOKEN_true);
    }
    else if ((token = tokens->expect(TOKEN_null))) {
        buf->write(AST_NULL)->write<int32_t>(token->row)->write<int32_t>(token->col);
    }
    else if ((token = tokens->expect(TOKEN_PARENTHESIS_OPEN))) {
        buf->write(AST_PAREN)->write<int32_t>(token->row)->write<int32_t>(token->col);
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
    }
    else if ((token = tokens->expect(TOKEN_defer))) {
        buf->write(AST_DEFER)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_PARENTHESIS_OPEN)) throw Error::parser(tokens->pop(), "Expected '('");
        if (!(token = tokens->expect(TOKEN_IDENTIFIER))) throw Error::parser(tokens->pop(), "Expected identifier");
        buf->write(token->value.string);
        if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
    }
    else if ((token = tokens->expect(TOKEN_TRIPLE_DOT))) {
        buf->write(AST_VARARGS)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_BRACKET_OPEN)) throw Error::parser(tokens->pop(), "Expected '['");
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
    }
    else if ((token = tokens->expect(TOKEN_sizeof))) {
        buf->write(AST_SIZEOF)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_PARENTHESIS_OPEN)) throw Error::parser(tokens->pop(), "Expected '('");
        if (tokens->expect(TOKEN_TRIPLE_DOT)) buf->write(true);
        else {
            buf->write(false);
            parse_expression(context, buf, tokens);
        }
        if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
    }
    else if ((token = tokens->expect(TOKEN_typeof))) {
        buf->write(AST_TYPEOF)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_PARENTHESIS_OPEN)) throw Error::parser(tokens->pop(), "Expected '('");
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
    }
    else if ((token = tokens->expect(TOKEN_delete))) {
        buf->write(AST_DELETE)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_PARENTHESIS_OPEN)) throw Error::parser(tokens->pop(), "Expected '('");
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
    }
    else if ((token = tokens->expect(TOKEN_scopeof))) {
        buf->write(AST_SCOPEOF)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_PARENTHESIS_OPEN)) throw Error::parser(tokens->pop(), "Expected '('");
        if (tokens->expect(TOKEN_this)) buf->write(false);
        else buf->write(true)->write(token->value.string);
        if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
    }
    else if ((token = tokens->expect(TOKEN_new))) {
        buf->write(AST_NEW)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (tokens->expect(TOKEN_scoped)) buf->write(true);
        else buf->write(false);
        if (!tokens->expect(TOKEN_BRACKET_OPEN)) throw Error::parser(tokens->pop(), "Expected '['");
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
        if (tokens->expect(TOKEN_PARENTHESIS_OPEN)) {
            buf->write(AllocType_Array);
            parse_expression(context, buf, tokens);
            if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
            if (tokens->expect(TOKEN_BRACE_OPEN)) while (!tokens->expect(TOKEN_BRACE_CLOSE)) {
                parse_expression(context, buf, tokens);
                if (tokens->expect(TOKEN_BRACE_CLOSE)) break;
                if (tokens->expect(TOKEN_COMMA)) continue;
                throw Error::parser(tokens->pop(), "Expected ',' or '}'");
            }
            buf->write(AST_END);
        }
        else {
            if (tokens->expect(TOKEN_EQUALS_ARROW)) {
                buf->write(AllocType_Function);
                if (tokens->expect(TOKEN_BRACKET_OPEN)) {
                    if (tokens->expect(TOKEN_EQUALS)) buf->write(CaptureMode_CopyPerCall);
                    else if (tokens->expect(TOKEN_TILDE)) buf->write(CaptureMode_CopyOnce);
                    else if (tokens->expect(TOKEN_DOLLAR)) buf->write(CaptureMode_Shared);
                    else throw Error::parser(tokens->pop(), "Expected '=', `~` or '$'");
                    if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
                }
                else buf->write(CaptureMode_None);
                if (!tokens->expect(TOKEN_BRACE_OPEN)) throw Error::parser(tokens->pop(), "Expected '{'");
                buf->push();
                while (!tokens->expect(TOKEN_BRACE_CLOSE)) parse_command(context, buf, tokens);
                buf->write(AST_END);
                buf->pop();
            }
            else if (tokens->expect(TOKEN_BRACE_OPEN)) {
                if (tokens->expect(TOKEN_BRACE_CLOSE)) buf->write(AllocType_None);
                else if (tokens->expect(TOKEN_DOT)) {
                    buf->write(AllocType_Struct);
                    while (true) {
                        buf->write(true);
                        if ((token = tokens->expect(TOKEN_IDENTIFIER))) buf->write(token->value.string);
                        else throw Error::parser(tokens->pop(), "Expected identifier");
                        if (!tokens->expect(TOKEN_EQUALS)) throw Error::parser(tokens->pop(), "Expected '='");
                        parse_expression(context, buf, tokens);
                        if (tokens->expect(TOKEN_COMMA)) {
                            if (tokens->expect(TOKEN_BRACE_CLOSE)) break;
                            if (tokens->expect(TOKEN_DOT)) continue;
                            throw Error::parser(tokens->pop(), "Expected '.'");
                        }
                        else if (tokens->expect(TOKEN_BRACE_CLOSE)) break;
                        else throw Error::parser(tokens->pop(), "Expected ',' or '}'");
                    }
                    buf->write(false);
                }
                else {
                    buf->write(AllocType_Scalar);
                    parse_expression(context, buf, tokens);
                    if (!tokens->expect(TOKEN_BRACE_CLOSE)) throw Error::parser(tokens->pop(), "Expected '}'");
                }
            }
            else buf->write(AllocType_None);
        }
    }
    else if ((token = tokens->expect(TOKEN_move))) {
        buf->write(AST_MOVE)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_PARENTHESIS_OPEN)) throw Error::parser(tokens->pop(), "Expected '('");
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) throw Error::parser(tokens->pop(), "Expected ')'");
        if (!tokens->expect(TOKEN_EQUALS_ARROW)) throw Error::parser(tokens->pop(), "Expected '=>'");
        if (!tokens->expect(TOKEN_BRACKET_OPEN)) throw Error::parser(tokens->pop(), "Expected '['");
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
    }
    else if ((token = tokens->expect(TOKEN_if))) {
        buf->write(AST_TERNARY)->write<int32_t>(token->row)->write<int32_t>(token->col);
        parse_expression(context, buf, tokens);
        if (!tokens->expect(TOKEN_EQUALS_ARROW)) throw Error::parser(tokens->pop(), "Expected '=>'");
        if (!tokens->expect(TOKEN_BRACKET_OPEN)) throw Error::parser(tokens->pop(), "Expected '['");
        buf->push();
        parse_expression(context, buf, tokens);
        buf->pop();
        if (!tokens->expect(TOKEN_SEMICOLON)) throw Error::parser(tokens->pop(), "Expected ';'");
        buf->push();
        parse_expression(context, buf, tokens);
        buf->pop();
        if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
    }
    else if ((token = tokens->expect(TOKEN_include))) {
        buf->write(AST_INCLUDE)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!(token = tokens->expect(TOKEN_STRING))) throw Error::parser(tokens->pop(), "Expected a string literal");
        buf->write(token->value.string);
    }
    else {
        bool parsed = false;
        buf->write(AST_TYPE)->write<int32_t>(tokens->peek()->row)->write<int32_t>(tokens->peek()->col);
        if (tokens->expect(TOKEN_const)) {
            buf->write(true);
            parsed = true;
        }
        else buf->write(false);
        if      (tokens->expect(TOKEN_s8))   { buf->write(TypeKind_Int8);    buf->write(false); }
        else if (tokens->expect(TOKEN_s16))  { buf->write(TypeKind_Int16);   buf->write(false); }
        else if (tokens->expect(TOKEN_s32))  { buf->write(TypeKind_Int32);   buf->write(false); }
        else if (tokens->expect(TOKEN_s64))  { buf->write(TypeKind_Int64);   buf->write(false); }
        else if (tokens->expect(TOKEN_u8))   { buf->write(TypeKind_Int8);    buf->write(true);  }
        else if (tokens->expect(TOKEN_u16))  { buf->write(TypeKind_Int16);   buf->write(true);  }
        else if (tokens->expect(TOKEN_u32))  { buf->write(TypeKind_Int32);   buf->write(true);  }
        else if (tokens->expect(TOKEN_u64))  { buf->write(TypeKind_Int64);   buf->write(true);  }
        else if (tokens->expect(TOKEN_f32))  { buf->write(TypeKind_Float32); buf->write(false); }
        else if (tokens->expect(TOKEN_f64))  { buf->write(TypeKind_Float64); buf->write(false); }
        else if (tokens->expect(TOKEN_void)) { buf->write(TypeKind_Void);    buf->write(false); }
        else if (tokens->expect(TOKEN_type)) { buf->write(TypeKind_Type);    buf->write(false); }
        else if (tokens->expect(TOKEN_bool)) { buf->write(TypeKind_Int8);    buf->write(true);  }
        else if (tokens->expect(TOKEN_struct)) {
            buf->write(TypeKind_Struct);
            if (tokens->expect(TOKEN_COLON)) {
                buf->write(true);
                parse_expression(context, buf, tokens, true);
            }
            else buf->write(false);
            if (!tokens->expect(TOKEN_BRACE_OPEN)) throw Error::parser(tokens->pop(), "Expected '{'");
            while (!tokens->expect(TOKEN_BRACE_CLOSE)) {
                buf->write(true)->write<int32_t>(tokens->peek()->row)->write<int32_t>(tokens->peek()->col);
                if (tokens->expect(TOKEN_BRACKET_OPEN)) {
                    buf->write(true);
                    if (!tokens->expect(TOKEN_BRACKET_CLOSE)) while (true) {
                        if ((token = tokens->expect(TOKEN_TRIPLE_DOT))) {
                            buf->write(AST_TYPE)->write<int32_t>(token->row)->write<int32_t>(token->col)->write(false)->write(TypeKind_Varargs)->write(false);
                            if (tokens->expect(TOKEN_BRACKET_CLOSE)) break;
                            throw Error::parser(tokens->pop(), "Expected ']'");
                            break;
                        }
                        parse_expression(context, buf, tokens, true);
                        if ((token = tokens->expect(TOKEN_IDENTIFIER))) {
                            buf->write(true);
                            buf->write(token->value.string);
                        }
                        else buf->write(false);
                        if (tokens->expect(TOKEN_COMMA)) continue;
                        if (tokens->expect(TOKEN_BRACKET_CLOSE)) break;
                        throw Error::parser(tokens->pop(), "Expected ',' or ']'");
                    }
                    buf->write(AST_END);
                    if (tokens->expect(TOKEN_DOLLAR)) buf->write(true);
                    else buf->write(false);
                    parse_expression(context, buf, tokens, true);
                    if (tokens->expect(TOKEN_BRACKET_OPEN)) {
                        if (tokens->expect(TOKEN_EQUALS)) buf->write(CaptureMode_CopyPerCall);
                        else if (tokens->expect(TOKEN_TILDE)) buf->write(CaptureMode_CopyOnce);
                        else if (tokens->expect(TOKEN_DOLLAR)) buf->write(CaptureMode_Shared);
                        else throw Error::parser(tokens->pop(), "Expected '=', `~` or '$'");
                        if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
                    }
                    else buf->write(CaptureMode_None);
                    buf->push();
                    if ((token = tokens->expect(TOKEN_BRACE_OPEN))) parse_codeblock(context, buf, tokens, token);
                    else throw Error::parser(tokens->pop(), "Expected '{'");
                    buf->pop();
                }
                else {
                    buf->write(false);
                    bool inlined = tokens->expect(TOKEN_inline);
                    buf->write(inlined);
                    parse_expression(context, buf, tokens, true);
                    bool mandatory_codeblock = false;
                    if ((token = tokens->expect(TOKEN_IDENTIFIER))) buf->write(true)->write(token->value.string);
                    else if (tokens->expect(TOKEN_new)) {
                        buf->write(true)->write("new");
                        mandatory_codeblock = true;
                    }
                    else if (tokens->expect(TOKEN_delete)) {
                        buf->write(true)->write("delete");
                        mandatory_codeblock = true;
                    }
                    else {
                        if (!inlined) throw Error::parser(tokens->pop(), "Expected 'new', 'delete' or identifier");
                        buf->write(false);
                    }
                    if (tokens->expect(TOKEN_EQUALS)) {
                        if (mandatory_codeblock) throw Error::parser(tokens->pop(), "Expected '{'");
                        if (inlined) throw Error::parser(tokens->pop(), "Cannot pre-assign to an inline field");
                        buf->write(true)->write(false);
                        parse_expression(context, buf, tokens);
                    }
                    else {
                        CaptureMode capture_mode = CaptureMode_None;
                        if (tokens->expect(TOKEN_BRACKET_OPEN)) {
                            if (tokens->expect(TOKEN_EQUALS)) capture_mode = CaptureMode_CopyPerCall;
                            else if (tokens->expect(TOKEN_TILDE)) capture_mode = CaptureMode_CopyOnce;
                            else if (tokens->expect(TOKEN_DOLLAR)) capture_mode = CaptureMode_Shared;
                            else throw Error::parser(tokens->pop(), "Expected '=', `~` or '$'");
                            if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
                        }
                        if ((token = tokens->expect(TOKEN_BRACE_OPEN))) {
                            if (inlined) throw Error::parser(tokens->pop(), "Cannot pre-assign to an inline field");
                            buf->write(true)->write(true)->write(capture_mode);
                            buf->push();
                            parse_codeblock(context, buf, tokens, token);
                            buf->pop();
                        }
                        else if (capture_mode != CaptureMode_None || mandatory_codeblock) throw Error::parser(tokens->pop(), "Expected '{'");
                        else buf->write(false);
                    }
                }
                if (tokens->expect(TOKEN_AT)) {
                    buf->write(true)->write(false);
                    parse_expression(context, buf, tokens);
                }
                else if (tokens->expect(TOKEN_PLUS_AT)) {
                    buf->write(true)->write(true);
                    parse_expression(context, buf, tokens);
                }
                else buf->write(false);
                if (!tokens->expect(TOKEN_SEMICOLON)) throw Error::parser(tokens->pop(), "Expected ';'");
            }
            buf->write(false);
        }
        else throw Error::parser(tokens->pop(), parsed ? "Expected base type" : "Expected expression");
    }
    while (true) {
        if (
            (token = tokens->expect(TOKEN_DOUBLE_PLUS)) ||
            (token = tokens->expect(TOKEN_DOUBLE_MINUS)) ||
            (token = tokens->expect(TOKEN_HASHTAG)) ||
            (token = tokens->expect(TOKEN_const))
        ) buf->write(
            token->type == TOKEN_DOUBLE_PLUS  ? AST_SUFFIX_INCREMENT :
            token->type == TOKEN_DOUBLE_MINUS ? AST_SUFFIX_DECREMENT :
            token->type == TOKEN_HASHTAG      ? AST_POINTER          :
            token->type == TOKEN_const        ? AST_CONST            : AST_END
        )->write<int32_t>(token->row)->write<int32_t>(token->col);
        else if (
            (token = tokens->expect(TOKEN_BRACKET_OPEN)) ||
            (token = tokens->expect(TOKEN_PARENTHESIS_OPEN))
        ) {
            TokenKind end = token->type == TOKEN_BRACKET_OPEN ? TOKEN_BRACKET_CLOSE : TOKEN_PARENTHESIS_CLOSE;
            buf->write(token->type == TOKEN_BRACKET_OPEN ? AST_ARRAY : AST_CALL)->write<int32_t>(token->row)->write<int32_t>(token->col);
            if (!tokens->expect(end)) while (true) {
                parse_expression(context, buf, tokens);
                if (tokens->expect(TOKEN_COMMA)) continue;
                if (tokens->expect(end)) break;
                throw Error::parser(tokens->pop(), String::new_format("Expected ',' or '%s'", token_table[end]));
            }
            buf->write(AST_END);
        }
        else if ((token = tokens->expect(TOKEN_REVERSE_ARROW))) {
            buf->write(AST_FUNCTION)->write<int32_t>(token->row)->write<int32_t>(token->col);
            if (tokens->expect(TOKEN_DOLLAR)) buf->write(true);
            else buf->write(false);
            if (!tokens->expect(TOKEN_PARENTHESIS_OPEN)) throw Error::parser(tokens->pop(), "Expected '('");
            if (!tokens->expect(TOKEN_PARENTHESIS_CLOSE)) while (true) {
                if ((token = tokens->expect(TOKEN_TRIPLE_DOT))) {
                    buf->write(AST_TYPE)->write<int32_t>(token->row)->write<int32_t>(token->col)->write(false)->write(TypeKind_Varargs)->write(false);
                    buf->write(AST_END)->write(false);
                    if (tokens->expect(TOKEN_PARENTHESIS_CLOSE)) break;
                    throw Error::parser(tokens->pop(), "Expected ')'");
                    break;
                }
                parse_expression(context, buf, tokens, true);
                if ((token = tokens->expect(TOKEN_IDENTIFIER))) {
                    buf->write(true);
                    buf->write(token->value.string);
                }
                else buf->write(false);
                if (tokens->expect(TOKEN_COMMA)) continue;
                if (tokens->expect(TOKEN_PARENTHESIS_CLOSE)) break;
                throw Error::parser(tokens->pop(), "Expected ',' or ')'");
            }
            buf->write(AST_END);
        }
        else if ((token = tokens->expect(TOKEN_DOUBLE_COLON))) {
            Token* t = token;
            if (!(token = tokens->expect(TOKEN_IDENTIFIER))) throw Error::parser(tokens->pop(), "Expected identifier");
            else if (strcmp(token->value.string, "size") == 0) buf->write(AST_GET_SIZE)->write<int32_t>(t->row)->write<int32_t>(t->col);
            else if (strcmp(token->value.string, "length") == 0) buf->write(AST_GET_LENGTH)->write<int32_t>(t->row)->write<int32_t>(t->col);
            else if (strcmp(token->value.string, "scope") == 0) buf->write(AST_GET_SCOPE)->write<int32_t>(t->row)->write<int32_t>(t->col);
            else throw Error::parser(token, "Expected 'size', 'length' or 'scope'");
        }
        else if ((token = tokens->expect(TOKEN_DOT))) {
            buf->write(AST_WALK_STRUCT)->write<int32_t>(token->row)->write<int32_t>(token->col);
            if ((token = tokens->expect(TOKEN_IDENTIFIER))) buf->write(token->value.string);
            else throw Error::parser(tokens->pop(), "Expected identifier");
        }
        else break;
    }
    while (prefix_stack->size > 0) buf->merge(prefix_stack->pop());
    delete prefix_stack;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-designator"

static struct OperatorInfo {
    TokenKind token;
    enum: uint8_t {
        OpFmt_Prefix,
        OpFmt_Suffix,
        OpFmt_Binary,
        OpFmt_Special_Array,
        OpFmt_Special_Call,
        OpFmt_Special_Function,
        OpFmt_Special_GetSize,
        OpFmt_Special_GetLength,
        OpFmt_Special_GetScope,
    } format;
    bool right_associative;
    int precedence;
} operator_info[AST_COUNT] = {
    [AST_ADDITION]                 = { TOKEN_PLUS,                       OperatorInfo::OpFmt_Binary, false, 11 },
    [AST_SUBTRACTION]              = { TOKEN_MINUS,                      OperatorInfo::OpFmt_Binary, false, 11 },
    [AST_MULTIPLICATION]           = { TOKEN_ASTERISK,                   OperatorInfo::OpFmt_Binary, false, 12 },
    [AST_DIVISION]                 = { TOKEN_SLASH,                      OperatorInfo::OpFmt_Binary, false, 12 },
    [AST_MODULO]                   = { TOKEN_PERCENT,                    OperatorInfo::OpFmt_Binary, false, 12 },
    [AST_POWER]                    = { TOKEN_DOUBLE_HAT,                 OperatorInfo::OpFmt_Binary, false, 13 },
    [AST_BITSHIFT_LEFT]            = { TOKEN_DOUBLE_LESS_THAN,           OperatorInfo::OpFmt_Binary, false, 10 },
    [AST_BITSHIFT_RIGHT]           = { TOKEN_DOUBLE_GREATER_THAN,        OperatorInfo::OpFmt_Binary, false, 10 },
    [AST_BITWISE_AND]              = { TOKEN_AMPERSAND,                  OperatorInfo::OpFmt_Binary, false, 7  },
    [AST_BITWISE_OR]               = { TOKEN_PIPE,                       OperatorInfo::OpFmt_Binary, false, 5  },
    [AST_BITWISE_XOR]              = { TOKEN_HAT,                        OperatorInfo::OpFmt_Binary, false, 6  },
    [AST_ELVIS]                    = { TOKEN_QUESTION_MARK,              OperatorInfo::OpFmt_Binary, false, 2  },
    [AST_EQUALS]                   = { TOKEN_DOUBLE_EQUALS,              OperatorInfo::OpFmt_Binary, false, 8  },
    [AST_NOT_EQUALS]               = { TOKEN_NOT_EQUALS,                 OperatorInfo::OpFmt_Binary, false, 8  },
    [AST_LESS_THAN]                = { TOKEN_LESS_THAN,                  OperatorInfo::OpFmt_Binary, false, 9  },
    [AST_GREATER_THAN]             = { TOKEN_GREATER_THAN,               OperatorInfo::OpFmt_Binary, false, 9  },
    [AST_LESS_THAN_OR_EQUAL_TO]    = { TOKEN_LESS_THAN_EQUALS,           OperatorInfo::OpFmt_Binary, false, 9  },
    [AST_GREATER_THAN_OR_EQUAL_TO] = { TOKEN_GREATER_THAN_EQUALS,        OperatorInfo::OpFmt_Binary, false, 9  },
    [AST_LOGICAL_AND]              = { TOKEN_DOUBLE_AMPERSAND,           OperatorInfo::OpFmt_Binary, false, 4  },
    [AST_LOGICAL_OR]               = { TOKEN_DOUBLE_PIPE,                OperatorInfo::OpFmt_Binary, false, 3  },
    [AST_ASSIGN]                   = { TOKEN_EQUALS,                     OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_ADD_ASSIGN]               = { TOKEN_PLUS_EQUALS,                OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_SUBTRACT_ASSIGN]          = { TOKEN_MINUS_EQUALS,               OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_MULTIPLY_ASSIGN]          = { TOKEN_ASTERISK_EQUALS,            OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_DIVIDE_ASSIGN]            = { TOKEN_SLASH_EQUALS,               OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_MODULO_ASSIGN]            = { TOKEN_PERCENT_EQUALS,             OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_POWER_ASSIGN]             = { TOKEN_DOUBLE_HAT_EQUALS,          OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_BITSHIFT_LEFT_ASSIGN]     = { TOKEN_DOUBLE_LESS_THAN_EQUALS,    OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_BITSHIFT_RIGHT_ASSIGN]    = { TOKEN_DOUBLE_GREATER_THAN_EQUALS, OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_BITWISE_AND_ASSIGN]       = { TOKEN_AMPERSAND_EQUALS,           OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_BITWISE_OR_ASSIGN]        = { TOKEN_PIPE_EQUALS,                OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_BITWISE_XOR_ASSIGN]       = { TOKEN_HAT_EQUALS,                 OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_ELVIS_ASSIGN]             = { TOKEN_QUESTION_MARK_EQUALS,       OperatorInfo::OpFmt_Binary, true,  1  },
    [AST_CAST]                     = { TOKEN_MINUS_ARROW,                OperatorInfo::OpFmt_Binary, false, 14 },
    [AST_BITCAST]                  = { TOKEN_TILDE_ARROW,                OperatorInfo::OpFmt_Binary, false, 14 },

    [AST_PREFIX_INCREMENT] = { TOKEN_DOUBLE_PLUS,      OperatorInfo::OpFmt_Prefix },
    [AST_PREFIX_DECREMENT] = { TOKEN_DOUBLE_MINUS,     OperatorInfo::OpFmt_Prefix },
    [AST_ADDRESS]          = { TOKEN_DOLLAR,           OperatorInfo::OpFmt_Prefix },
    [AST_DEREFERENCE]      = { TOKEN_HASHTAG,          OperatorInfo::OpFmt_Prefix },
    [AST_ARITH_PLUS]       = { TOKEN_PLUS,             OperatorInfo::OpFmt_Prefix },
    [AST_ARITH_NEGATE]     = { TOKEN_MINUS,            OperatorInfo::OpFmt_Prefix },
    [AST_LOGIC_NEGATE]     = { TOKEN_EXCLAMATION_MARK, OperatorInfo::OpFmt_Prefix },
    [AST_BINARY_NEGATE]    = { TOKEN_TILDE,            OperatorInfo::OpFmt_Prefix },

    [AST_SUFFIX_INCREMENT] = { TOKEN_DOUBLE_PLUS,       OperatorInfo::OpFmt_Suffix },
    [AST_SUFFIX_DECREMENT] = { TOKEN_DOUBLE_MINUS,      OperatorInfo::OpFmt_Suffix },
    [AST_POINTER]          = { TOKEN_HASHTAG,           OperatorInfo::OpFmt_Suffix },
    [AST_ARRAY]            = { TOKEN_BRACKET_OPEN,      OperatorInfo::OpFmt_Special_Array },
    [AST_CALL]             = { TOKEN_PARENTHESIS_OPEN,  OperatorInfo::OpFmt_Special_Call },
    [AST_FUNCTION]         = { TOKEN_REVERSE_ARROW,     OperatorInfo::OpFmt_Special_Function },
    [AST_GET_SIZE]         = { TOKEN_DOUBLE_COLON,      OperatorInfo::OpFmt_Special_GetSize },
    [AST_GET_LENGTH]       = { TOKEN_DOUBLE_COLON,      OperatorInfo::OpFmt_Special_GetLength },
    [AST_GET_SCOPE]        = { TOKEN_DOUBLE_COLON,      OperatorInfo::OpFmt_Special_GetScope },
    [AST_WALK_STRUCT]      = { TOKEN_DOT,               OperatorInfo::OpFmt_Suffix },
};

#pragma clang diagnostic pop

static void infix_to_postfix(ByteWriter* outbuf, List<ByteWriter*>* list) {
    Stack<ByteWriter*>* op_stack = new Stack<ByteWriter*>;
    for (int i = 0; i < list->size; i++) {
        if (i % 2 == 0) outbuf->merge(list->items[i]);
        else {
            AST_Node op = (AST_Node)list->items[i]->bytes[0];
            while (op_stack->size > 0) {
                AST_Node top = (AST_Node)op_stack->peek()->bytes[0];
                bool should_pop = (
                    (!operator_info[op].right_associative && operator_info[op].precedence <= operator_info[top].precedence) ||
                    ( operator_info[op].right_associative && operator_info[op].precedence <  operator_info[top].precedence)
                );
                if (should_pop) outbuf->merge(op_stack->pop());
                else break;
            }
            op_stack->push(list->items[i]);
        }
    }
    while (op_stack->size > 0) outbuf->merge(op_stack->pop());
    delete op_stack;
    delete list;
}

static bool parse_expression(Context* context, ByteWriter* buf, TokenQueue* tokens, bool operand_only) {
    List<ByteWriter*>* buffers = new List<ByteWriter*>;
    bool require_semicolon = true;
    while (true) {
        ByteWriter* buffer = new ByteWriter;
        Token* extern_token = operand_only ? NULL : tokens->expect(TOKEN_extern);
        Token* token = NULL;
        parse_operand(context, buffer, tokens);
        buffers->add(buffer);
        if (operand_only) break;
        if ((token = tokens->expect(TOKEN_IDENTIFIER))) {
            buffer->write(AST_DECL)->write<int32_t>(token->row)->write<int32_t>(token->col);
            buffer->write(extern_token != NULL);
            buffer->write(token->value.string);
            CaptureMode capture_mode = CaptureMode_None;
            if (tokens->expect(TOKEN_PARENTHESIS_OPEN)) while (true) {
                buffer->write(true);
                if (!(token = tokens->expect(TOKEN_IDENTIFIER))) throw Error::parser(tokens->pop(), "Expected identifier");
                buffer->write(token->value.string);
                if (!tokens->expect(TOKEN_EQUALS)) throw Error::parser(tokens->pop(), "Expected '='");
                parse_expression(context, buffer, tokens);
                if (tokens->expect(TOKEN_COMMA)) continue;
                if (tokens->expect(TOKEN_PARENTHESIS_CLOSE)) break;
                throw Error::parser(tokens->pop(), "Expected ',' or ')'");
            }
            buffer->write(false);
            if (tokens->expect(TOKEN_BRACKET_OPEN)) {
                if (tokens->expect(TOKEN_EQUALS)) capture_mode = CaptureMode_CopyPerCall;
                else if (tokens->expect(TOKEN_TILDE)) capture_mode = CaptureMode_CopyOnce;
                else if (tokens->expect(TOKEN_DOLLAR)) capture_mode = CaptureMode_Shared;
                else throw Error::parser(tokens->pop(), "Expected '=', `~` or '$'");
                if (!tokens->expect(TOKEN_BRACKET_CLOSE)) throw Error::parser(tokens->pop(), "Expected ']'");
            }
            if ((token = tokens->expect(TOKEN_BRACE_OPEN)) || (token = tokens->expect(TOKEN_EQUALS_ARROW))) {
                require_semicolon = false;
                buffer->write(true);
                buffer->write(capture_mode);
                buffer->push();
                parse_codeblock(context, buffer, tokens, token);
                buffer->pop();
            }
            else if (capture_mode != CaptureMode_None) throw Error::parser(tokens->pop(), "Expected '{'");
            else buffer->write(false);
            extern_token = NULL;
        }
        if (extern_token) throw Error::parser(extern_token, "Unexpected 'extern'");
        AST_Node node = AST_END;
        for (int i = 0; i < AST_COUNT; i++) {
            if (operator_info[i].token == TOKEN_END_OF_FILE) continue;
            if (operator_info[i].format != OperatorInfo::OpFmt_Binary) continue;
            if (!(token = tokens->expect(operator_info[i].token))) continue;
            node = (AST_Node)i;
            break;
        }
        if (node == AST_END) break;
        require_semicolon = true;
        buffer = new ByteWriter;
        buffer->write(node)->write<int32_t>(token->row)->write<int32_t>(token->col);
        buffers->add(buffer);
    }
    infix_to_postfix(buf, buffers);
    buf->write(AST_END);
    return require_semicolon;
}

static void parse_codeblock(Context* context, ByteWriter* buf, TokenQueue* tokens, Token* start) {
    if ((start && start->type == TOKEN_EQUALS_ARROW) || (!start && tokens->expect(TOKEN_EQUALS_ARROW))) parse_command(context, buf, tokens);
    else if ((start && start->type == TOKEN_BRACE_OPEN) || (!start && tokens->expect(TOKEN_BRACE_OPEN)))
        while (!tokens->expect(TOKEN_BRACE_CLOSE)) parse_command(context, buf, tokens);
    else throw Error::parser(tokens->pop(), "Expected '=>' or '{'");
    buf->write(AST_END);
}

static void parse_command(Context* context, ByteWriter* buf, TokenQueue* tokens) {
    Token* token = NULL;
    if ((token = tokens->expect(TOKEN_if))) while (true) {
        buf->write(AST_IF)->write<int32_t>(token->row)->write<int32_t>(token->col);
        parse_expression(context, buf, tokens);
        buf->push();
        parse_codeblock(context, buf, tokens, NULL);
        buf->pop();
        if (tokens->expect(TOKEN_else)) {
            buf->write(true);
            if (tokens->expect(TOKEN_if)) continue;
            else {
                buf->push();
                parse_codeblock(context, buf, tokens, NULL);
                buf->pop();
            }
        }
        else buf->write(false);
        break;
    }
    else if ((token = tokens->expect(TOKEN_while))) {
        buf->write(AST_WHILE)->write<int32_t>(token->row)->write<int32_t>(token->col);
        parse_expression(context, buf, tokens);
        buf->push();
        if (!tokens->expect(TOKEN_SEMICOLON)) parse_codeblock(context, buf, tokens, NULL);
        else buf->write(AST_END);
        buf->pop();
    }
    else if ((token = tokens->expect(TOKEN_for))) {
        buf->write(AST_FOR)->write<int32_t>(token->row)->write<int32_t>(token->col);
        parse_expression(context, buf, tokens, true);
        if ((token = tokens->expect(TOKEN_IDENTIFIER))) buf->write(token->value.string);
        else throw Error::parser(tokens->pop(), "Expected identifier");
        if (!tokens->expect(TOKEN_COLON)) throw Error::parser(tokens->pop(), "Expected ':'");
        parse_expression(context, buf, tokens);
        if (tokens->expect(TOKEN_excl)) buf->write(true);
        else if (tokens->expect(TOKEN_incl) || true) buf->write(false);
        if (!tokens->expect(TOKEN_EQUALS_ARROW)) throw Error::parser(tokens->pop(), "Expected '=>'");
        parse_expression(context, buf, tokens);
        if (tokens->expect(TOKEN_incl)) buf->write(false);
        else if (tokens->expect(TOKEN_incl) || true) buf->write(true);
        if (tokens->expect(TOKEN_step)) {
            buf->write(true);
            parse_expression(context, buf, tokens);
        }
        else buf->write(false);
        buf->push();
        parse_codeblock(context, buf, tokens, NULL);
        buf->pop();
    }
    else if ((token = tokens->expect(TOKEN_return))) {
        buf->write(AST_RETURN)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (tokens->expect(TOKEN_SEMICOLON)) buf->write(false);
        else {
            buf->write(true);
            parse_expression(context, buf, tokens);
            if (!tokens->expect(TOKEN_SEMICOLON)) throw Error::parser(tokens->pop(), "Expected ';'");
        }
    }
    else if ((token = tokens->expect(TOKEN_continue))) {
        buf->write(AST_CONTINUE)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_SEMICOLON)) throw Error::parser(tokens->pop(), "Expected ';'");
    }
    else if ((token = tokens->expect(TOKEN_break))) {
        buf->write(AST_BREAK)->write<int32_t>(token->row)->write<int32_t>(token->col);
        if (!tokens->expect(TOKEN_SEMICOLON)) throw Error::parser(tokens->pop(), "Expected ';'");
    }
    else if ((token = tokens->expect(TOKEN_try))) {
        buf->write(AST_TRY)->write<int32_t>(token->row)->write<int32_t>(token->col);
        buf->push();
        parse_codeblock(context, buf, tokens, NULL);
        buf->pop();
        if (tokens->expect(TOKEN_catch)) {
            buf->write(true);
            bool silently = tokens->expect(TOKEN_silently);
            buf->write(silently);
            if (tokens->expect(TOKEN_as)) {
                silently = false;
                buf->write(true);
                if ((token = tokens->expect(TOKEN_IDENTIFIER))) buf->write(token->value.string);
                else throw Error::parser(tokens->pop(), "Expected identifier");
            }
            else buf->write(false);
            if (silently && tokens->expect(TOKEN_SEMICOLON)) buf->push()->write(AST_END)->pop();
            else {
                buf->push();
                parse_codeblock(context, buf, tokens, NULL);
                buf->pop();
            }
        }
        else buf->write(false);
    }
    else if ((token = tokens->expect(TOKEN_throw))) {
        buf->write(AST_THROW)->write<int32_t>(token->row)->write<int32_t>(token->col);
        parse_expression(context, buf, tokens);
        if (tokens->expect(TOKEN_as)) {
            buf->write(true);
            if (!(token = tokens->expect(TOKEN_STRING))) throw Error::parser(tokens->pop(), "Expected a string literal");
            buf->write(token->value.string);
        }
        else buf->write(false);
        if (!tokens->expect(TOKEN_SEMICOLON)) throw Error::parser(tokens->pop(), "Expected ';'");
    }
    else if ((token = tokens->expect(TOKEN_EQUALS_ARROW)) || (token = tokens->expect(TOKEN_BRACE_OPEN))) {
        buf->write(AST_CODEBLOCK)->write<int32_t>(token->row)->write<int32_t>(token->col);
        parse_codeblock(context, buf, tokens, token);
    }
    else if (!(token = tokens->expect(TOKEN_SEMICOLON))) {
        buf->write(AST_EXPR)->write<int32_t>(tokens->peek()->row)->write<int32_t>(tokens->peek()->col);
        bool require_semicolon = parse_expression(context, buf, tokens);
        if (require_semicolon && !tokens->expect(TOKEN_SEMICOLON))
            throw Error::parser(tokens->pop(), "Expected ';'");
    }
}

// == INTERPRETER ==

static bool execute_operator(Context* context, ByteReader* reader, Stack<Variable>* stack, AST_Node node);
static Variable execute_expression_node(Context* context, ByteReader* reader, Stack<Variable>* stack = NULL);
static Variable execute_expression(Context* context, ByteReader* reader);
static Variable execute_codeblock(Context* context, ByteReader* reader, bool push_scope = true);
static Variable execute_command(Context* context, ByteReader* reader);
static Error* execute_file(Context* context, const char* filename);

API void pawscript_log_error(Error* error, FILE* f);
API void pawscript_destroy_error(Error* error);

enum VariableType: uint8_t {
    VarType_Integer,
    VarType_Number,
    VarType_Pointer,
    VarType_Struct,
    VarType_Function,
    VarType_Type,
    VarType_Any,

    VarType_Assignable = (1 << 7)
};

static bool is_truthy(Context* context, Variable* variable) {
    if (variable->type->kind == TypeKind_Type) return variable->as<Type*>()->resolve_defers(context)->kind;
    else if (variable->type->kind == TypeKind_Float32) return variable->as<float>();
    else if (variable->type->kind == TypeKind_Float64) return variable->as<double>();
    return variable->as<uint64_t>();
}

static bool matches(TypeKind kind, uint8_t type) {
    switch (type & ~VarType_Assignable) {
        case VarType_Number: if (
            kind == TypeKind_Float32 ||
            kind == TypeKind_Float64
        ) return true;
        case VarType_Integer: return
            kind == TypeKind_Int8  ||
            kind == TypeKind_Int16 ||
            kind == TypeKind_Int32 ||
            kind == TypeKind_Int64;
        case VarType_Pointer: return kind == TypeKind_Pointer;
        case VarType_Struct: return kind == TypeKind_Struct;
        case VarType_Function: return kind == TypeKind_Function;
        case VarType_Type: return kind == TypeKind_Type;
        case VarType_Any: return true;
    }
    return false;
}

static bool matches(Variable* variable, uint8_t type) {
    if ((type & VarType_Assignable) && !variable->ref) return false;
    return matches(variable->type->kind, type);
}

static Variable cast(Context* context, Type* type, Variable var, bool bitcast = false, bool implicit = true) {
    type = type->resolve_defers(context);
    Variable out(type);
    if (bitcast) out << var;
    else if (type->kind == TypeKind_Float32) {
        if      (type->kind == TypeKind_Float32) out.as<float>() = var.as<float>();
        else if (type->kind == TypeKind_Float64) out.as<float>() = var.as<double>();
        else {
            if (type->is_unsigned) out.as<float>() = out.as<uint64_t>();
            else out.as<float>() = out.as<int64_t>();
        }
    }
    else if (type->kind == TypeKind_Float64) {
        if      (type->kind == TypeKind_Float32) out.as<double>() = var.as<float>();
        else if (type->kind == TypeKind_Float64) out.as<double>() = var.as<double>();
        else {
            if (type->is_unsigned) out.as<double>() = out.as<uint64_t>();
            else out.as<double>() = out.as<int64_t>();
        }
    }
    else {
        uint64_t value = 0;
        if      (type->kind == TypeKind_Float32) out.as<uint64_t>() = var.as<float>();
        else if (type->kind == TypeKind_Float64) out.as<uint64_t>() = var.as<double>();
        else out << var;
    }
    return out;
}

static Type* promote(Context* context, Variable* a, Variable* b) {
    TypeKind kind = TypeKind_Int32;
    bool is_unsigned = a->type->is_unsigned || b->type->is_unsigned;
    if (
        a->type->kind == TypeKind_Int64 ||
        b->type->kind == TypeKind_Int64
    ) kind = TypeKind_Int64;
    if (
        a->type->kind == TypeKind_Float32 ||
        b->type->kind == TypeKind_Float32
    ) { kind = TypeKind_Float32; is_unsigned = false; }
    if (
        a->type->kind == TypeKind_Float64 ||
        b->type->kind == TypeKind_Float64
    ) { kind = TypeKind_Float64; is_unsigned = false; }
    if (
        a->type->kind == TypeKind_Pointer ||
        b->type->kind == TypeKind_Pointer
    ) { kind = TypeKind_Int64; is_unsigned = true; }
    Type* type = context->type_cache->primitive(kind);
    if (is_unsigned) type = type->unsign(context);
    *a = cast(context, type, *a);
    *b = cast(context, type, *b);
    return type;
}

static void execute_expressions(Context* context, ByteReader* reader, List<Variable>* params) {
    Variable var(NULL);
    while ((var = execute_expression(context, reader)).type) params->add(var);
}

static void execute_params(Context* context, ByteReader* reader, List<Type::Param>* params) {
    Variable var(NULL);
    while ((var = execute_expression(context, reader)).type) {
        Type::Param param;
        param.type = var.as<Type*>();
        if (reader->read<bool>()) param.name = reader->read<char*>();
        else param.name = NULL;
        params->add(param);
    }
}

static Variable execute_function(Context* context, Variable* function, List<Variable>* args) {
    if (function->type->kind != TypeKind_Function) throw Error::runtime(context, "Attempt to call a non-function value");
    Type::Param* params = function->type->function_info.params;
    size_t num_params = function->type->function_info.num_params;
    int varargs_index = num_params > 0 && params[num_params - 1].type->kind == TypeKind_Varargs ? num_params - 1 : -1;
    if (varargs_index == -1 && num_params != args->size) throw Error::runtime(context, String::new_format("Non-matching number of arguments (expected %d, got %d)", num_params, args->size));
    else if (args->size < varargs_index) throw Error::runtime(context, String::new_format("Non-matching number of arguments (expected >=%d, got %d)", varargs_index, args->size));
    for (int i = 0; i < num_params; i++) {
        if (params[i].type->kind == TypeKind_Varargs) break;
        args->get(i) = cast(context, params[i].type, args->get(i), false, true);
    }
    if (function->as<Function*>() == NULL) throw Error::runtime(context, "Calling an unset function");
    if (function->as<Function*>()->is_native()) {
        if (function->type->lvalue_return) throw Error::runtime(context, "Cannot call a native assignable function");
        FFI ffi;
        for (int i = 0; i < args->size; i++) {
            if (!ffi.varargs && params[i].type->kind == TypeKind_Varargs) ffi.varargs = true;
            if      (args->get(i).type->kind == TypeKind_Float32) ffi.push_f32(args->get(i).as<float>());
            else if (args->get(i).type->kind == TypeKind_Float64) ffi.push_f64(args->get(i).as<double>());
            else ffi.push_int(args->get(i).as<uint64_t>());
        }
        ffi.call(function->as<void*>());
        Variable retval(function->type->function_info.return_type);
        if      (retval.type->kind == TypeKind_Float32) retval.as<float>() = ffi.get_f32();
        else if (retval.type->kind == TypeKind_Float64) retval.as<double>() = ffi.get_f64();
        else retval.as<uint64_t>() = ffi.get_int();
        return retval;
    }
    else {
        Function* func = function->as<Function*>();
        context->push_stack_frame(func->name);
        context->set_file_location(func->file);
        if (func->capture_mode != CaptureMode_None) for (int i = 0; i < func->variables->size; i++) {
            if (func->capture_mode == CaptureMode_Shared || func->capture_mode == CaptureMode_CopyOnce)
                context->store_ref(func->variables->pairs[i].key, func->variables->pairs[i].value);
            if (func->capture_mode == CaptureMode_CopyPerCall)
                context->store(func->variables->pairs[i].key, *func->variables->pairs[i].value);
        }
        for (int i = 0; i < num_params; i++) {
            if (params[i].type->kind == TypeKind_Varargs) break;
            char* name = params[i].name;
            Variable var = context->store(name, args->get(i));
            if (!var.type) {
                if (func->variables->has(name))
                     throw Error::runtime(context, String::new_format("Cannot create parameter '%s' because a variable of the same name was captured", name));
                else throw Error::runtime(context, String::new_format("Duplicate parameter name '%s'", name));
            }
        }
        if (func->this_ptr) {
            Variable const_this = Variable(func->this_ptr_type);
            const_this.as<void*>() = func->this_ptr;
            const_this.type = const_this.type->constant(context);
            context->store("this", const_this);
        }
        VarargsInfo* varargs_info = NULL;
        if (varargs_index != -1) {
            Variable varargs = Variable(context->type_cache->primitive(TypeKind_Varargs));
            varargs.as<VarargsInfo*>() = new VarargsInfo(args->items + varargs_index, args->size - varargs_index);
            context->store("...", varargs);
        }
        ByteReader reader(func->entry, func->length);
        execute_codeblock(context, &reader, false);
        Variable var(context->type_cache->primitive(TypeKind_Void));
        if (context->state == State_Return) {
            if (function->type->lvalue_return) {
                Type* rettype = function->type->function_info.return_type;
                if (!context->state_var.ref) throw Error::runtime(context, "Return value is not assignable");
                if (rettype != context->state_var.type) throw Error::runtime(context, String::new_format("Types %s and %s aren't the same", rettype->to_string(), context->state_var.type->to_string()));
                var = context->state_var;
            }
            else var = cast(context, function->type->function_info.return_type, context->state_var);
        }
        else if (context->state == State_Running) {
            if (function->type->function_info.return_type->kind != TypeKind_Void)
                throw Error::runtime(context, "No return specified in a non-void return function");
        }
        else throw Error::runtime(context, String::new_format("'%s' outside of loop %d", context->state == State_Break ? "break" : "continue"));
        context->state = State_Running;
        context->pop_stack_frame();
        delete varargs_info;
        return var;
    }
}

static uint64_t call_driver(Context* context, Type* type, Function* function, uint64_t* args) {
    List<Variable> variables;
    for (int i = 0; i < type->function_info.num_params; i++) {
        if (type->function_info.params[i].type->kind == TypeKind_Varargs) {
            VarargsData* data = (VarargsData*)(uintptr_t)args[i];
            while (data->type != VarargsData::End) {
                Variable var(context->type_cache->primitive(data->type == VarargsData::Integer ? TypeKind_Int64 : TypeKind_Float64));
                var.as<uint64_t>() = data->integer;
                variables.add(var);
                data++;
            }
            break;
        }
        Variable var(type->function_info.params[i].type);
        var.as<uint64_t>() = args[i];
        variables.add(var);
    }
    Variable var(type);
    var.as<Function*>() = function;
    var = execute_function(context, &var, &variables);
    return var.as<uint64_t>();
}

#define bytes(...) write((uint8_t[]){__VA_ARGS__}, sizeof((uint8_t[]){__VA_ARGS__}))
#define ALIGN(x, a) (((x) + ((a) - 1)) / (a) * (a))

static Function* generate_function(Context* context, ByteReader* reader, Type* type, const char* name, const char* file, bool scoped, CaptureMode capture_mode) {
    void* func_ptr = reader->bytes + reader->ptr;
    Function* func = context->function_cache->get(func_ptr);
    if (capture_mode == CaptureMode_None && func) return func;
    ByteWriter* buf = new ByteWriter;

    buf->bytes(0x55);             // push %rbp
    buf->bytes(0x48, 0x89, 0xE5); // mov %rsp, %rbp
    buf->bytes(0x48, 0x81, 0xEC); // sub $x, %rsp
    buf->write<uint32_t>(ALIGN(type->function_info.num_params * 8, 16)); // enforce 16 byte stack alignment
    int ptr = 0;
#ifdef _WIN32
    int slot = 0, stack_ptr = 6; // skip rbp, return address and shadow space
#else
    int int_reg = 0, flt_reg = 0, stack_ptr = 2; // skip rbp and return address
#endif
    for (int i = 0; i < type->function_info.num_params; i++) { // dump args into stack allocated array
        TypeKind kind = type->function_info.params[i].type->kind;
#ifdef _WIN32
        if ((kind == TypeKind_Float32 || kind == TypeKind_Float64) && slot < NUM_FLT_REGS) switch (slot++) {
            case 0: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm0, x(%rsp)
            case 1: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm1, x(%rsp)
            case 2: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm2, x(%rsp)
            case 3: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm3, x(%rsp)
        }
        else if (slot < NUM_INT_REGS) switch (slot++) {
            case 0: buf->bytes(0x48, 0x89, 0x8C, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %rcx, x(%rsp)
            case 1: buf->bytes(0x48, 0x89, 0x94, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %rdx, x(%rsp)
            case 2: buf->bytes(0x4C, 0x89, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %r8, x(%rsp)
            case 3: buf->bytes(0x4C, 0x89, 0x8C, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %r9, x(%rsp)
        }
#else
        if ((kind == TypeKind_Float32 || kind == TypeKind_Float64) && flt_reg < NUM_FLT_REGS) switch (flt_reg++) {
            case 0: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm0, x(%rsp)
            case 1: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm1, x(%rsp)
            case 2: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm2, x(%rsp)
            case 3: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm3, x(%rsp)
            case 4: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm4, x(%rsp)
            case 5: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm5, x(%rsp)
            case 6: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm6, x(%rsp)
            case 7: buf->bytes(0x66, 0x0F, 0xD6, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %xmm7, x(%rsp)
        }
        else if (int_reg < NUM_INT_REGS) switch (int_reg++) {
            case 0: buf->bytes(0x48, 0x89, 0xBC, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %rdi, x(%rsp)
            case 1: buf->bytes(0x48, 0x89, 0xB4, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %rsi, x(%rsp)
            case 2: buf->bytes(0x48, 0x89, 0x94, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %rdx, x(%rsp)
            case 3: buf->bytes(0x48, 0x89, 0x8C, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %rcx, x(%rsp)
            case 4: buf->bytes(0x4C, 0x89, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %r8, x(%rsp)
            case 5: buf->bytes(0x4C, 0x89, 0x8C, 0x24)->write<uint32_t>(ptr++ * 8); break; // mov %r9, x(%rsp)
        }
#endif
        else {
            buf->bytes(0x48, 0x8B, 0x85)->write<uint32_t>(stack_ptr++ * 8); // mov x(%rbp), %rax
            buf->bytes(0x48, 0x89, 0x84, 0x24)->write<uint32_t>(ptr++ * 8); // mov %rax, x(%rsp)
        }
    }
    // call the driver with func meta and arg array
#ifdef _WIN32
    buf->bytes(0x49, 0x89, 0xE1);                    // mov %rsp, %r9   << 4th arg
    buf->bytes(0x48, 0xB9); buf->write(context);     // mov $x, %rcx    << 1st arg
    buf->bytes(0x48, 0xBA); buf->write(type);        // mov $x, %rdx    << 2nd arg
    buf->bytes(0x49, 0xB8);                          // mov $x, %r8     << 3rd arg (to be filled in later)
#else
    buf->bytes(0x48, 0x89, 0xE1);                    // mov %rsp, %rcx   << 4th arg
    buf->bytes(0x48, 0xBF); buf->write(context);     // mov $x, %rdi     << 1st arg
    buf->bytes(0x48, 0xBE); buf->write(type);        // mov $x, %rsi     << 2nd arg
    buf->bytes(0x48, 0xBA);                          // mov $x, %rdx     << 3rd arg (to be filled in later)
#endif
    int off = buf->size; buf->write<uint64_t>(0);
#ifdef _WIN32
    buf->bytes(0x48, 0x83, 0xEC, 0x20);              // sub $20, %rsp    << allocate shadow space
#endif
    buf->bytes(0x48, 0xB8); buf->write(call_driver); // mov $x, %rax
    buf->bytes(0xFF, 0xD0);                          // call %rax
    buf->bytes(0x66, 0x48, 0x0F, 0x6E, 0xC0);        // movq %rax, %xmm0 << return in both rax (int) and xmm0 (float)
    buf->bytes(0xC9);                                // leave
    buf->bytes(0xC3);                                // ret

    func = (Function*)context->new_allocation(sizeof(Function) + buf->size, scoped, type, Allocation::function_cleanup);
    memcpy(buf->bytes + off, &func, sizeof(Function*));
    memcpy((char*)func + sizeof(Function), buf->bytes, buf->size);
    func->name = (char*)name;
    func->file = (char*)file;
    func->length = reader->read<uint32_t>();
    func->entry = reader->bytes + reader->ptr;
    func->capture_mode = capture_mode;
    func->variables = new Map<char*, Variable*>(compare_strings);
    if (capture_mode != CaptureMode_None) for (int i = context->call_stack->peek()->scope_id; i < context->variables->size; i++) {
        if (i == 0) continue;
        Map<char*, Variable*>* vars = context->variables->items[i];
        for (int j = 0; j < vars->size; j++) {
            Variable* var;
            if (capture_mode == CaptureMode_Shared) var = &vars->pairs[j].value->retain();
            else var = &alloc->copy(vars->pairs[j].value)->retain();
            func->variables->add(vars->pairs[j].key, var);
        }
    }
    func->jmp[0] = 0xE9;
    *(uint32_t*)(func->jmp + 1) = sizeof(Function) - 5;
    make_executable(func, sizeof(Function) + buf->size);
    reader->skip(func->length);
    if (capture_mode == CaptureMode_None) context->function_cache->add(func_ptr, func);
    delete buf;
    return func;
}

static Variable walk_struct(Variable str, const char* name) {
    Type::Field* field = NULL;
    for (int i = 0; i < str.type->struct_info.num_fields && !field; i++) {
        Type::Field* f = &str.type->struct_info.fields[i];
        if (f->inlined && !f->name) {
            Variable inlined(f->type);
            inlined.as<void*>() = str.as<char*>() + f->offset;
            Variable var = walk_struct(inlined, name);
            if (!var.type) continue;
            return var;
        }
        if (strcmp(f->name, name) == 0) field = f;
    }
    if (!field) return Variable();
    if (field->inlined) {
        Variable var = Variable(field->type);
        var.as<void*>() = str.as<char*>() + field->offset;
        return var;
    }
    Variable var = Variable(field->type).lvalue(str.as<char*>() + field->offset);
    if (field->type->kind == TypeKind_Function) {
        Function* func = var.as<Function*>();
        if (func && !func->is_native()) {
            func->name = field->name;
            func->this_ptr = str.as<void*>();
            func->this_ptr_type = str.type;
        }
    }
    return var;
}

static void init_struct(Context* context, Variable* str) {
    for (int i = 0; i < str->type->struct_info.num_fields; i++) {
        Type::Field* field = &str->type->struct_info.fields[i];
        if (field->inlined && field->type->kind == TypeKind_Struct) {
            Variable field_var = Variable(field->type);
            field_var.as<char*>() = str->as<char*>() + field->offset;
            init_struct(context, &field_var);
        }
        else {
            Variable field_var = Variable(field->type).lvalue(str->as<char*>() + field->offset);
            if (field->value) field_var.as<uint64_t>() = field->value;
            else if (matches(field->type->kind, VarType_Type)) field_var.as<Type*>() = context->type_cache->primitive(TypeKind_Void);
        }
    }
}

#define BINARY(node, val1, val2, ...) { node, 2, (uint8_t[]){ val1, val2 }, [](Context* context, ByteReader* reader, Stack<Variable>* stack)__VA_ARGS__ }
#define UNARY(node, val1, ...) { node, 1, (uint8_t[]){ val1 }, [](Context* context, ByteReader* reader, Stack<Variable>* stack)__VA_ARGS__ }

#define INTEGER_COMPARE(var1, op, var2) ({ \
    Variable v1 = var1; /* ULTRAKILL :O */ \
    Variable v2 = var2; \
    bool neg_a = INTEGER_NEGATIVE(v1); \
    bool neg_b = INTEGER_NEGATIVE(v2); \
    bool result = false; \
    if (neg_a != neg_b) result = neg_b op neg_a; \
    else result = v1.as<uint64_t>() op v2.as<uint64_t>(); \
    result; \
})

#define INTEGER_NEGATIVE(var) (!(var).type->is_unsigned && ((var).as<uint64_t>() >> 63) & 1)

#define ARITH(op, ...) _ARITH(op __VA_OPT__(,) __VA_ARGS__, op)
#define _ARITH(int_op, flt_op, ...) { \
    Variable var2 = stack->pop(); \
    Variable var1 = stack->pop(); \
    Variable result(promote(context, &var1, &var2)); \
    if (result.type->kind == TypeKind_Float32) \
        result.as<float>() = flt_op(var1.as<float>(), var2.as<float>()); \
    if (result.type->kind == TypeKind_Float64) \
        result.as<double>() = flt_op(var1.as<double>(), var2.as<double>()); \
    else \
        result.as<uint64_t>() = int_op((uint64_t)var1.as<uint64_t>(), (uint64_t)var2.as<uint64_t>()); \
    stack->push(result); \
}

#define PTR_ARITH(op, left, right) { \
    Variable var_##right = stack->pop(); \
    Variable var_##left = stack->pop(); \
    Variable result(var_ptr.type); \
    result.as<uint64_t>() = var_ptr.as<uint64_t>() + var_ptr.type->pointer_info.base->size * var_int.as<uint64_t>(); \
    stack->push(result); \
}

#define BITWISE(op) { \
    Variable var2 = stack->pop(); \
    Variable var1 = stack->pop(); \
    Variable result(promote(context, &var1, &var2)); \
    result.as<uint64_t>() = var1.as<uint64_t>() op var2.as<uint64_t>(); \
    stack->push(result); \
}

#define LOGIC(op) { \
    Variable var2 = stack->pop(); \
    Variable var1 = stack->pop(); \
    Variable result(context->type_cache->primitive(TypeKind_Int8)->unsign(context)); \
    result.as<bool>() = is_truthy(context, &var1) op is_truthy(context, &var2); \
    stack->push(result); \
}

#define COMPARE(op) { \
    Variable var2 = stack->pop(); \
    Variable var1 = stack->pop(); \
    Variable result(context->type_cache->primitive(TypeKind_Int8)->unsign(context)); \
    Type* type = promote(context, &var1, &var2); \
    if (result.type->kind == TypeKind_Float32) \
        result.as<bool>() = var1.as<float>() op var2.as<float>(); \
    if (result.type->kind == TypeKind_Float64) \
        result.as<bool>() = var1.as<double>() op var2.as<double>(); \
    else result.as<uint8_t>() = INTEGER_COMPARE(var1, op, var2); \
    stack->push(result); \
}

#define ASSIGN_OP_NONE(node)
#define ASSIGN_OP_EVAL(node) { \
    stack->push(var1); \
    stack->push(var2); \
    execute_operator(context, reader, stack, node); \
    var2 = stack->pop(); \
}
#define ASSIGN(...) _ASSIGN(__VA_ARGS__ __VA_OPT__(,) node, ASSIGN_OP_EVAL, ASSIGN_OP_NONE)
#define _ASSIGN(node, _, eval, ...) { \
    Variable var2 = stack->pop(); \
    Variable var1 = stack->pop(); \
    if (var1.type->is_const) throw Error::runtime(context, "Cannot write into a constant"); \
    eval(node) \
    var2 = cast(context, var1.type, var2); \
    var1 << var2; \
    stack->push(var2); \
}

#define INCREMENT(amount, op1, op2) { \
    Variable var = stack->pop(); \
    INC_##op1(amount); \
    INC_##op2(amount); \
}
#define INC_EXEC(amount) \
    if      (var.type->kind == TypeKind_Float32) var.as<float>() += amount; \
    else if (var.type->kind == TypeKind_Float64) var.as<double>() += amount; \
    else var.as<uint64_t>() += amount;
#define INC_EVAL(amount) \
    Variable out(var.type); \
    out << var; \
    stack->push(out);

#define ADD(a, b) a + b
#define SUBTRACT(a, b) a - b
#define MULTIPLY(a, b) a * b
#define DIVIDE(a, b) a / b
#define MODULO(a, b) a % b
#define BITSHIFT_LEFT(a, b) a << b
#define BITSHIFT_RIGHT(a, b) a >> b
#define AND(a, b) a & b
#define OR(a, b) a | b
#define XOR(a, b) a ^ b

static struct {
    AST_Node node;
    int num_values;
    uint8_t* types;
    void(*func)(Context* context, ByteReader* reader, Stack<Variable>* stack);
} operators[] = {
    BINARY(AST_ADDITION, VarType_Number, VarType_Number, ARITH(ADD)),
    BINARY(AST_ADDITION, VarType_Pointer, VarType_Integer, PTR_ARITH(+, ptr, int)),
    BINARY(AST_ADDITION, VarType_Integer, VarType_Pointer, PTR_ARITH(+, int, ptr)),
    BINARY(AST_SUBTRACTION, VarType_Number, VarType_Number, ARITH(SUBTRACT)),
    BINARY(AST_SUBTRACTION, VarType_Pointer, VarType_Integer, PTR_ARITH(-, ptr, int)),
    BINARY(AST_SUBTRACTION, VarType_Integer, VarType_Pointer, PTR_ARITH(-, int, ptr)),
    BINARY(AST_MULTIPLICATION, VarType_Number, VarType_Number, ARITH(MULTIPLY)),
    BINARY(AST_DIVISION, VarType_Number, VarType_Number, ARITH(DIVIDE)),
    BINARY(AST_MODULO, VarType_Number, VarType_Number, ARITH(MODULO, fmod)),
    BINARY(AST_POWER, VarType_Number, VarType_Number, ARITH(pow)),
    BINARY(AST_BITSHIFT_LEFT, VarType_Integer, VarType_Integer, BITWISE(<<)),
    BINARY(AST_BITSHIFT_RIGHT, VarType_Integer, VarType_Integer, BITWISE(>>)),
    BINARY(AST_BITWISE_AND, VarType_Integer, VarType_Integer, BITWISE(&)),
    BINARY(AST_BITWISE_OR, VarType_Integer, VarType_Integer, BITWISE(|)),
    BINARY(AST_BITWISE_XOR, VarType_Integer, VarType_Integer, BITWISE(^)),
    BINARY(AST_ELVIS, VarType_Any, VarType_Any, {
        Variable var2 = stack->pop();
        Variable var1 = stack->pop();
        stack->push(is_truthy(context, &var1) ? var1 : var2);
    }),
    BINARY(AST_EQUALS, VarType_Any, VarType_Any, COMPARE(==)),
    BINARY(AST_NOT_EQUALS, VarType_Any, VarType_Any, COMPARE(!=)),
    BINARY(AST_LESS_THAN, VarType_Number, VarType_Number, COMPARE(<)),
    BINARY(AST_GREATER_THAN, VarType_Number, VarType_Number, COMPARE(>)),
    BINARY(AST_LESS_THAN_OR_EQUAL_TO, VarType_Number, VarType_Number, COMPARE(<=)),
    BINARY(AST_GREATER_THAN_OR_EQUAL_TO, VarType_Number, VarType_Number, COMPARE(>=)),
    BINARY(AST_LOGICAL_AND, VarType_Any, VarType_Any, LOGIC(&&)),
    BINARY(AST_LOGICAL_OR, VarType_Type, VarType_Type, LOGIC(||)),
    BINARY(AST_ASSIGN, VarType_Any | VarType_Assignable, VarType_Any, ASSIGN()),
    BINARY(AST_ADD_ASSIGN, VarType_Number | VarType_Assignable, VarType_Number, ASSIGN(AST_ADDITION)),
    BINARY(AST_ADD_ASSIGN, VarType_Pointer | VarType_Assignable, VarType_Integer, ASSIGN(AST_ADDITION)),
    BINARY(AST_SUBTRACT_ASSIGN, VarType_Number | VarType_Assignable, VarType_Number, ASSIGN(AST_SUBTRACTION)),
    BINARY(AST_SUBTRACT_ASSIGN, VarType_Pointer | VarType_Assignable, VarType_Integer, ASSIGN(AST_SUBTRACTION)),
    BINARY(AST_MULTIPLY_ASSIGN, VarType_Number | VarType_Assignable, VarType_Number, ASSIGN(AST_MULTIPLICATION)),
    BINARY(AST_DIVIDE_ASSIGN, VarType_Number | VarType_Assignable, VarType_Number, ASSIGN(AST_DIVISION)),
    BINARY(AST_MODULO_ASSIGN, VarType_Number | VarType_Assignable, VarType_Number, ASSIGN(AST_MODULO)),
    BINARY(AST_POWER_ASSIGN, VarType_Number | VarType_Assignable, VarType_Number, ASSIGN(AST_POWER)),
    BINARY(AST_BITSHIFT_LEFT_ASSIGN, VarType_Integer | VarType_Assignable, VarType_Integer, ASSIGN(AST_BITSHIFT_LEFT)),
    BINARY(AST_BITSHIFT_RIGHT_ASSIGN, VarType_Integer | VarType_Assignable, VarType_Integer, ASSIGN(AST_BITSHIFT_RIGHT)),
    BINARY(AST_BITWISE_AND_ASSIGN, VarType_Integer | VarType_Assignable, VarType_Integer, ASSIGN(AST_BITWISE_AND)),
    BINARY(AST_BITWISE_OR_ASSIGN, VarType_Integer | VarType_Assignable, VarType_Integer, ASSIGN(AST_BITWISE_OR)),
    BINARY(AST_BITWISE_XOR_ASSIGN, VarType_Integer | VarType_Assignable, VarType_Integer, ASSIGN(AST_BITWISE_XOR)),
    BINARY(AST_ELVIS_ASSIGN, VarType_Any | VarType_Assignable, VarType_Any, ASSIGN(AST_ELVIS)),
    BINARY(AST_CAST, VarType_Any, VarType_Type, {
        Variable type = stack->pop();
        Variable var = stack->pop();
        stack->push(cast(context, type.as<Type*>(), var, false, false));
    }),
    BINARY(AST_BITCAST, VarType_Any, VarType_Type, {
        Variable type = stack->pop();
        Variable var = stack->pop();
        stack->push(cast(context, type.as<Type*>(), var, true, false));
    }),

    UNARY(AST_PREFIX_INCREMENT, VarType_Number | VarType_Assignable, INCREMENT(+1, EXEC, EVAL)),
    UNARY(AST_PREFIX_DECREMENT, VarType_Number | VarType_Assignable, INCREMENT(-1, EXEC, EVAL)),
    UNARY(AST_ADDRESS, VarType_Any | VarType_Assignable, {
        Variable var = stack->pop();
        Variable ptr(var.type->pointer(context));
        ptr.as<void*>() = var.ptr();
        stack->push(ptr);
    }),
    UNARY(AST_DEREFERENCE, VarType_Pointer, {
        Variable ptr = stack->pop();
        Variable var = ptr.deref();
        stack->push(var);
    }),
    UNARY(AST_DEREFERENCE, VarType_Type, {
        Variable ptr = stack->pop();
        Variable var(context->type_cache->primitive(TypeKind_Type));
        Type* type = ptr.as<Type*>()->resolve_defers(context);
        switch (type->kind) {
            case TypeKind_Pointer : var.as<Type*>() = type->pointer_info.base;         break;
            case TypeKind_Function: var.as<Type*>() = type->function_info.return_type; break;
            default: throw Error::runtime(context, String::new_format("Type %s is not a pointer or a function", type->to_string()));
        }
        stack->push(var);
    }),
    UNARY(AST_ARITH_PLUS, VarType_Number, {
        Variable var = stack->pop();
        Variable out(var.type);
        out << var;
        stack->push(out);
    }),
    UNARY(AST_ARITH_NEGATE, VarType_Number, {
        Variable var = stack->pop();
        Variable out(var.type);
        if      (var.type->kind == TypeKind_Float32) out.as<float>() = -var.as<float>();
        else if (var.type->kind == TypeKind_Float64) out.as<double>() = -var.as<double>();
        else out.as<uint64_t>() = -var.as<uint64_t>();
        stack->push(out);
    }),
    UNARY(AST_LOGIC_NEGATE, VarType_Any, {
        Variable var = stack->pop();
        Variable out(context->type_cache->primitive(TypeKind_Int8)->unsign(context));
        out.as<bool>() = !is_truthy(context, &var);
        stack->push(out);
    }),
    UNARY(AST_BINARY_NEGATE, VarType_Integer, {
        Variable var = stack->pop();
        Variable out(var.type);
        out.as<uint64_t>() = ~var.as<uint64_t>();
        stack->push(out);
    }),

    UNARY(AST_CONST, VarType_Type, {
        Variable type = stack->pop();
        Variable out(context->type_cache->primitive(TypeKind_Type));
        out.as<Type*>() = type.as<Type*>()->constant(context);
        stack->push(out);
    }),
    UNARY(AST_SUFFIX_INCREMENT, VarType_Number | VarType_Assignable, INCREMENT(+1, EVAL, EXEC)),
    UNARY(AST_SUFFIX_DECREMENT, VarType_Number | VarType_Assignable, INCREMENT(-1, EVAL, EXEC)),
    UNARY(AST_POINTER, VarType_Type, {
        Variable type = stack->pop();
        Variable out(context->type_cache->primitive(TypeKind_Type));
        out.as<Type*>() = type.as<Type*>()->pointer(context);
        stack->push(out);
    }),
    UNARY(AST_ARRAY, VarType_Pointer, {
        Variable ptr = stack->pop();
        List<Variable> args;
        execute_expressions(context, reader, &args);
        if (args.size != 1) throw Error::runtime(context, "Expected only 1 argument");
        Variable index = args.get(0);
        Variable out = ptr.deref(index.as<uint64_t>());
        stack->push(out);
    }),
    UNARY(AST_ARRAY, VarType_Struct, {
        Variable str = stack->pop();
        if (!str.as<void*>()) throw Error::runtime(context, "Struct is unset");
        Variable func = walk_struct(str, "[]");
        if (!func.type) throw Error::runtime(context, "Struct doesn't have an array function");
        List<Variable> args;
        execute_expressions(context, reader, &args);
        stack->push(execute_function(context, &func, &args));
    }),
    UNARY(AST_CALL, VarType_Function, {
        Variable var = stack->pop();
        List<Variable> args;
        execute_expressions(context, reader, &args);
        stack->push(execute_function(context, &var, &args));
    }),
    UNARY(AST_FUNCTION, VarType_Type, {
        Variable type = stack->pop();
        List<Type::Param> params;
        bool lvalue_return = reader->read<bool>();
        execute_params(context, reader, &params);
        Variable out(context->type_cache->primitive(TypeKind_Type));
        out.as<Type*>() = type.as<Type*>()->function(context, &params, lvalue_return);
        stack->push(out);
    }),
    UNARY(AST_GET_SIZE, VarType_Pointer, {
        Variable alloc = stack->pop();
        Variable size(context->type_cache->primitive(TypeKind_Int64)->unsign(context));
        size.as<uint64_t>() = context->alloc_size(alloc.as<void*>());
        stack->push(size);
    }),
    UNARY(AST_GET_LENGTH, VarType_Pointer, {
        Variable alloc = stack->pop();
        Variable len(context->type_cache->primitive(TypeKind_Int64)->unsign(context));
        len.as<uint64_t>() = context->alloc_size(alloc.as<void*>()) / alloc.type->pointer_info.base->value_size();
        stack->push(len);
    }),
    UNARY(AST_GET_SCOPE, VarType_Pointer, {
        Variable alloc = stack->pop();
        Variable scope(context->type_cache->primitive(TypeKind_Int64)->unsign(context));
        scope.as<uint64_t>() = context->alloc_scope(alloc.as<void*>());
        stack->push(scope);
    }),
    UNARY(AST_WALK_STRUCT, VarType_Struct, {
        Variable str = stack->pop();
        char* name = reader->read<char*>();
        if (!str.as<void*>()) throw Error::runtime(context, "Struct is unset");
        Variable var = walk_struct(str, name);
        if (!var.type) throw Error::runtime(context, String::new_format("Field '%s' not found", name));
        else stack->push(var);
    }),
    UNARY(AST_WALK_STRUCT, VarType_Type, {
        Variable vartype = stack->pop();
        Type* type = vartype.as<Type*>()->resolve_defers(context);
        Type* result = NULL;
        char* name = reader->read<char*>();
        if (type->kind == TypeKind_Struct) for (int i = 0; i < type->struct_info.num_fields && !result; i++) {
            Type::Field* field = &type->struct_info.fields[i];
            if (strcmp(field->name, name) == 0) result = field->type;
        }
        else if (type->kind == TypeKind_Function) for (int i = 0; i < type->struct_info.num_fields && !result; i++) {
            Type::Field* field = &type->struct_info.fields[i];
            if (strcmp(field->name, name) == 0) result = field->type;
        }
        else throw Error::runtime(context, "Not a function or struct");
        if (!result) throw Error::runtime(context, String::new_format("%s '%s' not found", type->kind == TypeKind_Struct ? "Field" : "Parameter", name));
        Variable out(context->type_cache->primitive(TypeKind_Type));
        out.as<Type*>() = result;
        stack->push(out);
    }),
};

static bool execute_operator(Context* context, ByteReader* reader, Stack<Variable>* stack, AST_Node node) {
    for (int i = 0; i < sizeof(operators) / sizeof(*operators); i++) {
        if (operators[i].node != node) continue;
        if (operators[i].num_values > stack->size) continue;
        bool matching = true;
        Variable vars[operators[i].num_values];
        for (int j = operators[i].num_values - 1; j >= 0; j--) vars[j] = stack->pop();
        for (int j = 0; j < operators[i].num_values; j++) {
            stack->push(vars[j]);
            if (!matches(&vars[j], operators[i].types[j])) matching = false;
        }
        if (!matching) continue;
        operators[i].func(context, reader, stack);
        return true;
    }
    OperatorInfo* info = &operator_info[node];
    if (info->token == TOKEN_END_OF_FILE) throw Error::runtime(context, "Invalid operator");
    if (stack->size == 0) throw Error::runtime(context, "Empty stack");
    if (info->format == OperatorInfo::OpFmt_Binary && stack->size < 2) throw Error::runtime(context, "Cannot run a binary operator on a single value");
    Variable right = stack->items[stack->size - 1];
    Variable left = info->format == OperatorInfo::OpFmt_Binary ? stack->items[stack->size - 2] : right;
    throw Error::runtime(context, String().concat("Operand type mismatch ").format((const char*[]){
        "(%1$s%2$s)",       // prefix
        "(%2$s%1$s)",       // suffix
        "(%2$s %1$s %3$s)", // binary
        "(%2$s[])",         // array
        "(%2$s())",         // call
        "(%2$s<-())",       // function
        "(%2$s%1$ssize)",   // size
        "(%2$s%1$slength)", // length
        "(%2$s%1$sscope)",  // scope
    }[info->format], token_table[info->token],
        left .type->to_string().concat(left .ref ? "=" : ""),
        right.type->to_string().concat(right.ref ? "=" : "")
    ));
    return false;
}

static Variable execute_expression_node(Context* context, ByteReader* reader, Stack<Variable>* stack) {
    Variable var;
    AST_Node node = reader->read<AST_Node>();
    if (node == AST_END) return var;
    Context::LocationHook hook;
    context->set_location(reader, &hook);
    switch (node) {
        case AST_INTEGER: {
            uint64_t value = reader->read<uint64_t>();
            var = Variable(context->type_cache->primitive(
                value < 2147483648ULL ? TypeKind_Int32 : TypeKind_Int64
            ));
            if (value >= 9223372036854775808ULL && var.type->kind == TypeKind_Int64)
                var.type = context->type_cache->unsign(var.type);
            var.as<uint64_t>() = value;
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_FLOAT: {
            var = Variable(context->type_cache->primitive(TypeKind_Float64));
            var.as<double>() = reader->read<double>();
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_STRING: {
            var = Variable(context->type_cache->primitive(TypeKind_Int8)->constant(context)->pointer(context));
            var.as<char*>() = reader->read<char*>();
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_TYPE: {
            bool is_const = reader->read<bool>();
            TypeKind kind = reader->read<TypeKind>();
            Type* type = NULL;
            if (kind == TypeKind_Struct) {
                List<Type::Field> fields;
                if (reader->read<bool>()) {
                    Variable base = execute_expression(context, reader);
                    if (!matches(&base, VarType_Type)) throw Error::runtime(context, "Not a type");
                    Type::Field field = { .offset = 0, .value = 0, .inlined = true };
                    field.name = (char*)"super";
                    field.type = base.as<Type*>();
                    fields.add(field);
                }
                while (reader->read<bool>()) {
                    Type::Field field;
                    Context::LocationHook hook;
                    context->set_location(reader, &hook);
                    field.offset = -1;
                    if (reader->read<bool>()) {
                        List<Type::Param> params;
                        execute_params(context, reader, &params);
                        bool lvalue_return = reader->read<bool>();
                        Variable return_type = execute_expression(context, reader);
                        if (!matches(&return_type, VarType_Type)) throw Error::runtime(context, "Return type is not a type");
                        CaptureMode capture_mode = reader->read<CaptureMode>();
                        field.inlined = false;
                        field.name = (char*)"[]";
                        field.type = context->type_cache->function(return_type.as<Type*>(), &params, lvalue_return);
                        field.value = (uintptr_t)generate_function(context, reader, field.type, "[]", context->call_stack->peek()->file, false, capture_mode);
                    }
                    else {
                        field.inlined = reader->read<bool>();
                        Variable type = execute_expression(context, reader);
                        if (!matches(&type, VarType_Type)) throw Error::runtime(context, "Not a type");
                        field.type = type.as<Type*>();
                        field.name = reader->read<bool>() ? reader->read<char*>() : NULL;
                        if (field.name && (strcmp(field.name, "new") == 0 || strcmp(field.name, "delete") == 0)) {
                            if (field.type->kind == TypeKind_Function) {
                                if (field.type->function_info.num_params > 0) throw Error::runtime(context, "Cannot take any parameters");
                            }
                            else throw Error::runtime(context, "Must be a function");
                        }
                        if (reader->read<bool>()) {
                            if (reader->read<bool>()) {
                                CaptureMode capture_mode = reader->read<CaptureMode>();
                                field.value = (uintptr_t)generate_function(context, reader, field.type, field.name, context->call_stack->peek()->file, false, capture_mode);
                            }
                            else field.value = cast(context, field.type, execute_expression(context, reader)).as<uint64_t>();
                        }
                    }
                    if (reader->read<bool>()) {
                        bool relative = reader->read<bool>();
                        Variable expr = execute_expression(context, reader);
                        if (!matches(&expr, VarType_Integer)) throw Error::runtime(context, "Offset is not an integer");
                        field.offset = expr.as<uint64_t>() | ((uint64_t)relative << 63);
                    }
                    fields.add(field);
                }
                for (int i = 0; i < fields.size; i++) {
                    for (int j = i + 1; j < fields.size; j++) {
                        if (fields.items[i].name == fields.items[j].name)
                            throw Error::runtime(context, String::new_format("Duplicate field names found: '%s'", fields.items[i].name));
                    }
                }
                type = context->type_cache->structure(&fields);
            }
            else {
                type = context->type_cache->primitive(kind);
                if (reader->read<bool>()) type = type->unsign(context);
            }
            if (is_const) type = type->constant(context);
            var = Variable(context->type_cache->primitive(TypeKind_Type));
            var.as<Type*>() = type;
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_TRUTHY: {
            Variable var(context->type_cache->primitive(TypeKind_Int8)->unsign(context));
            var.as<bool>() = reader->read<bool>();
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_NULL: {
            Variable var(context->type_cache->primitive(TypeKind_Void)->pointer(context));
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_VARIABLE: {
            const char* name = reader->read<char*>();
            var = context->load(name);
            if (!var.type) throw Error::runtime(context, String::new_format("Variable '%s' not found", name));
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_VARARGS: {
            Variable var = context->load("...");
            if (!var.type) throw Error::runtime(context, "No varargs available in current context");
            VarargsInfo* info = var.as<VarargsInfo*>();
            Variable index_var = execute_expression(context, reader);
            if (!matches(&index_var, VarType_Integer)) throw Error::runtime(context, "Index must be an integer");
            uint64_t index = index_var.as<uint64_t>();
            if (index >= info->num_args) throw Error::runtime(context, "Vararg index out of bounds");
            var = info->array[index];
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_DEFER: {
            Variable var = Variable(context->type_cache->primitive(TypeKind_Type));
            var.as<Type*>() = context->type_cache->deferred(reader->read<char*>());
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_PAREN: {
            var = execute_expression(context, reader);
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_SIZEOF: {
            bool varargs = reader->read<bool>();
            Variable var = varargs ? context->load("...") : execute_expression(context, reader);
            if (!var.type) throw Error::runtime(context, "No varargs available in current context");
            Type* type = matches(&var, VarType_Type) ? var.as<Type*>() : var.type;
            Variable size = Variable(context->type_cache->primitive(TypeKind_Int64)->unsign(context));
            size.as<uint64_t>() = varargs ? var.as<VarargsInfo*>()->num_args : type->size;
            return stack ? stack->push(size)->peek() : size;
        } break;
        case AST_TYPEOF: {
            Variable var = execute_expression(context, reader);
            Variable out = Variable(context->type_cache->primitive(TypeKind_Type));
            out.as<Type*>() = var.type;
            return stack ? stack->push(out)->peek() : out;
        } break;
        case AST_SCOPEOF: {
            Variable var = Variable(context->type_cache->primitive(TypeKind_Int32));
            if (reader->read<bool>()) {
                char* name = reader->read<char*>();
                int scope = context->scopeof(name);
                if (scope == -1) throw Error::runtime(context, String::new_format("Variable '%s' not found", name));
            }
            else var.as<int32_t>() = context->variables->size - 1;
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_NEW: {
            bool scoped = reader->read<bool>();
            Variable vartype = execute_expression(context, reader);
            if (!matches(&vartype, VarType_Type)) throw Error::runtime(context, "Not a type");
            Type* type = vartype.as<Type*>()->resolve_defers(context);
            Map<char*, Variable>* struct_data = NULL;
            Variable out(matches(type->kind, VarType_Function) || matches(type->kind, VarType_Struct) ? type : type->pointer(context));
            switch (reader->read<AllocType>()) {
                case AllocType_None: {
                    if (matches(type->kind, VarType_Function)) throw Error::runtime(context, "Allocating an empty function");
                    if (matches(type->kind, VarType_Struct)) struct_data = new Map<char*, Variable>(compare_strings);
                    else out.as<void*>() = context->new_allocation(type->value_size(), scoped, type);
                } break;
                case AllocType_Scalar: {
                    if (matches(type->kind, VarType_Function)) throw Error::runtime(context, "Cannot allocate a function using the scalar initializer");
                    Variable var = cast(context, type, execute_expression(context, reader));
                    if (matches(type->kind, VarType_Struct)) {
                        out.as<void*>() = context->new_allocation(type->size, scoped, type, Allocation::struct_cleanup);
                        memcpy(out.as<void*>(), var.as<void*>(), type->size);
                    }
                    else {
                        out.as<void*>() = context->new_allocation(type->value_size(), scoped, type);
                        out.deref() << var;
                    }
                } break;
                case AllocType_Array: {
                    List<Variable> init_list;
                    Variable size = execute_expression(context, reader);
                    if (!matches(&size, VarType_Integer)) throw Error::runtime(context, "Array size is not an integer");
                    execute_expressions(context, reader, &init_list);
                    out.as<void*>() = context->new_allocation(type->value_size() * size.as<uint64_t>(), scoped, type);
                    for (int i = 0; i < (init_list.size < size.as<uint64_t>() ? init_list.size : size.as<uint64_t>()); i++) {
                        Variable var = cast(context, type, init_list.items[i]);
                        out.deref(i) << var;
                    }
                } break;
                case AllocType_Function: {
                    if (!matches(type->kind, VarType_Function)) throw Error::runtime(context, "Not a function");
                    CaptureMode capture_mode = reader->read<CaptureMode>();
                    out.as<Function*>() = generate_function(context, reader, type, "<anonymous>", context->call_stack->peek()->file, scoped, capture_mode);
                } break;
                case AllocType_Struct: {
                    if (!matches(type->kind, VarType_Struct)) throw Error::runtime(context, "Not a struct");
                    struct_data = new Map<char*, Variable>(compare_strings);
                    while (reader->read<bool>()) {
                        char* name = reader->read<char*>();
                        Variable var = execute_expression(context, reader);
                        struct_data->add(name, var);
                    }
                } break;
            }
            if (struct_data) {
                out.as<void*>() = context->new_allocation(type->size, scoped, type, Allocation::struct_cleanup);
                init_struct(context, &out);
                for (int i = 0; i < struct_data->size; i++) {
                    Variable field = walk_struct(out, struct_data->pairs[i].key);
                    if (!field.type) throw Error::runtime(context, String::new_format("Field '%s' doesn't exist", struct_data->pairs[i].key));
                    field << cast(context, field.type, struct_data->pairs[i].value);
                }
                Variable constructor = walk_struct(out, "new");
                if (constructor.type) {
                    List<Variable> args;
                    execute_function(context, &constructor, &args);
                }
                delete struct_data;
                return stack ? stack->push(out)->peek() : out;
            }
            out.rvalue();
            return stack ? stack->push(out)->peek() : out;
        } break;
        case AST_DELETE: {
            Variable var = execute_expression(context, reader);
            if (!matches(&var, VarType_Pointer) && !matches(&var, VarType_Struct) && !matches(&var, VarType_Function))
                throw Error::runtime(context, "Not a pointer, struct or function");
            context->delete_allocation(var.as<void*>());
            var = Variable(context->type_cache->primitive(TypeKind_Void));
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_MOVE: {
            Variable var = execute_expression(context, reader);
            if (!matches(&var, VarType_Pointer) && !matches(&var, VarType_Struct) && !matches(&var, VarType_Function))
                throw Error::runtime(context, "Not a pointer, struct or function");
            Variable scope = execute_expression(context, reader);
            if (!matches(&scope, VarType_Integer)) throw Error::runtime(context, "Not an integer");
            context->move_allocation(var.as<void*>(), scope.as<uint64_t>());
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_TERNARY: {
            Variable var = execute_expression(context, reader);
            if (is_truthy(context, &var)) {
                var = execute_expression(context, reader->enter());
                reader->skip();
            }
            else {
                reader->skip();
                var = execute_expression(context, reader->enter());
            }
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_INCLUDE: {
            char* name = reader->read<char*>();
#ifdef _WIN32
            if (GetFileAttributes(context->resource(name).data) == -1)
#else
            if (access(context->resource(name).data, F_OK) == 0)
#endif
                throw Error::runtime(context, String::new_format("File '%s' not found", name));
            context->push_stack_frame("<include>");
            Error* error = execute_file(context, name);
            context->pop_stack_frame();
            if (error) throw error;
            Variable var = context->load("@RESULT@").rvalue();
            return stack ? stack->push(var)->peek() : var;
        } break;
        case AST_DECL: {
            bool is_extern = reader->read<bool>();
            char* name = reader->read<char*>();
            var = stack->pop();
            if (!matches(&var, VarType_Type)) throw Error::runtime(context, "Not a type");
            void* symbol = is_extern ? dlsym(NULL, name) : NULL;
            if (!symbol && is_extern) throw Error::runtime(context, String::new_format("Cannot find symbol '%s'", name));
            context->push_codeblock();
            while (reader->read<bool>()) {
                char* varname = reader->read<char*>();
                Variable typevar = execute_expression(context, reader);
                if (!matches(&typevar, VarType_Type)) throw Error::runtime(context, String::new_format("Parameter '%s' is not a type", varname));
                context->store(varname, typevar);
            }
            var = Variable(var.as<Type*>()->resolve_defers(context));
            context->pop_codeblock();
            if (matches(var.type->kind, VarType_Type)) var.as<Type*>() = context->type_cache->primitive(TypeKind_Void);
            var = context->store(name, var, symbol);
            if (!var.type) throw Error::runtime(context, String::new_format("Variable '%s' already exists in the current scope", name));
            if (reader->read<bool>()) {
                if (!matches(&var, VarType_Function)) throw Error::runtime(context, "Cannot attach code to a non-function variable");
                CaptureMode capture_mode = reader->read<CaptureMode>();
                var.as<Function*>() = generate_function(context, reader, var.type, name, context->call_stack->peek()->file, true, capture_mode);
            }
            return stack ? stack->push(var)->peek() : var;
        } break;
        default:
            execute_operator(context, reader, stack, node);
            var = stack->peek();
            break;
    }
    return var;
}

static Variable execute_expression(Context* context, ByteReader* reader) {
    Stack<Variable> stack;
    while (true) {
        // todo: short circuiting
        Variable result = execute_expression_node(context, reader, &stack);
        if (!result.type) break;
    }
    return stack.pop();
}

static Variable execute_codeblock(Context* context, ByteReader* reader, bool push_scope) {
    Variable var(context->type_cache->primitive(TypeKind_Void));
    if (push_scope) context->push_codeblock();
    while (context->state == State_Running) {
        Variable last = execute_command(context, reader);
        if (!last.type) break;
        var = last;
    }
    if (push_scope) context->pop_codeblock();
    return var;
}

static Variable execute_command(Context* context, ByteReader* reader) {
    AST_Node cmd = reader->read<AST_Node>();
    if (cmd == AST_END) return Variable();
    Variable var;
    Context::LocationHook hook;
    context->set_location(reader, &hook);
    switch (cmd) {
        case AST_IF: {
            Variable var = execute_expression(context, reader);
            if (is_truthy(context, &var)) {
                var = execute_codeblock(context, reader->enter());
                if (reader->read<bool>()) reader->skip();
            }
            else {
                reader->skip();
                if (reader->read<bool>()) var = execute_codeblock(context, reader->enter());
                else var = Variable(context->type_cache->primitive(TypeKind_Void));
            }
            return var;
        }
        case AST_WHILE: {
            int start_ptr = reader->ptr;
            while (true) {
                Variable cond = execute_expression(context, reader->seek(start_ptr));
                var = Variable(context->type_cache->primitive(TypeKind_Void));
                if (is_truthy(context, &cond)) {
                    var = execute_codeblock(context, reader->enter());
                    State state = context->state;
                    if (state == State_Break || state == State_Continue) context->state = State_Running;
                    if (state == State_Break || state == State_Return) {
                        reader->seek(start_ptr)->skip();
                        return var;
                    }
                }
                else {
                    reader->skip();
                    return var;
                }
            }
        } break;
        case AST_FOR: {
            Variable iter_var = execute_expression(context, reader);
            if (!matches(&iter_var, VarType_Type)) throw Error::runtime(context, "Not a type");
            Type* iter_type = iter_var.as<Type*>()->resolve_defers(context);
            if (!matches(iter_type->kind, VarType_Integer)) throw Error::runtime(context, "Not an integer type");
            char* name = reader->read<char*>();
            Variable from = cast(context, iter_type, execute_expression(context, reader));
            bool from_exclusive = reader->read<bool>();
            Variable to = cast(context, iter_type, execute_expression(context, reader));
            bool to_exclusive = reader->read<bool>();
            Variable step(context->type_cache->primitive(TypeKind_Int64));
            if (reader->read<bool>()) step = cast(context, step.type, execute_expression(context, reader));
            else step.as<uint64_t>() = 1;
            bool reverse = INTEGER_NEGATIVE(step);
            Variable var(context->type_cache->primitive(TypeKind_Void));
            Variable iter(iter_type);
            int start_ptr = reader->ptr;
            iter << (reverse ? to : from);
            if (reverse ? to_exclusive : from_exclusive) iter.as<uint64_t>() += step.as<uint64_t>();
            while (
                (!reverse &&   (to_exclusive ? INTEGER_COMPARE(iter, <, to)   : INTEGER_COMPARE(iter, <=, to))) ||
                ( reverse && (from_exclusive ? INTEGER_COMPARE(iter, >, from) : INTEGER_COMPARE(iter, >=, from)))
            ) {
                context->push_codeblock();
                context->store(name, iter);
                context->state = State_Running;
                reader->seek(start_ptr);
                var = execute_codeblock(context, reader->enter(), false);
                State state = context->state;
                if (state == State_Break || state == State_Continue) context->state = State_Running;
                if (state == State_Break || state == State_Return) break;
                iter.as<uint64_t>() = context->load(name).as<uint64_t>() + step.as<uint64_t>();
                context->pop_codeblock();
            }
            reader->seek(start_ptr)->skip();
            return var;
        } break;
        case AST_RETURN: {
            context->state_var = reader->read<bool>()
                ? execute_expression(context, reader)
                : Variable(context->type_cache->primitive(TypeKind_Void));
            context->state = State_Return;
        } break;
        case AST_CONTINUE: {
            context->state = State_Continue;
        } break;
        case AST_BREAK: {
            context->state = State_Break;
        } break;
        case AST_TRY: {
            Variable var(context->type_cache->primitive(TypeKind_Void));
            int scope = context->variables->size;
            int ptr = reader->ptr;
            try {
                var = execute_codeblock(context, reader->enter());
                if (reader->read<bool>()) {
                    reader->read<bool>(); // silently
                    if (reader->read<bool>()) reader->read<char*>(); // as
                    reader->skip(); // catch body
                }
            }
            catch (Error* error) {
                context->pop_until(scope);
                reader->seek(ptr)->skip();
                if (reader->read<bool>()) {
                    if (reader->read<bool>()) pawscript_destroy_error(error);
                    else pawscript_log_error(error, stderr);
                    context->push_codeblock();
                    if (reader->read<bool>()) context->store(reader->read<char*>(), context->state_var);
                    var = execute_codeblock(context, reader->enter(), false);
                    context->pop_codeblock();
                }
                else pawscript_log_error(error, stderr);
            }
            return var;
        } break;
        case AST_THROW: {
            context->state_var = execute_expression(context, reader);
            if (reader->read<bool>()) throw Error::runtime(context, reader->read<char*>());
            else throw Error::runtime(context, var.to_string());
        } break;
        case AST_CODEBLOCK: return execute_codeblock(context, reader);
        case AST_EXPR: return execute_expression(context, reader);
        default: break;
    }
    return var;
}

void Allocation::function_cleanup(void* ptr, Context* context, Type* type) {
    Function* func = (Function*)ptr;
    for (int i = 0; i < func->variables->size; i++) func->variables->pairs[i].value->release();
    delete func->variables;
}

void Allocation::struct_cleanup(void* ptr, Context* context, Type* type) {
    int prev_scope = context->variables->size - 1;
    try {
        Variable str = Variable(type);
        str.as<void*>() = ptr;
        Variable destructor = walk_struct(str, "delete");
        if (!destructor.type) return;
        List<Variable> args;
        execute_function(context, &destructor, &args);
    }
    catch (Error* error) {
        pawscript_log_error(error, stderr);
        context->pop_until(prev_scope);
    }
}

static void(*user_segfault_handler)(void* addr);
static Context* curr_context;

static Error* execute(Context* context, const char* code, const char* file) {
    TokenQueue* tokens = NULL;
    ByteWriter* writer = new ByteWriter;
    Error* err = NULL;
    Variable var(context->type_cache->primitive(TypeKind_Void));
    try {
        tokens = lex(context, code, file);
        writer->write(tokens->peek()->filename);
        while (!tokens->expect(TOKEN_END_OF_FILE)) parse_command(context, writer, tokens);
        ByteReader* reader = writer->read();
        /*printf("--------------- PAWSCRIPT BYTECODE DUMP ---------------\n");
        printf("       x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF");
        for (int i = 0; i < reader->size; i++) {
            if (i % 16 == 0) printf("\n%04X   ", i);
            printf("%02X ", reader->bytes[i]);
        }
        printf("\n");*/
        context->set_file_location(reader->read<char*>());
        context->bytecodes->add(reader);
        while (reader->ptr < reader->size) {
            var = execute_command(context, reader);
            switch (context->state) {
                case State_Running: break;
                case State_Return:
                    var = context->state_var;
                    reader->ptr = reader->size;
                    break;
                case State_Continue: throw Error::runtime(context, "'continue' outside of loop");
                case State_Break: throw Error::runtime(context, "'break' outside of loop");
            }
        }
    }
    catch (Error* error) {
        err = error;
    }
    *context->lookup_variable("@RESULT@") = var;
    context->call_stack->peek()->file = NULL;
    delete tokens;
    return err;
}

static Error* execute_file(Context* context, const char* filename) {
    FILE* f = fopen(context->resource(filename).data, "r");
    if (!f) return Error::syntax(filename, 1, 1, String::new_format("Cannot open '%s' for reading: %s", filename, strerror(errno)));
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = alloc->malloc<char>(size + 1);
    fread(data, size, 1, f);
    data[size] = 0;
    fclose(f);
    Error* error = execute(context, data, filename);
    alloc->free(data);
    return error;
}

static jmp_buf segfault_jump_buffer;
static bool in_code = false;
static void* segfault_addr = NULL;

#ifndef _WIN32
#undef setjmp
#define longjmp siglongjmp
#define setjmp(x) sigsetjmp(x, 1)
#endif

#ifdef _WIN32
static LONG WINAPI handle_segfault(EXCEPTION_POINTERS* exception) { \
    segfault_addr = (void*)(uintptr_t)exception->ExceptionRecord->ExceptionInformation[1];
#else
static void handle_segfault(int signum, siginfo_t* info) { \
    segfault_addr = info->si_addr;
#endif
    if (in_code) longjmp(segfault_jump_buffer, 1);
    else if (user_segfault_handler) user_segfault_handler(segfault_addr);
    abort();
}

static Error* segfault_handler(Context* context) {
    if (!segfault_addr) return Error::runtime(context, "Null pointer dereference");
    else return Error::runtime(context, String::new_format("Invalid memory access at %p", segfault_addr));
}

// == INTERPRETER API ==

API Context* pawscript_create_context() {
    static bool did_atexit = false;
    if (!did_atexit) {
        did_atexit = true;
        //atexit([]() { delete alloc; });
#ifdef _WIN32
        SetUnhandledExceptionFilter(handle_segfault);
#else
        struct sigaction signal_handler;
        signal_handler.sa_handler = (typeof(signal_handler.sa_handler))handle_segfault;
        sigemptyset(&signal_handler.sa_mask);
        signal_handler.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &signal_handler, NULL);
#endif
    }
    Context* context = alloc->malloc<Context>();
    context->strings = new Set<char*>(compare_int64);
    context->bytecodes = new Set<ByteReader*>(compare_int64);
    context->type_cache = new TypeCache;
    context->function_cache = new Map<void*, Function*>(compare_int64);
    context->call_stack = new Stack<Scope*>;
    context->variables = new Stack<Map<char*, Variable*>*>;
    context->allocs = new Stack<Set<Allocation*>*>;
    context->push_stack_frame("<global>");
    context->store("@RESULT@", Variable(context->type_cache->primitive(TypeKind_Void)));
    return context;
}

API void pawscript_destroy_context(Context *context) {
    context->call_stack->peek()->file = (char*)"<context destroy>";
    while (context->call_stack->size > 0) context->pop_stack_frame();
    for (int i = 0; i < context->strings->size; i++) alloc->free(context->strings->items[i]);
    for (int i = 0; i < context->bytecodes->size; i++) delete context->bytecodes->items[i];
    delete context->strings;
    delete context->bytecodes;
    delete context->type_cache;
    delete context->function_cache;
    delete context->call_stack;
    delete context->variables;
    delete context->allocs;
    alloc->free(context);
}

API void pawscript_log_error(Error* error, FILE* f) {
    ErrorScope* scope = error->scope;
    fprintf(f, "Error: %s\n", error->msg);
    while (scope) {
        ErrorScope* parent = scope->parent;
        fprintf(f, "  in %s at %s (%d:%d)\n", scope->name, scope->file, scope->row, scope->col);
        alloc->free(scope);
        scope = parent;
    }
    alloc->free(error);
}

API void pawscript_destroy_error(Error* error) {
    ErrorScope* scope = error->scope;
    while (scope) {
        ErrorScope* parent = scope->parent;
        alloc->free(scope);
        scope = parent;
    }
    alloc->free(error);
}

API Error* pawscript_run(Context* context, const char* code) {
    Error* error;
    in_code = true;
    if (setjmp(segfault_jump_buffer) == 0) error = execute(context, code, "<memory>");
    else error = segfault_handler(context);
    context->pop_until(0);
    in_code = false;
    return error;
}

API Error* pawscript_run_file(Context* context, const char* filename) {
    Error* error;
    in_code = true;
    if (setjmp(segfault_jump_buffer) == 0) error = execute_file(context, filename);
    else error = segfault_handler(context);
    context->pop_until(0);
    in_code = false;
    return error;
}

API bool pawscript_print_variable(Context* context, FILE* f, const char* name) {
    Variable var = context->load(name);
    if (!var.type) return false;
    fprintf(f, "%s\n", var.to_string().data);
    return true;
}

API bool pawscript_print_type(Context* context, FILE* f, const char* name) {
    Variable var = context->load(name);
    if (!var.type) return false;
    fprintf(f, "%s\n", var.type->to_string().data);
    return true;
}

API Type* pawscript_typeof(Context* context, const char* name) {
    Variable var = context->load(name);
    if (!var.type) return NULL;
    return var.type;
}

API bool pawscript_get(Context* context, const char* name, void* data) {
    Variable var = context->load(name);
    if (!var.type) return false;
    memcpy(data, var.ptr(), var.type->value_size());
    return true;
}

API bool pawscript_set(Context* context, const char* name, void* data) {
    Variable var = context->load(name);
    if (!var.type) return false;
    if (var.type->kind == TypeKind_Function) var.as<void*>() = data;
    else memcpy(var.ptr(), data, var.type->value_size());
    return true;
}

API void on_segfault(void(*handler)(void* addr)) {
    user_segfault_handler = handler;
}

#endif