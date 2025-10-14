#pragma once
#include <string>
#include <vector>
#include <memory>

enum class IROpcode {
    // Arithmetic operations
    ADD, SUB, MUL, DIV, MOD,
    
    // Logical operations  
    AND, OR, NOT,
    
    // Bitwise operations
    BIT_AND, BIT_OR, BIT_XOR, BIT_NOT,
    LEFT_SHIFT, RIGHT_SHIFT,
    
    // Comparison operations
    EQ, NE, LT, LE, GT, GE,
    
    // Assignment operations
    ASSIGN,
    
    // Special operations
    LOAD,      // Load from variable
    STORE,     // Store to variable
    
    // Future: Control flow, functions, etc.
    LABEL,
    JUMP,
    CJUMP
};

struct IRInstruction {
    IROpcode opcode;
    std::string result;     // Destination temporary or variable
    std::string operand1;   // First operand
    std::string operand2;   // Second operand (optional)
    std::string label;      // For labels/jumps (optional)
    int line_number;        // Source line number
    
    IRInstruction(IROpcode op, const std::string& res = "", 
                  const std::string& op1 = "", const std::string& op2 = "",
                  const std::string& lbl = "", int line = 0)
        : opcode(op), result(res), operand1(op1), operand2(op2), label(lbl), line_number(line) {}
};

class IRGenerator {
private:
    std::vector<IRInstruction> instructions;
    int temp_counter = 0;
    int label_counter = 0;

public:
    // Generate a new temporary variable
    std::string new_temp();
    
    // Generate a new label
    std::string new_label();
    
    // Add an instruction
    void emit(IROpcode opcode, const std::string& result = "",
              const std::string& operand1 = "", const std::string& operand2 = "",
              const std::string& label = "", int line = 0);
    
    // Get all instructions
    const std::vector<IRInstruction>& get_instructions() const { return instructions; }
    
    // Clear all instructions
    void clear() { instructions.clear(); temp_counter = 0; label_counter = 0; }
    
    // Convert opcode to string for output
    static std::string opcode_to_string(IROpcode opcode);
    
    // Convert opcode to operator symbol for TAC output
    static std::string get_operator_symbol(IROpcode opcode);
    
    // Print IR in readable format
    void print_ir() const;
};