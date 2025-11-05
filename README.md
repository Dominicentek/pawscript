# PawScript

Low level embeddable scripting language for C projects.

> [!WARNING]
> Only supports x86_64 architectures that use either System V or Win64 ABIs. (TODO: aarch64 System V support)

> [!WARNING]
> Windows and macOS untested, only tested on Linux

## Building

Simply run `make` with `clang` installed.

## Language Syntax

The language has 2 constructs: commands and expressions. A command can be an expression but an expression cannot be a command.
Expressions perform operations on values, while commands usually handle control flow. Commands can only be used in codeblocks. Non-expression commands start with a specific keyword.

### Expressions

#### Type literal

Types are a runtime construct (i.e. they're regular values). There are 12 primitive types:

| Width  | Signed | Unsigned    | Floating |
|--------|--------|-------------|----------|
| 8-bit  | `s8`   | `u8` `bool` |          |
| 16-bit | `s16`  | `u16`       |          |
| 32-bit | `s32`  | `u32`       | `f32`    |
| 64-bit | `s64`  | `u64`       | `f64`    |

There's also `void`, which represents no type, and `type`, which can hold a type.

Then there are 3 kinds of composed types, which are pointers, functions and structs. Pointers are defined via the `#` operator after a type.

> [!NOTE]
> The reason for this choice is simple. C uses asterisks (`*`), however this conflicts with the multiply operator. Since the parser doesn't know if it's parsing a type or a different value, it just knows it's parsing a value, so `s32* ptr` would get parsed as "multiply s32 by ptr". Semantically, this doesn't make sense, but the parser doesn't know that.

Functions are like pointers, except they hold pointers to executable code. They are declared as `return_type<-()`.
Inside the parentheses is a comma separated list of types. They can have an identifier, although it's optional.
At the end of the list, `...` can be specified. This represents variadic arguments.

Placing a dollar sign (`$`) between `<-` and `(` (`s32<-$()`) makes the function assignable. The function must return the exact type and the expression returned must be assignable (variable reference or pointer dereference).

Struct are declared like this: `struct {}`. Inside the curly braces is a semicolon (`;`) separated list of types, however now an identifier is required. An example struct is:
```
struct {
  s32 a;
  u32 b;
}
```
There is no such thing as struct-by-value. Each struct is a pointer. This simplifies interpreter logic as all possible values are 8 bytes wide. It also simplifies the ABI logic.

A struct can be inlined into another struct, which acts as a by-value declaration in C. Inline structs don't need an identifier, in which case, the struct acts as an anonymous struct declaration in C.
```
struct {
  Struct str;        // valid
  Struct;            // invalid
  inline Struct str; // valid
  inline Struct;     // valid
}
```
Pointers can be inlined as well:
```
struct {
  s64 value;
  inline u8# bytes;
}
```
Their offsets are the same as the previous field's offset.

Manual offset can also be specified with the at (`@`) symbol after the identifier:
```
struct {
  s64 value;
  u32 low @ 0;
  u32 high @ 4;
}
```
Or you can use the `+@` symbol for a relative offset from the previous field. This doesn't take the size of the field into account:
```
struct {
  s32 field1 @ 4;
  s32 field2 +@ 4; // offset 8, not 12
}
```

Using a colon (`:`), you can specify a parent. These two declarations are equivalent:
```
struct: Parent {}

struct { inline Parent super; }
```

#### Integer literal

Evaluates to an integer value. If the value is lower than `2^31`, it is evaluated as `s32`. If it's higher than that but lower than `2^63`, it is evaluated as `s64`. If it's outside of all of these ranges, then it's `u64`.
Hexadecimal, octal and binary digits can be specified with the standard `0x`, `0` and `0b` prefixes respectively.

#### Floating literal

Always evaluates to an `f64`. Examples: `1.5`, `.5`, `1.`, `1e+6`, `1.5e-3`, `0x1.fp5`.

#### String literal

Returns a `const s8#` pointing to an array of null-terminated characters. These escape codes are supported:
* `\\a`: Alert (`\x07`)
* `\\b`: Backspace (`\x08`)
* `\\e`: Escape character (`\x1B`)
* `\\f`: Formfeed page break (`\x0C`)
* `\\n`: New line (`\x0A`)
* `\\r`: Carriage return (`\x0D`)
* `\\t`: Horizontal tab (`\x09`)
* `\\v`: Vertical tab (`\x0B`)
* `\\\\`: Backslash (`\x5C`)
* `\\'`: Quote (`\x27`)
* `\\"`: Double quote (`\x22`)
* `\\<newline>`: New line (`\x0A`)
* `\\ooo`: Octal number
* `\\xnn`: 2 digit hexadecimal number
* `\\unnnn`: 4 digit hexadecimal number
* `\\Unnnnnnnn`: 8 digit hexadecimal number

#### Character literal

Evaluated to `s32`, value being the codepoint of the character between single quotes (`'`). Also supports escape sequences.

#### `true`

Evaluates to `u8` holding the value `1`.

#### `false`

Evaluates to `u8` holding the value `0`.

#### `null`

Evaluates to `void#` holding the value `0`.

#### Variable

Searches the context for a defined variable and evaluates to it.

#### `...[x]`

Evaluates to a variadic argument at the specified index.

#### `(x)`

Evaluates the expression inside and returns the result.

#### `sizeof(x)`

Evaluates to `u64` holding the size of the expressions value's type. If the expression gets evaluated to a `type`, it returns the size of the type it holds, not the expression's type itself.

`sizeof(...)` evaluates to the number of variadic arguments.

#### `typeof(x)`

Evaluates to `type` holding the type of the expression.

#### `scopeof(x)`

Evaluates to the scope ID of a variable.

ID of global scope is `0`, each codeblock and function call increments the ID by 1.

#### `new`

Allocates a block of memory and returns a pointer to it.

After `new`, a type in brackets (`[ ]`) is specified: `new[s32]` returns an `s32#` pointing to the allocate 4 byte block of memory.
Arrays can be allocated by specifying the size of the array in parentheses (`( )`): `new[s32](4)` returns an `s32#` pointing to the allocated 16 byte block of memory.

The `scoped` keyword can be used to allocate in the current scope, meaning the allocation would get destroyed after the scope pops: `new scoped[s32]`.

Curly braces can be added to initialize the allocation.

If the allocation allocates an array, the initializer is a comma separated list of expressions: `new[s32](4) { 0, 1, 2, 3 }`.
Otherwise, the initializer can be a scalar: `new[s32]{ 5 }`, a struct: `new[Struct]{ .field1 = 1, .field2 = 1.5 }` or a function: `new[void<-()] => { ... }`

Function initializer can capture variables in the local scope.
* `[$]{ ... }` - Capture by reference: Mutation reflects outside of function
* `[~]{ ... }` - Capture by copy: Mutation reflects in the function and future calls
* `[=]{ ... }` - Capture by value: Mutation reflects only in a single call

#### `delete(x)`

Manually deletes an allocation allocated by `new`.

#### `move(x) => [ID]`

Moves an allocation allocated by `new` into a different scope, at the end of which gets freed automatically.

`move(x) => [scopeof(this) - 1]` guarantees that it gets moved one scope frame up.

#### `if x => [a; b]`

Evaluates the expression `x` and if it's truthy, `a` gets evaluated and returned, otherwise `b` gets evaluated and returned.

#### `include "file.paw"`

Runs the file `file.paw` and evaluates to whatever that file returned.

First it searches for the file relative to the directory the current file is in, and if it isn't there, it instead uses the interpreter's current working directory.

#### Variable declaration

Defines a new function in the current scope and returns a reference to it, just like a variable reference.

Declared using an identifier after an expression. The expression must return a type to be semantically valid.

If there's a codeblock after the identifier, it gets attached to the variable if it's a function.
Just like with function allocations, `[$]`, `[~]` or `[=]` can be specified before the codeblock to enable capture of variables.

An `extern` can be specified at the beginning (`extern s32 value`). If specified, the interpreter will search all defined symbols in the host program and `value` will reflect its state.

#### Operators

Type kinds:
* `integer` - Any integer type
* `number` - Any integer or floating point type
* `pointer` - Any pointer type
* `function` - Any function type
* `struct` - Any struct type
* `type` - The `type` type
* `assignable` - Any value that can be assigned
* `any` - Anything

| Operator    | Precedence | Left-hand Side | Right-hand Side | Description      |
|-------------|------------|----------------|-----------------|------------------|
| `x -> y`    | `14` `->`  | `any`          | `type`          | Casts `x` to type `y` |
| `x ~> y`    | `14` `->`  | `any`          | `type`          | Bitcasts `x` to type `y` |
| `x ^^ y`    | `13` `->`  | `number`       | `number`        | Raises `x` to the power of `y` |
| `x * y`     | `12` `->`  | `number`       | `number`        | Multiplies `x` by `y` |
| `x / y`     | `12` `->`  | `number`       | `number`        | Divides `x` by `y` |
| `x % y`     | `12` `->`  | `number`       | `number`        | Divides `x` by `y` and returns the remainder |
| `x + y`     | `11` `->`  | `number`       | `number`        | Adds two numbers |
| `x + y`     | `11` `->`  | `pointer`      | `integer`       | Advances the pointer `x` by `y` values |
| `x + y`     | `11` `->`  | `integer`      | `pointer`       | Advances the pointer `y` by `x` values |
| `x - y`     | `11` `->`  | `number`       | `number`        | Subtracts two numbers |
| `x - y`     | `11` `->`  | `pointer`      | `integer`       | Rewinds the pointer `x` by `y` values |
| `x - y`     | `11` `->`  | `integer`      | `pointer`       | Rewinds the pointer `y` by `x` values |
| `x << y`    | `10` `->`  | `integer`      | `integer`       | Shifts `x` by `y` bits to the left |
| `x >> y`    | `10` `->`  | `integer`      | `integer`       | Shifts `x` by `y` bits to the right |
| `x < y`     | `9` `->`   | `any`          | `any`           | Checks if `x` is less than `y` |
| `x > y`     | `9` `->`   | `any`          | `any`           | Checks if `x` is greater than `y` |
| `x <= y`    | `9` `->`   | `any`          | `any`           | Checks if `x` is less than or equal to `y` |
| `x >= y`    | `9` `->`   | `any`          | `any`           | Checks if `x` is greater than or equal to `y` |
| `x == y`    | `8` `->`   | `any`          | `any`           | Checks if `x` is equal to `y` |
| `x != y`    | `8` `->`   | `any`          | `any`           | Checks if `x` is not equal to `y` |
| `x & y`     | `7` `->`   | `integer`      | `integer`       | Bitwise ANDs `x` and `y` |
| `x ^ y`     | `6` `->`   | `integer`      | `integer`       | Bitwise XORs `x` and `y` |
| `x | y`     | `5` `->`   | `integer`      | `integer`       | Bitwise ORs `x` and `y` |
| `x && y`    | `4` `->`   | `any`          | `any`           | If `x` and `y` is truthy, `1` is returned, otherwise `0` |
| `x || y`    | `3` `->`   | `any`          | `any`           | If `x` or `y` is truthy, `1` is returned, otherwise `0` |
| `x ? y`     | `2` `->`   | `any`          | `any`           | If `x` is truthy, `x` is returned, otherwise `y` |
| `x = y`     | `1` `<-`   | `assignable`   | `any`           | Assigns `y` to `x` |
| `x += y`    | `1` `<-`   | `assignable`   | `number`        | Performs `x + y` and stores the result to `x` |
| `x -= y`    | `1` `<-`   | `assignable`   | `number`        | Performs `x - y` and stores the result to `x` |
| `x *= y`    | `1` `<-`   | `assignable`   | `number`        | Performs `x * y` and stores the result to `x` |
| `x /= y`    | `1` `<-`   | `assignable`   | `number`        | Performs `x / y` and stores the result to `x` |
| `x %= y`    | `1` `<-`   | `assignable`   | `number`        | Performs `x % y` and stores the result to `x` |
| `x ^^= y`   | `1` `<-`   | `assignable`   | `number`        | Performs `x ^^ y` and stores the result to `x` |
| `x <<= y`   | `1` `<-`   | `assignable`   | `integer`       | Performs `x << y` and stores the result to `x` |
| `x >>= y`   | `1` `<-`   | `assignable`   | `integer`       | Performs `x >> y` and stores the result to `x` |
| `x &= y`    | `1` `<-`   | `assignable`   | `integer`       | Performs `x & y` and stores the result to `x` |
| `x |= y`    | `1` `<-`   | `assignable`   | `integer`       | Performs `x | y` and stores the result to `x` |
| `x ^= y`    | `1` `<-`   | `assignable`   | `integer`       | Performs `x ^ y` and stores the result to `x` |
| `x ?= y`    | `1` `<-`   | `assignable`   | `any`           | Performs `x ? y` and stores the result to `x` |
| `++x`       |            |                | `assignable`    | Increments `x` and stores it to itself |
| `--x`       |            |                | `assignable`    | Decrements `x` and stores it to itself |
| `$x`        |            |                | `assignable`    | Takes the address of `x` |
| `#x`        |            |                | `pointer`       | Dereferences pointer `x` |
| `+x`        |            |                | `number`        | Does nothing. Just, nothing. |
| `-x`        |            |                | `number`        | Negates the number `x` |
| `!x`        |            |                | `any`           | If `x` is truthy, `0` is returned, otherwise `1` |
| `~x`        |            |                | `integer`       | Flips all the bits in `x` |
| `x++`       |            | `assignable`   |                 | Increments `x` and stores it to itself, evaluates to the value pre-operation |
| `x--`       |            | `assignable`   |                 | Decrements `x` and stores it to itself, evaluates to the value pre-operation |
| `x#`        |            | `type`         |                 | Turns the type `x` into a pointer |
| `x[y]`      |            | `pointer`      |                 | Performs `#(x + y)`, however `x` must be a pointer |
| `x[...]`    |            | `struct`       |                 | Calls the array function in a struct |
| `x(...)`    |            | `function`     |                 | Calls the function |
| `x<-(...)`  |            | `type`         |                 | Turns the type `x` into a function |
| `x::scope`  |            | `pointer`      |                 | Returns the scope ID of an allocation pointed to by `x` |
| `x::size`   |            | `pointer`      |                 | Returns the size of an allocation pointed to by `x` |
| `x::length` |            | `pointer`      |                 | Performs `x::size / sizeof(#x)` |
| `x.y`       |            | `struct`       |                 | Looks up the field `y` in struct `x` |

### Commands

#### `if <expr> <codeblock> [else <codeblock|if>]`

Evaluates the expression, and if it's truthy, it runs the codeblock. Otherwise, if specified, runs the else block

#### `while <expr> <codeblock>`

Repeatedly runs the codeblock as long as the expression evaluates to a truthy value

#### `for <expr> <identifier>: <expr> [incl|excl] => <expr> [incl|expr] [step <expr>] <codeblock>`

Iterates through all values in a range. Iterator type must be `integer`. If there's no values in the range, then nothing is executed. If inclusivity isn't specified, then on the left side, `incl` (inclusive) is used by default, however `excl` (exclusive) is used by default. Step size, if unspecified, is `1`. If negative, the loop iterates backwards.

```
for s32 i: 0 => 10 => printf("%d ", i);
// "0 1 2 3 4 5 6 7 8 9 "

for s32 i: 0 excl => 10 incl => printf("%d ", i);
// "1 2 3 4 5 6 7 8 9 10 "

for s32 i: 0 excl => 10 incl step -1 => printf("%d ", i);
// "10 9 8 7 6 5 4 3 2 1 "
```

#### `return [<expr>];`

Makes a function return a value

#### `continue;`

Cancels the current loop iteration

#### `break;`

Cancels the current loop iteration and forces the loop to exit

#### `try <codeblock> [catch [silently] [as <identifier>] [<codeblock>]]`

If a runtime error occurs inside the `try` block, the `catch` block executes. The `catch` block can be omitted entirely.

When a `catch` statement is declared, the codeblock can be omitted as long as the catch is silent and doesn't declare an identifier.

By default, `catch` logs the error into `stderr`. The specification of `silently` will disable this behavior.

When an identifier is declared, the error gets stored into it, only visible in the catch block. A language error will store `void`, but a user-defined error (`throw`) will store the value thrown.

#### `throw <expr> [as <string>];`

Throws a value. By default, the program terminates, however when in a `try` block, the corresponding `catch` will execute

#### `<codeblock>`

Can either be inline (`=> ...`) or multiline (`{ ... }`). Inline must have exactly one command, but multiline can have any amount

#### `<expr>`

Evaluates the expression

## Engine API

* `PawScriptContext* pawscript_create_context()`
  * `returns`: A new script context
* `PawScriptError* pawscript_run(PawScriptContext* context, const char* code)`
  * Runs code from a string in memory
  * `returns`: `NULL` if there weren't any errors, `PawScriptError*` otherwise
* `PawScriptError* pawscript_run_file(PawScriptContext* context, const char* filename)`
  * Runs code from a file
  * `returns`: `NULL` if there weren't any errors, `PawScriptError*` otherwise
* `bool pawscript_get(PawScriptContext* context, const char* name, void* ptr)`
  * Copies the data from variable `name` into `ptr`
  * `returns`: `true` if the variable is found, `false` otherwise
* `bool pawscript_set(PawScriptContext* context, const char* name, void* ptr)`
  * Copies the data from `ptr` into variable `name`
  * `returns`: `true` if the variable is found, `false` otherwise
* `bool pawscript_print_variable(PawScriptContext* context, FILE* f, const char* name)`
  * Prints the variable `name` into file stream `f`
  * `returns`: `true` if the variable is found, `false` otherwise
* `void pawscript_log_error(PawScriptError* error, FILE* f)`
  * Logs the `error` into the file stream `f`. `error` gets destroyed
* `void pawscript_destroy_error(PawScriptError* error)`
  * Destroys `error` without logging it
* `void pawscript_destroy_context(PawScriptContext* context)`
  * Destroys the `context`
* `void on_segfault(void(*handler)(void* addr))`
  * The interpreter installs its own segfault handler to catch invalid memory accesses caused by scripts. This function can be used to install callbacks that get called if a segfault occurs outside of scripts
  * `addr` - The address that was tried to be accessed

The engine also has a special variable: `@RESULT@` (stored in the `PAWSCRIPT_RESULT` macro), which contains the result of the code last run. `pawscript_run` and `pawscript_run_file` both update this variable.

### Calling C functions from PawScript

In C, you can just define a non-static function:
```c
void function() {
  ...
}
```
and then in PawScript, you can use `extern` to use it in the script:
```
extern void<-() function;
```
Then you can use it as a regular PawScript function.

If the function is defined in the host program, you need to compile it with `-rdynamic` on Linux and use `__declspec(dllexport)` on Windows: `__declspec(dllexport) void function() {}`.
If the function is defined in a library, no extra compiler flags or function attributes are needed.

Alternatively, you can store the function directly into a variable:
```
void<-() function;
```
```c
void function() {
  ...
}

pawscript_set(context, "function", function); // function is already a pointer
```
In this case, no compiler flags or function attributes are needed as no symbol lookup is performed.

### Calling PawScript functions from C

For a function defined like
```
void<-() function { ... }
```
the function can be obtained in C like this:
```c
void(*function)();
pawscript_get(context, "function", &function);
function(); // calls the script function
```

## Standard Library

TODO
