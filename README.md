# c4

`c4` is a small C-subset compiler written in C++20 for a compiler lab project. The repository contains a complete front end with lexing, parsing, AST construction, semantic analysis, and an LLVM-based IR backend under active development.

The executable is named `c4` and can be used to:

- tokenize source files
- print a parse tree
- print the reconstructed AST
- attempt LLVM IR generation

## Overview

The compiler pipeline in this repo is organized as:

1. **Lexing**: convert source text into tokens with source locations.
2. **Parsing**: build a parse tree for a C-like grammar.
3. **AST construction**: lower the parse tree into a cleaner AST.
4. **Semantic analysis**: resolve names, check types, validate control flow, and reject unsupported constructs.
5. **IR generation**: lower the typed AST to LLVM IR.

The codebase is aimed at a focused teaching subset of C, not full ISO C.

## Supported Language Subset

Based on the current implementation, the front end supports:

- builtin types: `int`, `char`, `void`
- named `struct` types
- pointers, including pointer-to-function forms
- global declarations and function definitions
- block scope declarations
- statements: compound blocks, expression statements, `if`/`else`, `while`, labels, `goto`, `continue`, `break`, `return`
- expressions: identifiers, decimal constants, character constants, string literals, calls, `.` / `->`, `[]`, unary `&`, `*`, `!`, unary `-`, `sizeof`, pre/post `++` and `--`
- arithmetic and logical operators: `*`, `/`, `%`, `+`, `-`, comparisons, equality, `&&`, `||`, ternary `?:`, and assignment

Important current limitations from semantic analysis and code generation include:

- this is **not** a full C compiler
- anonymous structs are not supported
- functions returning structs are not supported
- struct parameters are not supported
- assignment of struct values is not supported
- front-end inspection modes are currently more reliable than the LLVM backend path

## Repository Layout

```text
.
├── CMakeLists.txt
├── src
│   ├── lexer        # tokenization
│   ├── parser       # parse tree construction
│   ├── ast          # AST data structures + AST builder
│   ├── semantic     # semantic analysis and type checking
│   ├── ir           # LLVM IR generation
│   ├── prettyPrint  # parse tree pretty printer
│   └── helper       # shared utilities, symbols, diagnostics, token/node structs
├── test
│   └── lexer        # sample input programs used as regression inputs
└── common
    ├── assignments  # assignment PDFs
    └── references   # reference material
```

## Build Requirements

You will need:

- CMake 3.16 or newer
- a C++20 compiler
- LLVM **21.1** development files
- Zlib headers/libraries if your LLVM build depends on them

The current `CMakeLists.txt` expects LLVM via `find_package(LLVM 21.1 REQUIRED CONFIG)`, so on a fresh machine you will usually need to pass `LLVM_DIR` explicitly.

## Build

Configure and build with CMake:

```bash
cmake -S . -B build -DLLVM_DIR=/absolute/path/to/lib/cmake/llvm
cmake --build build
```

If your LLVM installation is discoverable through `CMAKE_PREFIX_PATH`, that may work as well, but `LLVM_DIR` is the most direct option for this project.

The compiled binary is written to:

```text
build/bin/c4
```

## Usage

```text
c4 [--tokenize|--tokenize_verbose|--parse|--parse_verbose|--print-ast|--compile] file
```

If you pass only a file path, `c4` defaults to `--compile`.

### Modes

- `--tokenize`: print one token per line with `file:line:column` location info
- `--tokenize_verbose`: same phase with extra tokenizer debug output
- `--parse`: parse the file, run semantic analysis, and print the parse tree on success
- `--parse_verbose`: parse mode with extra parser debug output
- `--print-ast`: build and print the AST after successful semantic analysis
- `--compile`: attempt to lower the input to LLVM IR

### Example Commands

Tokenize a sample program:

```bash
./build/bin/c4 --tokenize test/lexer/test_expr.c
```

Parse and print the parse tree for a struct declaration:

```bash
./build/bin/c4 --parse test/lexer/test_parser1.c
```

Print the AST for a larger sample:

```bash
./build/bin/c4 --print-ast test/lexer/pretty.c
```

Try the compile path:

```bash
./build/bin/c4 --compile path/to/file.c
```

The intended output of the compile mode is an LLVM IR file named:

```text
<input-stem>.ll
```

written to the current working directory.

## Error Reporting

Diagnostics use a standard compiler-style format:

```text
file:line:column: error: message
```

This makes it easy to use the tool from a terminal or editor integration.

## Sample Inputs

Useful example files live in [`test/lexer`](test/lexer):

- `test_expr.c`: small expression/function sample
- `test_parser1.c`: struct declaration
- `pretty.c`: broader syntax stress test for parsing and AST printing
- `test_rekursive_funktion.c`: semantic error example due undeclared `printf`

## Status

The strongest parts of the project today are the front-end stages:

- lexing
- parsing
- AST construction
- semantic analysis

The LLVM IR backend already exists in [`src/ir`](src/ir), but it should be treated as work in progress when documenting or demoing the project.

## References

Supporting material for the lab is included in:

- [`common/assignments`](common/assignments)
- [`common/references`](common/references)

## License

No license file is currently included in the repository. If you plan to publish or redistribute this project, add an explicit license first.

## Contributors

I worked on this project alongside:

- [Vinzenz Benedikt Brehme](https://github.com/justaVinz)
- [Michael Alexander Warnecke](https://github.com/bitsopher)
