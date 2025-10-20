#ifndef TAC_H
#define TAC_H

#include <string>
#include <vector>

// Three-Address Code (TAC) for Phase 1

// TAC Operand - represents a value in TAC
class TACOperand
{
public:
    enum OperandType
    {
        OPERAND_TEMP,       // Temporary variable (#t1, #t2, etc.)
        OPERAND_IDENTIFIER, // Named variable (x, y, etc.)
        OPERAND_CONSTANT,   // Literal value (5, 3.14, etc.)
        OPERAND_LABEL,      // Label (L1, L2, etc.)
        OPERAND_STRING,     // String literal ("hello", etc.)
        OPERAND_EMPTY       // Empty operand (for unary ops)
    };

    OperandType type;
    std::string name;

    TACOperand() : type(OPERAND_EMPTY), name("") {}
    TACOperand(OperandType t, const std::string &n) : type(t), name(n) {}

    std::string to_string() const;
    bool is_empty() const { return type == OPERAND_EMPTY; }
};

// TAC Operators
enum TACOp
{
    // Assignment
    TAC_ASSIGN, // x = y

    // Arithmetic
    TAC_ADD, // x = y + z
    TAC_SUB, // x = y - z
    TAC_MUL, // x = y * z
    TAC_DIV, // x = y / z
    TAC_MOD, // x = y % z

    // Unary
    TAC_UMINUS,  // x = -y
    TAC_UPLUS,   // x = +y
    TAC_PRE_INC, // ++x (x = x + 1)
    TAC_PRE_DEC, // --x (x = x - 1)

    // Pointer/Array Operations
    TAC_ADDR_OF,     // x = &y (address-of)
    TAC_DEREF,       // x = *y (dereference/load)
    TAC_DEREF_STORE, // *x = y (store through pointer)

    // Bitwise Operations (Phase 1)
    TAC_BITWISE_AND, // x = y & z
    TAC_BITWISE_OR,  // x = y | z
    TAC_BITWISE_XOR, // x = y ^ z
    TAC_BITWISE_NOT, // x = ~y
    TAC_LEFT_SHIFT,  // x = y << z
    TAC_RIGHT_SHIFT, // x = y >> z

    // Comparison Operations (Phase 1)
    TAC_LT, // x = y < z  (less than)
    TAC_GT, // x = y > z  (greater than)
    TAC_LE, // x = y <= z (less than or equal)
    TAC_GE, // x = y >= z (greater than or equal)
    TAC_EQ, // x = y == z (equal)
    TAC_NE, // x = y != z (not equal)

    // Logical Operations (Phase 1 - basic, Phase 2 - short-circuit)
    TAC_LOGICAL_AND, // x = y && z (logical AND)
    TAC_LOGICAL_OR,  // x = y || z (logical OR)
    TAC_LOGICAL_NOT, // x = !y     (logical NOT)

    // Control Flow (for Phase 2 backpatching)
    TAC_LABEL,         // L1:
    TAC_GOTO,          // goto L1
    TAC_IF_GOTO,       // if x goto L1
    TAC_IF_FALSE_GOTO, // ifFalse x goto L1

    // Function calls and return
    TAC_RETURN, // return
    TAC_PARAM,  // param x
    TAC_CALL,   // r = call f, nArgs
};

// TAC Instruction - one line of TAC
class TACInstruction
{
public:
    TACOp op;
    TACOperand result; // Left side (or target for goto)
    TACOperand arg1;   // First argument
    TACOperand arg2;   // Second argument (optional)
    int line_number;   // Line number in TAC (for backpatching)
    int target_line;   // Target line for goto/if (initially -1, filled by backpatch)

    TACInstruction(TACOp operation, TACOperand res, TACOperand a1, TACOperand a2 = TACOperand())
        : op(operation), result(res), arg1(a1), arg2(a2), line_number(-1), target_line(-1) {}

    std::string to_string() const;
};

// List of instruction indices for backpatching
typedef std::vector<int> InstructionList;

// Backpatching helper functions
InstructionList makelist(int index);
InstructionList merge(InstructionList list1, InstructionList list2);
void backpatch(InstructionList list, int target);

// TAC Code Generator
class TACGenerator
{
private:
    std::vector<TACInstruction *> code;
    int temp_counter;
    int label_counter;

public:
    TACGenerator() : temp_counter(0), label_counter(0) {}
    ~TACGenerator();

    // Generate new temporary variable
    TACOperand newTemp();

    // Generate new label
    TACOperand newLabel();

    // Emit TAC instruction and return its index
    int emit(TACOp op, TACOperand result, TACOperand arg1, TACOperand arg2 = TACOperand());

    // Get current instruction count (nextinstr)
    int nextinstr() const { return code.size(); }

    // Backpatch a list of instructions with target line
    void backpatch(InstructionList list, int target);

    // Get all generated code
    const std::vector<TACInstruction *> &getCode() const { return code; }
    std::vector<TACInstruction *> &getCode() { return code; }

    // Print TAC
    void print() const;
    void clear();
};

// Global TAC generator
extern TACGenerator tacGen;

#endif // TAC_H
