#ifndef RISCV_CODEGEN_H
#define RISCV_CODEGEN_H

#include "tac.h"
#include "symbol_table.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

// ============================================================================
// RISC-V Code Generator
// Translates Three-Address Code (TAC) to RISC-V assembly
// ============================================================================

// RISC-V Register definitions (using ABI names)
enum RISCVRegister
{
    // Hard-wired zero
    REG_ZERO = 0, // x0: Always zero

    // Return address
    REG_RA = 1, // x1: Return address

    // Stack pointer
    REG_SP = 2, // x2: Stack pointer

    // Global pointer
    REG_GP = 3, // x3: Global pointer

    // Thread pointer
    REG_TP = 4, // x4: Thread pointer

    // Temporaries (caller-saved)
    REG_T0 = 5, // x5: Temporary
    REG_T1 = 6, // x6: Temporary
    REG_T2 = 7, // x7: Temporary

    // Saved / Frame pointer
    REG_S0 = 8, // x8: Saved register / Frame pointer
    REG_FP = 8, // Alias for s0
    REG_S1 = 9, // x9: Saved register

    // Function arguments and return values
    REG_A0 = 10, // x10: Argument/Return value 0
    REG_A1 = 11, // x11: Argument/Return value 1
    REG_A2 = 12, // x12: Argument 2
    REG_A3 = 13, // x13: Argument 3
    REG_A4 = 14, // x14: Argument 4
    REG_A5 = 15, // x15: Argument 5
    REG_A6 = 16, // x16: Argument 6
    REG_A7 = 17, // x17: Argument 7

    // More saved registers (callee-saved)
    REG_S2 = 18,  // x18: Saved register
    REG_S3 = 19,  // x19: Saved register
    REG_S4 = 20,  // x20: Saved register
    REG_S5 = 21,  // x21: Saved register
    REG_S6 = 22,  // x22: Saved register
    REG_S7 = 23,  // x23: Saved register
    REG_S8 = 24,  // x24: Saved register
    REG_S9 = 25,  // x25: Saved register
    REG_S10 = 26, // x26: Saved register
    REG_S11 = 27, // x27: Saved register

    // More temporaries
    REG_T3 = 28, // x28: Temporary
    REG_T4 = 29, // x29: Temporary
    REG_T5 = 30, // x30: Temporary
    REG_T6 = 31, // x31: Temporary

    // Floating-point temporaries (caller-saved)
    REG_FT0 = 32,  // f0: FP temporary
    REG_FT1 = 33,  // f1: FP temporary
    REG_FT2 = 34,  // f2: FP temporary
    REG_FT3 = 35,  // f3: FP temporary
    REG_FT4 = 36,  // f4: FP temporary
    REG_FT5 = 37,  // f5: FP temporary
    REG_FT6 = 38,  // f6: FP temporary
    REG_FT7 = 39,  // f7: FP temporary
    REG_FT8 = 40,  // f8: FP temporary
    REG_FT9 = 41,  // f9: FP temporary
    REG_FT10 = 42, // f10: FP temporary
    REG_FT11 = 43, // f11: FP temporary

    // Floating-point saved registers (callee-saved)
    REG_FS0 = 44,  // f8: FP saved register
    REG_FS1 = 45,  // f9: FP saved register
    REG_FS2 = 46,  // f18: FP saved register
    REG_FS3 = 47,  // f19: FP saved register
    REG_FS4 = 48,  // f20: FP saved register
    REG_FS5 = 49,  // f21: FP saved register
    REG_FS6 = 50,  // f22: FP saved register
    REG_FS7 = 51,  // f23: FP saved register
    REG_FS8 = 52,  // f24: FP saved register
    REG_FS9 = 53,  // f25: FP saved register
    REG_FS10 = 54, // f26: FP saved register
    REG_FS11 = 55, // f27: FP saved register

    // Floating-point arguments and return values
    REG_FA0 = 56, // f10: FP argument/return value 0
    REG_FA1 = 57, // f11: FP argument/return value 1
    REG_FA2 = 58, // f12: FP argument 2
    REG_FA3 = 59, // f13: FP argument 3
    REG_FA4 = 60, // f14: FP argument 4
    REG_FA5 = 61, // f15: FP argument 5
    REG_FA6 = 62, // f16: FP argument 6
    REG_FA7 = 63  // f17: FP argument 7
};

class RISCVCodeGenerator
{
private:
    std::ofstream output;
    std::string current_function;

    // Symbol table for type information
    SymbolTable *symtab;

    // Stack frame management
    int stack_offset;
    int max_stack_size;
    int current_frame_size;
    std::unordered_map<std::string, int> var_offsets; // Variable name -> stack offset

    // String literals storage
    std::vector<std::pair<std::string, std::string>> string_literals; // label -> content
    int string_counter;

    // Double constants storage
    std::vector<std::pair<std::string, std::string>> double_constants; // label -> value
    int double_counter;

    // Label management
    int label_counter;

    // Register names (ABI names)
    std::string reg_name(RISCVRegister reg);

    // Helper functions for code generation
    void emit(const std::string &instr);
    void emit_comment(const std::string &comment);
    void emit_label(const std::string &label);

    // Stack management
    int allocate_stack_var(const std::string &var_name, int size);
    int get_var_offset(const std::string &var_name);
    
    // Helper for large offsets
    void emit_load_with_offset(const std::string &load_instr, const std::string &dest_reg, int offset, const std::string &base_reg);
    void emit_store_with_offset(const std::string &store_instr, const std::string &src_reg, int offset, const std::string &base_reg);

    // Load operand into a register
    void load_operand(const TACOperand &operand, RISCVRegister dest_reg, int str_idx = -1);

    // Store register value to operand location
    void store_to_operand(RISCVRegister src_reg, const TACOperand &operand);

    // Get type of an operand
    Type get_operand_type(const TACOperand &operand);

    // Check if operand is double type
    bool is_double_operand(const TACOperand &operand);

    // Generate assembly for specific TAC instructions
    void generate_assign(const TACInstruction *instr);
    void generate_unary(const TACInstruction *instr);
    void generate_arithmetic(const TACInstruction *instr);
    void generate_comparison(const TACInstruction *instr);
    void generate_addr_of(const TACInstruction *instr);
    void generate_deref(const TACInstruction *instr);
    void generate_deref_store(const TACInstruction *instr);
    void generate_call(const TACInstruction *instr);
    void generate_param(const TACInstruction *instr);
    void generate_return(const TACInstruction *instr);
    void generate_label(const TACInstruction *instr);
    void generate_goto(const TACInstruction *instr);
    void generate_conditional(const TACInstruction *instr);

    // Built-in function handling
    void generate_print_int();
    void generate_print_char();
    void generate_print_double();
    void generate_print_string();
    void generate_print_newline();
    void generate_scan_int();
    void generate_scan_double();
    void generate_scan_char();

    // Data section
    void generate_data_section();

    // Function prologue and epilogue
    void generate_prologue(const std::string &function_name);
    void generate_epilogue();

    // Param handling for function calls
    std::vector<TACOperand> pending_params;

    // Helper to get load/store instructions based on type
    void get_load_store_instructions(PrimitiveType type, std::string &load_instr, std::string &store_instr);

public:
    RISCVCodeGenerator(const std::string &output_file, SymbolTable *st = nullptr);
    ~RISCVCodeGenerator();

    // Main generation function
    void generate(const std::vector<TACInstruction *> &tac_code);

    // Generate built-in I/O functions
    void generate_builtin_functions();
};

#endif // RISCV_CODEGEN_H
