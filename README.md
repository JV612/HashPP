# HashPP

A toy compiler that compiles a subset of C/C++ to RISC-V assembly, featuring support for basic C constructs along with advanced features like classes, inheritance, and file I/O.

## Setup & Installation

### Prerequisites
- RISC-V toolchain (`riscv64-elf-gcc` or `riscv64-linux-gnu-gcc`)
- Spike RISC-V ISA Simulator and Proxy Kernel (pk)
- Bison (parser generator)
- Flex (lexical analyzer)
- g++ (C++ compiler for building the compiler)

### Installation

1. **Install dependencies** (automated):
   ```bash
   make install-deps
   ```
   This will automatically detect your package manager and install the required tools.

2. **Build the compiler**:
   ```bash
   make
   ```
   This generates the `parser` executable.

### Running the Compiler

Use the provided `run.sh` script to compile and execute a single C file:

```bash
./run.sh <input_file.c>
```

**Example:**
```bash
./run.sh testfiles/1_arithmetic_bitwise.c
```

This will:
1. Compile your C file to RISC-V assembly (`output.s`)
2. Assemble and link the code
3. Run it on Spike ISA simulator
4. Display the program output

### Manual Compilation Steps

If you prefer manual control:

```bash
# Build the compiler
make

# Compile C file to RISC-V assembly
./parser input.c

# Assemble and link
riscv64-elf-gcc -static -nostdlib -nostartfiles -T linker.ld -o output output.s

# Run on Spike with proxy kernel
spike pk ./output
```

### Clean Build Artifacts

```bash
make clean      # Remove build files
make distclean  # Remove all generated files including test results
```

---

## Supported Features

### Basic Features

#### 1. **Data Types**
- **Primitive types**: `int`, `char`, `double`, `bool`, `void`
- **Typedef**: Custom type aliases
- **Enumerations**: Named integer constants with explicit values

#### 2. **Variables & Constants**
- Variable declarations and initialization
- Integer, double, character, and boolean constants
- String literals
- `NULL` and `nullptr` constants
- Static variables

#### 3. **Operators**

**Arithmetic**: `+`, `-`, `*`, `/`, `%` (including unary `+`, `-`)

**Bitwise**: `&`, `|`, `^`, `~`, `<<`, `>>`

**Logical**: `&&`, `||`, `!`

**Comparison**: `==`, `!=`, `<`, `>`, `<=`, `>=`

**Assignment**: `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`

**Increment/Decrement**: `++`, `--` (both prefix and postfix)

**Address & Dereference**: `&`, `*`

**Member Access**: `.` (direct), `->` (pointer)

#### 4. **Control Flow**

**Conditionals**:
- `if`
- `if-else`
- `switch-case-default`

**Loops**:
- `for` (with declaration in initializer)
- `while`
- `do-while`
- `until` (loop until condition becomes true)

**Jump statements**: `break`, `continue`, `return`, `goto`

#### 5. **Functions**
- Function declarations and definitions
- Return types (including `void`)
- Parameter passing (call by value)
- Call by reference using pointers
- Function calls in expressions
- **Recursive function calls** fully supported with proper stack management

#### 6. **Pointers**
- Pointer declaration and initialization
- Pointer dereferencing (`*ptr`)
- Address-of operator (`&var`)
- Pointer arithmetic (addition/subtraction with integers)
- Multi-level pointers (pointer to pointer: `int**`)
- Null pointer checks with NULL/nullptr

#### 7. **Arrays**
- Single-dimensional arrays with fixed size
- Array element access via index
- Array initialization
- Nested array indexing
- Variable indices
- Pointer-array interoperability

#### 8. **Structures (struct)**
- Struct definition with multiple members
- Struct variable declaration
- Member access using `.` operator
- Struct initialization
- Array of structs

#### 9. **Unions**
- Union definition
- Member access
- Shared memory behavior between members

#### 10. **Enumerations (enum)**
- Named enum types
- Explicit enumerator values
- Enum variable declarations
- Enum comparisons in conditionals

#### 11. **Storage Classes**
- `static` variables (with proper scoping)
- `typedef` for type aliases

### Advanced Features

#### 1. **Classes & Objects**
- **Class definition** with member variables and methods
- **Object instantiation** and initialization
- **Method calls** using `.` operator
- **Constructors**: 
  - Default constructors (no parameters)
  - Parameterized constructors
  - Constructor-style initialization: `class Type var(args);`
- **Destructors** (`~ClassName()`) - grammar support, limited testing
- **Implicit object passing**: Member variables accessed automatically within methods via object pointer (param_0)
- **Array of objects**


#### 2. **Inheritance**
- **Single inheritance**: `class Derived : public Base` (Note : Only single Parent class is supported)
- **Access specifiers** in inheritance (`public`, `private`, `protected`)
- **Member inheritance**: Access to base class members in derived classes
- **Multi-level inheritance** support
- **Constructor chaining** (base class initialization from derived)

#### 3. **Access Control (Encapsulation)**
- **`public`**: Members accessible from outside the class
- **`private`**: Members accessible only within the class (default for classes)
- **`protected`**: Members accessible in derived classes and their subclasses
- **Access specifier sections**: Multiple `public:`/`private:`/`protected:` blocks within class definitions
- **Encapsulation**: Private/protected data members with public getter/setter methods
- **Access control enforcement**: Compile-time checking of member and method access permissions

