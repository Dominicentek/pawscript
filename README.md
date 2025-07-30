# PawScript

A minimal systems-like scripting language designed to interop with C.

> [!WARNING]
> PawScript is painfully slow. It's not at all designed with performance in mind. It's meant to be a small embeddable layer to extend functionality of a game engine.
> If you want something performant, look somewhere else, maybe try Lua.

> [!WARNING]
> The interpreter is designed for x86_64 architectures using either SysV or Win64 ABIs for it's interop. You can use the interpreter on other

Implementation of standard C headers can be found in the "stdc" directory.

To build the interpreter, just run `./build.sh` (you need to have gcc installed).

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Why is it called PawScript](#why-is-it-called-pawscript)
- [Specification](#specification)
  - [Types](#types)
  - [Structs](#structs)
  - [Functions](#functions)
  - [Statements](#statements)
  - [Expressions](#expressions)
  - [Varargs](#varargs)
  - [`include`](#include)
  - [`typedef`](#typedef)
  - [`new`](#new)
  - [`extern`](#extern)
  - [Memory Checking](#memory-checking)
- [Engine API](#engine-api)
  - [Error Checking](#error-checking)
  - [Getting variables](#getting-variables)
  - [Setting variables](#setting-variables)
  - [Symbol Blacklist/Whitelist](#symbol-blacklistwhitelist)
  - [Reflection](#reflection)
  - [Windows Difficulties](#windows-difficulties)
- [The Future](#the-future)
  - [Types as variables](#types-as-variables)
  - [Closures](#closures)
  - [Bytecode Compiler](#bytecode-compiler)
  - [Error Handling](#error-handling)

## Why is it called PawScript

i like cats :3

## Specification

### Types

PawScript has several base types.
* `u8` - unsigned 8-bit integer
* `u16` - unsigned 16-bit integer
* `u32` - unsigned 32-bit integer
* `u64` - unsigned 64-bit integer
* `s8` - signed 8-bit integer
* `s16` - signed 16-bit integer
* `s32` - signed 32-bit integer
* `s64` - signed 64-bit integer
* `f32` - 32-bit floating point number
* `f64` - 64-bit floating point number
* `bool` - same as `u8`
* `void` - just void.

These types can be extended into pointers or functions. Pointers are structured similarly to C, where you just append an `*` after the base type, like this:
```
s32* pointer_to_s32;
```
Functions are also specified after the base type, not the identifier:
```
void(s32 param) func;
```
This makes defining function pointers easier:
```
void(s32 param)* func_ptr;
```
You can also make a value constant by either prepending `const` to the base type, which makes the base type immutable, or after the type:
```
const s32 constant;
s32 const constant; // identical
```
With this, you can make immutable pointers as well:
```
s32* const const_ptr;
```
This doesn't make the base type immutable though, you'd have to `const` that one too.

### Structs

The language also supports structs. The way you define them is similar to C:
```
struct {
  s32 integer_value;
  f64 floating_value;
};
```
You can name them of course. Once you do name them, you can refer to them with just their name, no need for a `struct` prefix:
```
struct SomeStruct {
  s32 integer_value;
  f64 floating_value;
};

SomeStruct structure;
```
The offsets of fields follow the same rules as in C.

However, `union`s are missing. That's because you can manually specify an offset with the `@` symbol:
```
struct Variable {
  s8* name;
  u64 int_value    @ 8;                  // strictly at offset 8
  f64 float_value  @ int_value;          // exactly the same offset as int_value
  s8* string_value @ name + 8;           // you can use full-on expressions
  void* ptr_value  @ calculate_offset(); // meaning you can do this
};
```
These expressions **must** return an integer type.

You can also extend structs, so it's kinda like OOP but not quite:
```
struct Extended: SomeStruct {
  s32 integer_value_mirror @ integer_value; // you can use field from the superstruct of course
}
```
`Extended` retains the structure of `SomeStruct`, but adds its own fields, so you can cast `Extended*` to `SomeStruct*` and it'd work the same.

However, you **cannot** declare non-pointer structs as their own variable. This is done to keep the interpreter code simple, as it ensures that all variables are scalar. You can still use non-pointer structs in structs themselves though:
```
struct SomeStruct {
  struct {
    s32 value1;
  } anonymous_struct;
  struct {
    s32 value2;
  }; // nameless struct: flattened into the parent struct
};
```

### Functions

Because of how the type system works, function definitions look like this:
```
s32(s32 x) func {
  // ...
}
```
Of course, you can prematurely declare a function before defining it, like so:
```
s32(s32 x) func;

// ...

func {
  // ...
}
```
This enables support for mutual recursion.

### Statements

There are several types of statements. We have type/variable declarations/definitions, `include`, `typedef`, `extern`, `if`, `while`, `for`, `break`, `continue` and `return`. Statements can also be code blocks or expressions.

There are 2 types of code blocks: multiline and oneline. Multiline code blocks are your standard code blocks composed of multiple statements, and oneline code blocks are a cleaner way to write code blocks that just have one statement.

Here's an example of both:
```
// multiline code block
{
  ...; // statement 1
  ...; // statement 2
  ...; // statement 3
}

// oneline code block
-> ...;
```
Multiline code blocks don't require a semicolon at the end, however oneline code blocks must be terminated with a semicolon like any other statement.

You can't use oneline code blocks to define a function. You have to use multiline ones.

To declare/define a type/variable, you can use this syntax:
```
type name;
```
Of course, you can assign a value right away:
```
s32 variable = 69;
```
Or chain multiple declarations of the same type:
```
s32* ptr1, ptr2 = &variable;
```
Unlike in C, **both** values are actually pointers.

If a type specification includes a named struct, it gets automatically registered as a type alias in the current scope
```
struct SomeStruct {
  s32 field1;
  u32 field2;
}* var1;

SomeStruct* var2 = var1;
```
If statements are also quite simple. They're different syntactically from C, mainly because parentheses aren't mandatory. The expression is terminated with a code block, it can be both a multiline and a oneline code block. The code block gets evaluated only if the expression evaluates to a truthy value, which is a value that isn't 0. This means it cannot return a struct or a void, as they're non-scalar values and cannot be translated to a single number easily.
```
if variable { ... }
if variable -> ...;
```
If you put an `else` statement, its code block gets evaluated if the if statement fails, as in the evaluated expression returns a 0. The `else` statement must immediately be followed by a code block.
```
if variable -> ...;
else -> ...;
```
This means that if trees look like this:
```
if condition1 -> ...;
else -> if condition2 -> ...;
else -> if condition3 -> ...;
else -> ...;
```
Or you can use the `elif` keyword, which translates directly to `else -> if`, so the previous example, with `elif` used, looks like this:
```
if condition1 -> ...;
elif condition2 -> ...;
elif condition3 -> ...;
else -> ...;
```
If you want to loop a code block, you can use the `while` statement. It works like an `if` statement, but doesn't have an `else` component, and it runs the code block over and over again until the condition is evaluated to 0. The syntax is the same:
```
while condition -> ...;
```
You can also use a `;` directly after the condition if you don't need a body:
```
while condition;
```
However, if you want to loop through a predefined range of values, like this:
```
s32 iter = 0;
while iter < 5 -> printf("%d\n", iter++);
```
then using a `for` statement does that for you, with this syntax:
```
for s32 iter in [0, 5) -> printf("%d\n", iter);
```
The type of the iterator variable **must** be a numeric value, which means the base types that aren't `struct` and `void`, followed by the `in` keyword and then a range literal. If a bracket (`[]`) is specified, the value is inclusive, and if a parenthesis is specified (`()`), the value is exclusive. Here are the ranges and which values they include:
```
(1, 5)  2, 3, 4
[1, 5)  1, 2, 3, 4
(1, 5]  2, 3, 4, 5
[1, 5]  1, 2, 3, 4, 5
```
If the bigger value is at the start and the lower value at the end, the iterator goes backwards:
```
for s32 iter in [5, 1] -> printf("%d\n", iter);
```
would print:
```
5
4
3
2
1
```
There are also ways to interrupt a loop, and thats by using `continue` and `break`. `continue` interrupts the current iteration and moves on to the next, and `break` stops the loop altogether prematurely:
```
for s32 i in [1, 10] {
  if i == 5 -> continue;
  if i == 8 -> break;
  printf("%d\n", i);
}
```
would print:
```
1
2
3
4
6
7
```
Notice how `5` gets skipped and nothing gets printed after `7`.

### Expressions

Expressions are essentially a set of operands and operators. Operands can be anything from other expressions to variables and/or literal values. Operands can also have their own unary operators, both before the root (prefixed operators) and after the root (suffixed operators).

All operators are pretty much the same as in C, down to operator precedence. The differences is a lack of a `->` operator, which in C is used to traverse struct that are pointed to by pointers. In PawScript, all structs are struct pointers, as mentioned before, so the `.` operator is used for struct pointers. Accessing a non-pointer struct via the `.` operator will automatically convert it to a pointer pointing to that struct.

The `*of` built-in functions are reworked. `sizeof` only accepts types and variables, not full expressions. The syntax of `offsetof` was changed to `offsetof<struct_name>.field`. `alignof` and `typeof` are missing as they were deemed unnecessary.

Another difference is the ternary operator. Instead of `?:` it uses this syntax: `if condition -> [when_true; when_false]`.

Casting has a different syntax, instead of the usual `(s32)expression` you can use `cast<s32>(expression)`. Casting also follows the same rules as explicit casting in C, both implicit and explicit. Alternatively, you can use `bitcast<s32>(expression)`, which reinterprets the raw bits without any processing. It adds zeroes to higher bits if the destination is larger, and truncating if the destination is smaller.

Integer, floating point, string and character literals follow the same rules as in C, except the suffixes in integer and floating point literals do nothing. Integers evaluate to `s32` if below 2³¹, `s64` if below 2⁶³, and `u64` otherwise. Floating point literals always evaluate to `f64`.

### Varargs

In order to ensure full interopability with C, especially for functions like `printf`.
If you have `...` as the last argument, then the function takes an infinite number of arguments.
The syntax looks like this:
```
void(s32 fixed_arg, ...) func;
```
Unlike in C, you don't need any fixed arguments, `void(...)` is a valid declaration. However, this doesn't work for externs, as C doesn't allow this.

To access a vararg, you can use the array notation:
```
s32(...) func {
  return ...[1];
}

func(69, 420, 123);
```
In this case, `func` returns 420.

You can also get the number of arguments using `sizeof` with this syntax: `sizeof(...)`. In `func`'s case, this would evaluate to 3.

### `include`

`include` essentially just runs other script files in the same context.

It is not the same as evaluating the script in it's place. It only exposes the global scope to the script.

There are safeguards in place, you can't run a file that's already been ran.

It takes a path to a file from the interpreter's CWD:
```
include "module.paw"
```
The parameter must be a string literal.

You don't need to end it with a semicolon.

### `typedef`

No C-like language is complete without `typedef`! It follows the exact same structure, you use `typedef`, then a type, then an identifier. Then you can use the identifier instead of the type as an alias.

```
typedef s32 s32_but_different;

s32_but_different value;
```

### `new`

PawScript, since it's designed around a systems-like feel, has manual memory management. In order to make that a bit more bearable, the `new` keyword allows for all kinds of allocations, most notably **scoped allocations**. These allocations automatically get free'd once out of scope.

By default it allocates to the global scope:
```
s32* value = new(4);
```
An alternate way is using `<>` to enclose a type instead:
```
s32* value = new<s32>;
```
In order to create a scoped allocation, you can use the `scoped` keyword after the `new` keyword, like this:
```
s32* value1 = new scoped(4);
s32* value2 = new scoped<s32>;
```
You can allocate not just simple values, but also arrays and functions.

Since PawScript doesn't have an array type, you can use this syntax to create one:
```
s32* arr = new scoped<s32>(4); // equivalent to 'new scoped(16)'
```
C-style compound literals are implemented this way, with syntax like this:
```
SomeStruct* str = new scoped<SomeStruct> {
  field1 = 69,
  field2 = 420,
}; // for structs

s32* arr_explicit = new scoped<s32>(3) { 1, 2, 3 }; // for arrays
s32* arr_implicit = new scoped<s32>()  { 1, 2, 3 }; // you can omit the array size if youre going to specify the values afterwards
```
For struct compound literals, each value must be explicitly assigned to a set field, so code like this:
```
new scoped<SomeStruct> { field1, field2 };
```
will throw an error.

Did I mention functions, yeah, those work too. You can define anonymous functions like this:
```
new scoped<void()> { ... };
```
As specified before, you can't use oneline code blocks to define functions.

Remember the function definition example from before? That one is just syntax sugar for this:
```
s32(s32 x) func = new scoped<s32(s32 x)> { ... };
```
However, you **cannot** use local variables in local functions.
```
s32() func {
  s32 variable = 3;
  void() local_func {
    variable = 5; // Error: Undefined identifier 'variable'
  }
  local_func();
  return variable;
}
```
Closures will probably be implemented in the future, but I'm not willing to go through the pain right now.

Remember to promote the function to a global scope if you plan to return it or use it outside of the parent function in some way.

To free an allocation, you can use the `delete` keyword, like so:
```
delete(value);
```
This doesn't reset `value`'s value to something like `null` though, you have to do that manually. However, the `delete` operation does always evaluate to `null`, so something like this:
```
value = delete(value);
```
is sufficient.

If you need to push a scoped allocation to a higher scope, or even make it global, you can use the `promote` keyword:
```
promote(value);
```
During promotion, the pointer doesn't change. It will still point to the exact same data as before.

You can promote to higher scopes by appending an integer literal after `promote`, like this:
```
promote 2(value);
```
This **has** to be an integer literal though, you can't use expressions.

You can also use the `global` keyword instead to guarantee a promotion to the global scope, which pretty much makes the allocation permanent unless free'd:
```
promote global(value);
```
The `adopt` keyword lets the current scope take ownership of an allocation. I'm going to admit, I could've chosen `demote` but `adopt` just sounds funnier.
```
void*(s8* message) log_error {
  printf("! ERROR: %s\n", message);
  return null;
}

void*() allocate {
  void* very_big_allocation_DONT_LEAK = new scoped(123456789);
  // ...
  if some_error_happened -> return log_error(error);
  // ...
  return promote global(very_big_allocation_DONT_LEAK);
}

// non-adopt example:
void* my_allocation = allocate(); // allocation exists in global scope
if another_error_happened -> return log_error(error); // my_allocation would leak

// adopt example:
void* my_allocation = adopt(allocate()); // the current scope now "adopted" the allocation from the global scope, it now owns it
if another_error_happened -> return log_error(error); // my_allocation would get properly disposed
```

### `extern`

`extern` variable definitions allow you to import variables from the native context of the host program, but only if they're linked as visible (this means it doesn't work for static variables).

The syntax is the same as with declaring a variable, but you can't assign to it during declaration:
```
extern s32 value;     // valid
extern s32 value = 5; // illegal
```
Because Windows is Windows and Microsoft is Microsoft, (#windows-difficulties)[you have to do some things to get this working on Windows].

### Memory Checking

The language includes a memory checking feature. All variables keep track if they originate from the script or from the native context.
A variable is not marked as native by default, but becomes native if it's:
- declared with `extern`
- assigned a native variable
- returned by a native function
- dereferenced from a native pointer variable (including structs)

If a script is about to dereference a pointer, it checks if the pointer is `null` regardless of if it's native or not.
Then, the variable is checked if it's native, if yes, it is allowed to dereference.
If not, it is further checked if the pointer lies in script allocated memory. If yes, it is allowed to dereference, if not, an error is thrown.

Native promotion can be bypassed with low-level operations like `memcpy`:
```
void* script_ptr;
void* native_ptr;
memcpy(&script_ptr, &native_ptr, sizeof(void*));
```
You can also promote a script pointer to a native one manually using the `?` operator:
```
void* native_ptr = script_ptr?;
```
This doesn't make `script_ptr` native though. However, using `??` will make `script_ptr` native as well:
```
script_ptr??;
// is equivalent to
script_ptr = script_ptr?;
```
It can be paired with struct accesses or array accesses, which looks funny:
```
a_struct?.field;
an_array?[0];
```

## Engine API

The API itself is designed to be as easy to use as possible, with emphasis on minimal API boilerplate and plug-and-play. To run code, you just need 3 lines:
```c
const char* code = "...";

PawScriptContext* context = pawscript_create_context();
pawscript_run(context, code);
pawscript_destroy_context(context);
```
Alternatively, you can use `pawscript_run_file`:
```c
PawScriptContext* context = pawscript_create_context();
pawscript_run_filename(context, "script.paw");
pawscript_destroy_context(context);
```

Of course, if you plan to use the language for not just running it once, it's useful to not destroy the context.

### Error Checking

To check for errors, you can use `pawscript_any_errors` to check if there are any errors, and then `pawscript_error` to retreive an error off the error stack.
```c
if (pawscript_any_errors(context)) {
    PawScriptError* error;
    while (pawscript_error(context, &error)) {
        printf("(%d:%d) %s\n", error->row, error->col, error->msg);
    }
}
else printf("No errors encountered.\n");
```

### Getting variables

You can get variables from the scripting context quite easily, and that is with the `pawscript_get` function:
```c
int value;
pawscript_get(context, "value", &value); // assuming value is an s32
```
This function is quite powerful, as it also allows you to call script functions quite easily:
```c
int(*func)(int x);
pawscript_get(context, "func", &func);
int out = func(5);
```
The function written to `func` is actually a JIT compiled function trampoline that converts the passed arguments to the script context, evaluates the function, and returns the result accordingly.

Unfortunately, you can't easily get type metadata or the count of varargs in C, so in order to call a function that has varargs, you have to do a bunch of things.
PawScript provides several macros to help you with this though.

You can use the `ETC` macro in place of the varargs when specifying the function type,
so if a function definition is `void(s32 a, f64 b, ...)`, the C equivalent would be `void(*)(int a, double b, ETC);`

To call the function, you can use the `VARARGS` macros, which takes a bunch of `ITEM`s, which represent a single argument.
The `ITEM` macro takes 2 parameters, the type and the value. The type is passed as a string containing the PawScript type specification, and the value is, well, the value:
```c
void(*varargs_example)(int a, double b, ETC);
pawscript_get(context, "varargs_example", &varargs_example);
varargs_example(3, 1.5, VARARGS(
    ITEM("s64", 5),
    ITEM("void*", NULL),
));
```

> [!NOTE]
> Make sure to pass a proper type to the `ITEM` macro. For example, passing an `int` to a `s64` may make the higher bits garbage.
> A simple cast is enough to fix it.

### Setting variables

You can set variables too if you want to:
```c
int func(int x) {
    return x + 3;
}

pawscript_set(context, "func", &func); // you pass in a function pointer
pawscript_run(context, "func(2);");
```
The variable has to be declared first, otherwise the engine won't know its type.

The engine calls native functions via a custom FFI implementation.

### Symbol Blacklist/Whitelist

You might not want scripts to access some functions or variables in your program. You can use `pawscript_symbol_visibility` and `pawscript_register_symbol` to do just that.
By default, the visibility is set to `VISIBILITY_BLACKLIST`, meaning that registered symbols are blacklisted.
Of course, since there are no registered symbols by default, that effectively makes it see every symbol in your program.

In order to configure a whitelist, you can use this code:
```c
pawscript_symbol_visibility(context, VISIBILITY_WHITELIST);
pawscript_register_symbol(context, &some_variable);
pawscript_register_symbol(context, &another_variable);
pawscript_register_symbol(context, a_function);
```
This makes it so scripts can only use `some_variable`, `another_variable` and `a_function`

### Reflection

The interpreter allows for some basic reflection on the C side of things.

To get a type of a variable, you can use `pawscript_get_type` like this:
```c
PawScriptType* type = pawscript_get_type(context, "variable");
```
Don't forget to destroy the type after you're done using it:
```c
pawscript_destroy_type(type);
```
The `PawScriptType` struct isn't immutable, but `pawscript_get_type` returns a copy of its type, so you cannot modify the internal type.

The function can also return typedefs.

It's layout is like this:
```c
bool is_const;
bool is_unsigned;
bool is_native;         // used in memory checks, specifically if a pointer is script-originated or native-originated
bool is_incomplete;     // used to check if a struct is incomplete, pretty much always false since this is only used for recursive structs
PawScriptTypeKind kind; // the kind of the type
char* name;             // holds the variable/struct field/function argument/typedef name, NULL if not
PawScriptType* orig;    // used internally to point to the original copy of the type metadata
                        // for types obtained using pawscript_get_type it always points to itself
```
The `PawScriptTypeKind` is an enum with these fields:
```c
TYPE_VOID     = 0
TYPE_8BIT     = 1
TYPE_16BIT    = 2
TYPE_32BIT    = 3
TYPE_64BIT    = 4
TYPE_32FLT    = 5
TYPE_64FLT    = 6
TYPE_FUNCTION = 7
TYPE_POINTER  = 8
TYPE_STRUCT   = 9
```
They're self explanatory. The struct also holds some extra information for pointers, functions and struct definitions, for their respective type kind.

For pointers:
```c
PawScriptType* pointer_info.base   // holds the base type of the pointer
```
For structs:
```c
size_t struct_info.num_fields        // number of fields
size_t* struct_info.offsets          // array of offsets for fields
PawScriptType** struct_info.fields   // array of fields
```
For functions:
```c
PawScriptType* function_info.return_value   // return type
PawScriptType** function_info.arg_types     // array of arguments
size_t function_info.num_args               // number of arguments
```

### Windows Difficulties

On UNIX-like systems by default, all non-static symbols are visible for other programs like `readelf`, or even to itself using `dlsym`. Windows doesn't play nice with this. You have to **explicitly** tell the linker to make your symbols visible using `__declspec(dllexport)` or with an export table you provide to the linker. Either that, or you have to compile your program as a library, and then write a small program to load that library and run its main function. It's convoluted, but let's be real, that's the average C/C++ developer experience on Windows.

## The Future

I'm considering implementing these in the future to the language, but it's not guaranteed.

### Types as variables

A neat feature I came up with very late in development that could serve as an implementation of generics.
Maybe you could check if a variable is a specific type with `typeof(variable) == s32`.
`typedef`s would look like `type TypeName = u64;` or something.
It would also integrate with the operators, so something like `&s16` would be an equivalent to `s16*`,
or you could get the base type like this: `*typeof(ptr) == bool` (I don't think I'll make `typeof(variable) = type` valid syntax...)
Struct traversal could be something like `typeof(some_struct).field`, which would get the type of the `field` field.
Functions are a bit complicated, I could add something like `typeof(s32)()` of course, but designing dereference is a bit difficult.
Could be something like `void(s16 a, u64 b, f32 c)[1] == u64` or something, then have the dereference operator for getting the return type.

It would integrate well with the varargs feature and introduce overall support for reflection in the language, which would be a huge plus.

I did mention that it came in late in development when all the logic has been implemented,
so I'd basically have to rewrite the expression and type logic for this to work.
I'd also have to think of the way I could integrate it with the whole C interop, which is like, the main thing of this language.

### Closures

I did mention this before, but adding closures could be huge...if they weren't a pain to implement.
The current scope system is simply not designed for this kind of task.
For now, you can use the C way of passing a pointer to a value you want the function to modify.

### Bytecode Compiler

The language had a bytecode compiler at one point but I didn't design it very well so I scrapped it and went straight to evaluation from lexing.
Sure, it's awful in terms of performance, but it keeps the interpreter simple. Of course, I do want to add a bytecode VM someday, but not through a conventional instruction set,
like "LOAD", "PUSH", "STORE", or anything like that. Maybe just as a binary AST tree, it would keep the compiler simple and also me from going insane.

### Error Handling

Some way to handle non-syntax errors with the `try` keyword without stopping the entire script.