#### 4. **File Manipulation**
- **File operations**:
  - `fopen(filename, mode)` - Open file ("r", "w", "a" modes)
  - `fclose(file)` - Close file
- **String I/O**:
  - `fputs(string, file)` - Write string to file
  - `fgets(buffer, size, file)` - Read line from file
- **Character I/O**:
  - `fputc(char, file)` - Write character
  - `fgetc(file)` - Read character
- **File positioning**:
  - `fseek(file, offset, whence)` - Seek to position
  - `ftell(file)` - Get current position
  - `rewind(file)` - Reset to beginning
- **Multiple file handling**: Open and work with multiple files simultaneously

#### 5. **Multi-dimensional Arrays**
- Array of arrays
- Nested array indexing: `arr[i][j]`
- Multi-dimensional array initialization
- Array of structs/classes
- Dynamic indexing with nested brackets

#### 6. **Dynamic Memory Management**
- **`malloc(size)`**: Allocate memory block using heap extension
- **`calloc(num, size)`**: Allocate and zero-initialize array
- **`realloc(ptr, new_size)`**: Resize memory block with data preservation
- **`free(ptr)`**: Placeholder - no actual deallocation (no-op)
- Memory allocated via `brk` system call
- Works correctly for simple allocation patterns without heavy freeing

#### 7. **Built-in I/O Functions**
These are provided by the compiler's runtime:

**Print functions**:
- `print_int(int)` - Print integer
- `print_char(char)` - Print character  
- `print_string(char*)` - Print string
- `print_newline()` - Print newline
- `print_double(double)` - Print double with 6 decimal places

**Scan functions** (input):
- `scan_int()` - Read integer from input
- `scan_char()` - Read character from input
- `scan_double()` - Read double from input

---

## Compilation Pipeline

1. **Lexical Analysis** (Flex): Tokenizes the input C/C++ code
2. **Syntax Analysis** (Bison): Parses tokens and builds AST
3. **Semantic Analysis**: Type checking, symbol table management
4. **Intermediate Representation**: Three-Address Code (TAC) generation
5. **Code Generation**: RISC-V assembly generation
6. **Register Allocation**: Efficient register usage
7. **Assembly Output**: Generates `output.s`
8. **Linking**: Uses custom `linker.ld` for proper memory layout
9. **Execution**: Runs on Spike RISC-V ISA Simulator with proxy kernel

---


## Test Files

The `testfiles/` directory contains comprehensive examples demonstrating all features:

- `1_arithmetic_bitwise.c` - Arithmetic, bitwise, and logical operations
- `2_control_flow.c` - If/else, loops, switch statements
- `3_arrays_pointers.c` - Arrays, pointers, and their operations
- `4_functions.c` - Function calls, call by value/reference
- `5_struct_union_enum.c` - Structures, unions, enumerations
- `6_class.c` - Basic classes, methods, objects
- `7_print_functions.c` - Built-in print function usage
- `8_static.c` - Static variables and storage
- `9_typedef.c` - Type definitions and aliases
- `10_malloc.c` - Memory allocation (limited)
- `11_file_io.c` - File manipulation operations
- `12_public_private.c` - Access control keywords
- `13_inheritance.c` - Class inheritance
- `14_command_line.c` - Command-line arguments
- `15_references.c` - References and reference variables
---

## Project Structure

```
HashPP/
├── Makefile              # Build system
├── run.sh                # Compilation and execution script
├── linker.ld             # RISC-V linker script
├── src/
│   ├── ansic.y           # Bison grammar (parser)
│   ├── tokenizer.lpp     # Flex lexer
│   ├── ast_base.h        # AST base classes
│   ├── expression.{h,cpp}    # Expression AST nodes
│   ├── statement.{h,cpp}     # Statement AST nodes
│   ├── declaration.{h,cpp}   # Declaration handling
│   ├── symbol_table.{h,cpp}  # Symbol table & type system
│   ├── tac.{h,cpp}           # Three-Address Code generation
│   ├── riscv_codegen.{h,cpp} # RISC-V code generation
│   ├── register_allocator.{h,cpp} # Register allocation
│   └── diagnostics.{h,cpp}   # Error reporting
└── testfiles/            # Test programs
```

---

## Architecture Highlights

### Symbol Table
- Scope management with nested scopes
- Type system with struct/class/enum support
- Function/method signature registration
- Method name mangling for overloading
- Access control tracking

### Intermediate Representation (TAC)
- Three-Address Code with explicit temporaries
- Label generation for control flow
- Support for complex expressions and statements
- Method calls with implicit object pointer (param_0) for member access

### RISC-V Code Generation
- Stack-based register allocation with spilling
- Function prologue/epilogue generation
- Method dispatch with implicit object pointer passing (param_0)
- System call interface for I/O operations (ecall)
- Floating-point instruction support (fadd, fsub, fmul, fdiv, etc.)
- String and double literal data sections

### Object-Oriented Features
- Class member layout calculation
- Method dispatch table (basic)
- Inheritance member offset adjustment
- Access control enforcement
- Constructor/destructor handling

---

## Contributing

This is an end to end compiler implementation created under Professor Awanish Pandey's guidance in the course "CSC-305. Compiler Design".

---

## Authors

Harshit Jadwani - 23114035
Jay Vaghasiya - 23114104
Kartik Goyal - 23114046
Megh Shah - 23114065
