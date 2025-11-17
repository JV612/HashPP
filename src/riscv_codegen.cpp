#include "riscv_codegen.h"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;

// ============================================================================
// RISC-V Code Generator Implementation
// ============================================================================

RISCVCodeGenerator::RISCVCodeGenerator(const std::string &output_file, SymbolTable *st)
    : stack_offset(0), max_stack_size(0), current_frame_size(0), string_counter(0), double_counter(0), label_counter(0), symtab(st)
{
    output.open(output_file);
    if (!output.is_open())
    {
        cerr << "Error: Could not open output file: " << output_file << endl;
        exit(1);
    }
}

RISCVCodeGenerator::~RISCVCodeGenerator()
{
    if (output.is_open())
    {
        output.close();
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

string RISCVCodeGenerator::reg_name(RISCVRegister reg)
{
    static const char *names[] = {
        // Integer registers (x0-x31)
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
        // Floating-point registers (f0-f31)
        "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
        "ft8", "ft9", "ft10", "ft11",
        "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
        "fs8", "fs9", "fs10", "fs11",
        "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7"};
    return names[reg];
}

void RISCVCodeGenerator::emit(const string &instr)
{
    output << "    " << instr << endl;
}

void RISCVCodeGenerator::emit_comment(const string &comment)
{
    output << "    # " << comment << endl;
}

void RISCVCodeGenerator::emit_label(const string &label)
{
    output << label << ":" << endl;
}

// ============================================================================
// Built-in I/O Function Implementations
// ============================================================================

void RISCVCodeGenerator::generate_print_int()
{
    emit_comment("=== print_int: Print integer in a0 ===");
    emit_label("print_int_0");

    // Convert integer to string and print using write syscall
    // Save registers
    emit("addi sp, sp, -64");
    emit("sw ra, 60(sp)");
    emit("sw s0, 56(sp)");
    emit("mv s0, a0         # save integer to print");

    // Convert int to string (simplified - handle up to 11 digits + sign)
    emit("addi a1, sp, 32   # buffer on stack");
    emit("li t0, 10         # divisor");
    emit("li t1, 0          # digit count");
    emit("mv t2, a1         # save buffer start (for write)");
    emit("bge s0, zero, .Lpos");
    emit("li t3, '-'        # negative sign");
    emit("sb t3, 0(a1)");
    emit("addi a1, a1, 1");
    emit("addi t1, t1, 1    # count the minus sign");
    emit("sub s0, zero, s0  # make positive");
    emit_label(".Lpos");
    emit("mv t3, a1         # save digit start (for reversal)");
    emit_label(".Ldigit_loop");
    emit("remu t4, s0, t0   # get digit");
    emit("divu s0, s0, t0   # divide by 10");
    emit("addi t4, t4, '0'  # convert to ASCII");
    emit("sb t4, 0(a1)");
    emit("addi a1, a1, 1");
    emit("addi t1, t1, 1    # increment count");
    emit("bnez s0, .Ldigit_loop");

    // Reverse only the digit string (not the minus sign)
    emit("addi t4, a1, -1   # end of digits");
    emit_label(".Lreverse");
    emit("bge t3, t4, .Ldone_reverse");
    emit("lbu t5, 0(t3)");
    emit("lbu t6, 0(t4)");
    emit("sb t6, 0(t3)");
    emit("sb t5, 0(t4)");
    emit("addi t3, t3, 1");
    emit("addi t4, t4, -1");
    emit("j .Lreverse");
    emit_label(".Ldone_reverse");

    // Write syscall: write(fd, buf, count)
    emit("li a0, 1          # stdout");
    emit("mv a1, t2         # buffer start (includes minus if present)");
    emit("mv a2, t1         # total length (includes minus if present)");
    emit("li a7, 64         # syscall number for write");
    emit("ecall");

    emit("lw ra, 60(sp)");
    emit("lw s0, 56(sp)");
    emit("addi sp, sp, 64");
    emit("ret");
    output << endl;
}

void RISCVCodeGenerator::generate_print_char()
{
    emit_comment("=== print_char: Print character in a0 ===");
    emit_label("print_char_0");

    // Character is in a0, write it using write syscall
    emit("addi sp, sp, -16");
    emit("sw ra, 12(sp)");
    emit("sb a0, 8(sp)      # store char on stack");
    emit("li a0, 1          # stdout");
    emit("addi a1, sp, 8    # buffer address");
    emit("li a2, 1          # length = 1");
    emit("li a7, 64         # syscall number for write");
    emit("ecall");
    emit("lw ra, 12(sp)");
    emit("addi sp, sp, 16");
    emit("ret");
    output << endl;
}

void RISCVCodeGenerator::generate_print_double()
{
    emit_comment("=== print_double: Print double with 6 decimal places ===");
    emit_label("print_double_0");
    
    emit("addi sp, sp, -80");
    emit("sd ra, 72(sp)");
    emit("sd s0, 64(sp)");
    emit("fsd fs0, 56(sp)");
    emit("fsd fs1, 48(sp)");
    emit("sd s1, 40(sp)");
    emit("sd s2, 32(sp)");
    
    emit("fmv.d fs0, fa0      # save double in fs0");
    
    // Check if negative
    emit("fmv.x.d t0, fs0     # move double to int register");
    emit("srli t0, t0, 63     # get sign bit");
    emit("beqz t0, .Lpositive");
    
    // Print minus sign
    emit("li a0, 45           # ASCII '-'");
    emit("addi sp, sp, -16");
    emit("sb a0, 8(sp)");
    emit("li a0, 1            # stdout");
    emit("addi a1, sp, 8");
    emit("li a2, 1");
    emit("li a7, 64           # write syscall");
    emit("ecall");
    emit("addi sp, sp, 16");
    
    // Make positive
    emit("fneg.d fs0, fs0");
    
    emit_label(".Lpositive");
    
    // Extract integer part
    emit("fcvt.w.d s1, fs0, rtz  # integer part");
    
    // Print integer part
    emit("mv a0, s1");
    emit("jal ra, print_int_0");
    
    // Print decimal point
    emit("li a0, 46           # ASCII '.'");
    emit("addi sp, sp, -16");
    emit("sb a0, 8(sp)");
    emit("li a0, 1            # stdout");
    emit("addi a1, sp, 8");
    emit("li a2, 1");
    emit("li a7, 64           # write syscall");
    emit("ecall");
    emit("addi sp, sp, 16");
    
    // Get fractional part: frac = double - int
    emit("fcvt.d.w fs1, s1     # convert int back to double");
    emit("fsub.d fs1, fs0, fs1 # fractional part");
    
    // Multiply by 1000000 to get 6 decimal places
    emit("li t0, 1000000");
    emit("fcvt.d.w ft0, t0");
    emit("fmul.d fs1, fs1, ft0");
    emit("fcvt.w.d s2, fs1, rtz");
    
    // Ensure it's positive (handle rounding)
    emit("bge s2, zero, .Lprint_frac");
    emit("neg s2, s2");
    
    emit_label(".Lprint_frac");
    
    // Print fractional part with leading zeros (6 digits)
    emit("mv s1, s2           # save fractional value");
    emit("li s2, 100000       # place value");
    emit("li t1, 6            # digit count");
    
    emit_label(".Lprint_digit");
    emit("divu t2, s1, s2     # get digit");
    emit("addi t2, t2, 48     # convert to ASCII");
    emit("sb t2, 8(sp)");
    emit("li a0, 1            # stdout");
    emit("addi a1, sp, 8");
    emit("li a2, 1");
    emit("li a7, 64");
    emit("ecall");
    
    emit("remu s1, s1, s2     # remainder");
    emit("li t2, 10");
    emit("divu s2, s2, t2     # next place value");
    emit("addi t1, t1, -1");
    emit("bnez t1, .Lprint_digit");
    
    // Restore and return
    emit("ld ra, 72(sp)");
    emit("ld s0, 64(sp)");
    emit("fld fs0, 56(sp)");
    emit("fld fs1, 48(sp)");
    emit("ld s1, 40(sp)");
    emit("ld s2, 32(sp)");
    emit("addi sp, sp, 80");
    emit("ret");
    output << endl;
}

void RISCVCodeGenerator::generate_print_string()
{
    emit_comment("=== print_string: Print null-terminated string ===");
    emit_label("print_string_0");

    // String address is in a0, find length then write
    emit("addi sp, sp, -16");
    emit("sw ra, 12(sp)");
    emit("sw s0, 8(sp)");
    emit("mv s0, a0         # save string address");

    // Find string length
    emit("mv t0, a0         # current position");
    emit_label(".Lstrlen_loop");
    emit("lbu t1, 0(t0)");
    emit("beqz t1, .Lstrlen_done");
    emit("addi t0, t0, 1");
    emit("j .Lstrlen_loop");
    emit_label(".Lstrlen_done");
    emit("sub a2, t0, a0    # length = end - start");

    // Write syscall
    emit("li a0, 1          # stdout");
    emit("mv a1, s0         # buffer");
    emit("li a7, 64         # syscall number for write");
    emit("ecall");

    emit("lw ra, 12(sp)");
    emit("lw s0, 8(sp)");
    emit("addi sp, sp, 16");
    emit("ret");
    output << endl;
}

void RISCVCodeGenerator::generate_print_newline()
{
    emit_comment("=== print_newline: Print newline character ===");
    emit_label("print_newline_0");

    emit("addi sp, sp, -16");
    emit("sw ra, 12(sp)");
    emit("li t0, 10         # newline character");
    emit("sb t0, 8(sp)      # store on stack");
    emit("li a0, 1          # stdout");
    emit("addi a1, sp, 8    # buffer address");
    emit("li a2, 1          # length = 1");
    emit("li a7, 64         # syscall number for write");
    emit("ecall");
    emit("lw ra, 12(sp)");
    emit("addi sp, sp, 16");
    emit("ret");
    output << endl;
}

void RISCVCodeGenerator::generate_scan_int()
{
    emit_comment("=== scan_int: Read integer from input ===");
    emit_label("scan_int_0");

    // Syscall 5: read_int (result in a0)
    emit("li a7, 5          # syscall number for read_int");
    emit("ecall             # make syscall");
    emit("# result is in a0");
    emit("ret               # return");
    output << endl;
}

void RISCVCodeGenerator::generate_scan_double()
{
    emit_comment("=== scan_double: Read double from input ===");
    emit_label("scan_double_0");

    // Syscall 7: read_double (result in fa0)
    emit_comment("Note: Simplified - reads as int for now");
    emit("li a7, 5          # syscall number for read_int");
    emit("ecall             # make syscall");
    emit("fcvt.d.w fa0, a0  # convert int to double (if using FP)");
    emit("ret               # return");
    output << endl;
}

void RISCVCodeGenerator::generate_scan_char()
{
    emit_comment("=== scan_char: Read character from input ===");
    emit_label("scan_char_0");

    // Syscall 12: read_char (result in a0)
    emit("li a7, 12         # syscall number for read_char");
    emit("ecall             # make syscall");
    emit("# result is in a0");
    emit("ret               # return");
    output << endl;
}

void RISCVCodeGenerator::generate_builtin_functions()
{
    output << "# ============================================" << endl;
    output << "# Built-in I/O Functions" << endl;
    output << "# ============================================" << endl;
    output << endl;

    generate_print_int();
    generate_print_char();
    generate_print_double();
    generate_print_string();
    generate_print_newline();
    generate_scan_int();
    generate_scan_double();
    generate_scan_char();
}

// ============================================================================
// Main Generation Function
// ============================================================================

void RISCVCodeGenerator::generate(const vector<TACInstruction *> &tac_code)
{
    // Generate assembly file header
    output << "# ============================================" << endl;
    output << "# RISC-V Assembly Generated from TAC" << endl;
    output << "# ============================================" << endl;
    output << endl;

    // First pass: collect string literals and double constants
    for (const auto *instr : tac_code)
    {
        if (instr->op == TAC_PARAM && instr->arg1.type == TACOperand::OPERAND_STRING)
        {
            string label = ".str" + to_string(string_counter++);
            string_literals.push_back({label, instr->arg1.name});
        }
        // Also collect string literals from assignments (e.g., char *str = "Hello")
        if (instr->op == TAC_ASSIGN && instr->arg1.type == TACOperand::OPERAND_STRING)
        {
            string label = ".str" + to_string(string_counter++);
            string_literals.push_back({label, instr->arg1.name});
        }
        // Also collect string literals from dereference stores (e.g., *ptr = "Hello")
        if (instr->op == TAC_DEREF_STORE && instr->arg1.type == TACOperand::OPERAND_STRING)
        {
            string label = ".str" + to_string(string_counter++);
            string_literals.push_back({label, instr->arg1.name});
        }
        // Collect double constants from all operations
        auto collect_double_const = [this](const TACOperand &operand) {
            if (operand.type == TACOperand::OPERAND_CONSTANT)
            {
                if (operand.name.find('.') != string::npos || 
                    operand.name.find('e') != string::npos ||
                    operand.name.find('E') != string::npos)
                {
                    // Check if already collected
                    bool found = false;
                    for (const auto &dbl : double_constants)
                    {
                        if (dbl.second == operand.name)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        string label = ".Ldbl" + to_string(double_counter++);
                        double_constants.push_back({label, operand.name});
                    }
                }
            }
        };
        
        collect_double_const(instr->arg1);
        collect_double_const(instr->arg2);
        collect_double_const(instr->result);
    }

    // Second pass: pre-allocate all variables to calculate stack size
    for (const auto *instr : tac_code)
    {
        // Allocate space for result if it's a variable/temp
        if (!instr->result.is_empty() &&
            (instr->result.type == TACOperand::OPERAND_IDENTIFIER ||
             instr->result.type == TACOperand::OPERAND_TEMP))
        {
            get_var_offset(instr->result.name);
        }
        // Allocate space for arg1 if needed
        if (!instr->arg1.is_empty() &&
            (instr->arg1.type == TACOperand::OPERAND_IDENTIFIER ||
             instr->arg1.type == TACOperand::OPERAND_TEMP))
        {
            get_var_offset(instr->arg1.name);
        }
        // Allocate space for arg2 if needed
        if (!instr->arg2.is_empty() &&
            (instr->arg2.type == TACOperand::OPERAND_IDENTIFIER ||
             instr->arg2.type == TACOperand::OPERAND_TEMP))
        {
            get_var_offset(instr->arg2.name);
        }
    }

    // Generate data section with collected strings
    generate_data_section();

    // Generate text section
    output << ".text" << endl;
    output << ".globl _start" << endl;
    output << endl;

    // Generate _start function (entry point)
    output << "# ============================================" << endl;
    output << "# Entry Point" << endl;
    output << "# ============================================" << endl;
    emit_label("_start");
    emit(".option push");
    emit(".option norelax");
    emit("la gp, __global_pointer$   # initialize global pointer");
    emit(".option pop");
    emit("call main_0       # call main function");
    emit("# Exit program");
    emit("# Syscall 10 for RARS, 93 for Linux/pk");
    emit("li a7, 93         # syscall 93: exit (Linux/pk)");
    emit("# a0 already contains return value from main");
    emit("ecall             # make syscall");
    output << endl;

    // Generate built-in I/O functions
    generate_builtin_functions();

    // Generate main program
    output << "# ============================================" << endl;
    output << "# User Code" << endl;
    output << "# ============================================" << endl;
    output << endl;

    // Reset string counter for second pass
    int str_index = 0;

    // Process TAC instructions
    for (size_t i = 0; i < tac_code.size(); i++)
    {
        const auto *instr = tac_code[i];

        // Emit a label for this instruction number (for goto targets)
        // Only emit if it's not already a label instruction
        if (instr->op != TAC_LABEL)
        {
            output << ".L" << i << ":" << endl;
        }

        // Add original TAC as comment for debugging
        output << "    # TAC: " << instr->to_string() << endl;

        // Handle string literal indexing for params
        if (instr->op == TAC_PARAM && instr->arg1.type == TACOperand::OPERAND_STRING)
        {
            // Use the pre-collected string label
            load_operand(instr->arg1, REG_A0, str_index);
            str_index++;
            output << endl;
            continue;
        }

        switch (instr->op)
        {
        case TAC_LABEL:
            generate_label(instr);
            break;
        case TAC_ASSIGN:
            generate_assign(instr);
            break;
        case TAC_UMINUS:
        case TAC_UPLUS:
        case TAC_LOGICAL_NOT:
        case TAC_BITWISE_NOT:
            generate_unary(instr);
            break;
        case TAC_ADD:
        case TAC_SUB:
        case TAC_MUL:
        case TAC_DIV:
        case TAC_MOD:
        case TAC_BITWISE_AND:
        case TAC_BITWISE_OR:
        case TAC_BITWISE_XOR:
        case TAC_LEFT_SHIFT:
        case TAC_RIGHT_SHIFT:
            generate_arithmetic(instr);
            break;
        case TAC_LT:
        case TAC_GT:
        case TAC_LE:
        case TAC_GE:
        case TAC_EQ:
        case TAC_NE:
            generate_comparison(instr);
            break;
        case TAC_ADDR_OF:
            generate_addr_of(instr);
            break;
        case TAC_DEREF:
            generate_deref(instr);
            break;
        case TAC_DEREF_STORE:
            generate_deref_store(instr);
            break;
        case TAC_PARAM:
            generate_param(instr);
            break;
        case TAC_CALL:
            generate_call(instr);
            break;
        case TAC_RETURN:
            generate_return(instr);
            break;
        case TAC_GOTO:
            generate_goto(instr);
            break;
        case TAC_IF_GOTO:
        case TAC_IF_FALSE_GOTO:
            generate_conditional(instr);
            break;
        default:
            emit_comment("TODO: Implement " + instr->to_string());
            break;
        }

        output << endl;
    }

    // Generate exit syscall
    output << "# Program exit" << endl;
    emit_label("_exit");
    emit("li a7, 10         # syscall number for exit");
    emit("ecall             # make syscall");
}

// ============================================================================
// Data Section Generation
// ============================================================================

void RISCVCodeGenerator::generate_data_section()
{
    output << ".data" << endl;

    // Generate string literals
    for (const auto &str_lit : string_literals)
    {
        // Remove surrounding quotes from the string if present
        string content = str_lit.second;
        if (content.length() >= 2 && content.front() == '"' && content.back() == '"')
        {
            content = content.substr(1, content.length() - 2);
        }
        output << str_lit.first << ": .string \"" << content << "\"" << endl;
    }

    if (!string_literals.empty())
    {
        output << endl;
    }

    // Generate double constants
    output << ".align 3  # Align to 8-byte boundary for doubles" << endl;
    for (const auto &dbl_const : double_constants)
    {
        output << dbl_const.first << ": .double " << dbl_const.second << endl;
    }

    if (!double_constants.empty())
    {
        output << endl;
    }
}

// ============================================================================
// Stack Management (Placeholder implementations for now)
// ============================================================================

void RISCVCodeGenerator::emit_load_with_offset(const string &load_instr, const string &dest_reg, int offset, const string &base_reg)
{
    // Check if offset fits in 12-bit signed immediate [-2048, 2047]
    if (offset >= -2048 && offset <= 2047)
    {
        emit(load_instr + " " + dest_reg + ", " + to_string(offset) + "(" + base_reg + ")");
    }
    else
    {
        // Large offset: load into temp register and add
        emit("li t6, " + to_string(offset));
        emit("add t6, " + base_reg + ", t6");
        emit(load_instr + " " + dest_reg + ", 0(t6)");
    }
}

void RISCVCodeGenerator::emit_store_with_offset(const string &store_instr, const string &src_reg, int offset, const string &base_reg)
{
    // Check if offset fits in 12-bit signed immediate [-2048, 2047]
    if (offset >= -2048 && offset <= 2047)
    {
        emit(store_instr + " " + src_reg + ", " + to_string(offset) + "(" + base_reg + ")");
    }
    else
    {
        // Large offset: load into temp register and add
        emit("li t6, " + to_string(offset));
        emit("add t6, " + base_reg + ", t6");
        emit(store_instr + " " + src_reg + ", 0(t6)");
    }
}

int RISCVCodeGenerator::allocate_stack_var(const string &var_name, int size)
{
    // Align to 8 bytes for doubles
    if (size == 8 || size % 8 == 0)
    {
        stack_offset = (stack_offset + 7) & ~7; // Align to 8-byte boundary
    }
    
    // Allocate space at positive offsets from sp (0, 4, 8, ...)
    // Reserve space for ra and s0 at the top
    int offset = stack_offset;
    var_offsets[var_name] = offset;
    stack_offset += size;
    if (stack_offset > max_stack_size)
    {
        max_stack_size = stack_offset;
    }
    return offset;
}

int RISCVCodeGenerator::get_var_offset(const string &var_name)
{
    if (var_offsets.find(var_name) == var_offsets.end())
    {
        // Check if this is an array and calculate proper size
        int size = 8; // Default: 8 bytes for RV64 to support pointers

        // Parse scope ID from the mangled name (e.g., "arr_1" -> scope 1)
        size_t underscore_pos = var_name.rfind('_');
        if (underscore_pos != string::npos)
        {
            string scope_str = var_name.substr(underscore_pos + 1);
            // Check if it's a valid scope ID (all digits)
            bool is_scope_id = !scope_str.empty();
            for (char c : scope_str)
            {
                if (!isdigit(c))
                {
                    is_scope_id = false;
                    break;
                }
            }

            if (is_scope_id)
            {
                string base_name = var_name.substr(0, underscore_pos);
                int scope_id = stoi(scope_str);

                // Look up the scope table
                auto it = scope_map.find(scope_id);
                if (it != scope_map.end())
                {
                    SymbolTable *scope_table = it->second;
                    Symbol *sym = scope_table->lookup(base_name);
                    if (sym && sym->type.is_array)
                    {
                        // Calculate array size: total_elements * element_size
                        int total_elements = 1;
                        for (int dim_size : sym->type.array_sizes)
                        {
                            total_elements *= dim_size;
                        }

                        // Determine element size based on base type
                        int elem_size = 4; // Default to int size
                        switch (sym->type.base_type)
                        {
                        case TYPE_CHAR:
                        case TYPE_BOOL:
                            elem_size = 1;
                            break;
                        case TYPE_INT:
                            elem_size = 4;
                            break;
                        case TYPE_DOUBLE:
                            elem_size = 8;
                            break;
                        case TYPE_VOID:
                        case TYPE_ERROR:
                        default:
                            elem_size = 8; // Pointers or unknown types
                            break;
                        }

                        size = total_elements * elem_size;
                    }
                }
            }
        }

        // Allocate space
        return allocate_stack_var(var_name, size);
    }
    return var_offsets[var_name];
}

// ============================================================================
// Type Checking Helpers
// ============================================================================

Type RISCVCodeGenerator::get_operand_type(const TACOperand &operand)
{
    // For constants, determine type from value
    if (operand.type == TACOperand::OPERAND_CONSTANT)
    {
        // Check if it's a float/double literal
        if (operand.name.find('.') != string::npos || 
            operand.name.find('e') != string::npos ||
            operand.name.find('E') != string::npos)
        {
            return Type(TYPE_DOUBLE);
        }
        return Type(TYPE_INT);
    }

    // For identifiers and temps, look up in symbol table
    if (operand.type == TACOperand::OPERAND_IDENTIFIER || 
        operand.type == TACOperand::OPERAND_TEMP)
    {
        // Check if it's a temporary (starts with $t)
        if (operand.name.length() >= 2 && operand.name[0] == '$' && operand.name[1] == 't')
        {
            // Temporaries are not mangled - search all scopes
            for (auto &scope_entry : scope_map)
            {
                Symbol *sym = scope_entry.second->lookup(operand.name);
                if (sym)
                {
                    return sym->type;
                }
            }
            // Also try current symtab
            if (symtab != nullptr)
            {
                Symbol *sym = symtab->lookup(operand.name);
                if (sym)
                {
                    return sym->type;
                }
            }
        }
        else
        {
            // Parse scope ID from mangled name for regular variables
            size_t underscore_pos = operand.name.rfind('_');
            if (underscore_pos != string::npos)
            {
                string scope_str = operand.name.substr(underscore_pos + 1);
                bool is_scope_id = !scope_str.empty();
                for (char c : scope_str)
                {
                    if (!isdigit(c))
                    {
                        is_scope_id = false;
                        break;
                    }
                }

                if (is_scope_id)
                {
                    string base_name = operand.name.substr(0, underscore_pos);
                    int scope_id = stoi(scope_str);

                    auto it = scope_map.find(scope_id);
                    if (it != scope_map.end() && it->second != nullptr)
                    {
                        SymbolTable *scope_table = it->second;
                        Symbol *sym = scope_table->lookup(base_name);
                        if (sym)
                        {
                            return sym->type;
                        }
                    }
                }
            }

            // Try looking in current symbol table
            if (symtab != nullptr)
            {
                Symbol *sym = symtab->lookup(operand.name);
                if (sym)
                {
                    return sym->type;
                }
            }
        }
    }

    // Default to int
    return Type(TYPE_INT);
}

bool RISCVCodeGenerator::is_double_operand(const TACOperand &operand)
{
    // Safely check operand type
    if (operand.is_empty())
    {
        return false;
    }
    
    Type t = get_operand_type(operand);
    // Arrays and pointers are not double operands (they're addresses)
    return t.base_type == TYPE_DOUBLE && t.pointer_level == 0 && !t.is_array;
}

// ============================================================================
// Load/Store Operations
// ============================================================================

void RISCVCodeGenerator::load_operand(const TACOperand &operand, RISCVRegister dest_reg, int str_idx)
{
    string dest = reg_name(dest_reg);

    switch (operand.type)
    {
    case TACOperand::OPERAND_CONSTANT:
    {
        // Check if destination is FP register and value is a double
        bool is_fp_reg = (dest_reg >= REG_FT0 && dest_reg <= REG_FA7);
        bool is_double_const = (operand.name.find('.') != string::npos || 
                                operand.name.find('e') != string::npos ||
                                operand.name.find('E') != string::npos);
        
        if (is_fp_reg && is_double_const)
        {
            // Load double constant from data section - find the label
            string label;
            for (const auto &dbl : double_constants)
            {
                if (dbl.second == operand.name)
                {
                    label = dbl.first;
                    break;
                }
            }
            emit("# Loading double constant " + operand.name);
            emit("la t6, " + label);
            emit("fld " + dest + ", 0(t6)");
        }
        else
        {
            // Load integer immediate
            emit("li " + dest + ", " + operand.name);
        }
        break;
    }

    case TACOperand::OPERAND_IDENTIFIER:
    case TACOperand::OPERAND_TEMP:
    {
        int offset = get_var_offset(operand.name);

        // Check if this is an array - if so, we need its address, not its value
        bool is_array = false;
        if (operand.type == TACOperand::OPERAND_IDENTIFIER)
        {
            // Parse scope ID from the mangled name (e.g., "arr_1" -> scope 1)
            size_t underscore_pos = operand.name.rfind('_');
            if (underscore_pos != string::npos)
            {
                string scope_str = operand.name.substr(underscore_pos + 1);
                // Check if it's a valid scope ID (all digits)
                bool is_scope_id = !scope_str.empty();
                for (char c : scope_str)
                {
                    if (!isdigit(c))
                    {
                        is_scope_id = false;
                        break;
                    }
                }

                if (is_scope_id)
                {
                    string base_name = operand.name.substr(0, underscore_pos);
                    int scope_id = stoi(scope_str);

                    // Look up the scope table
                    auto it = scope_map.find(scope_id);
                    if (it != scope_map.end())
                    {
                        SymbolTable *scope_table = it->second;
                        Symbol *sym = scope_table->lookup(base_name);
                        if (sym && sym->type.is_array)
                        {
                            is_array = true;
                        }
                    }
                }
            }
        }

        if (is_array)
        {
            // For arrays, generate the address (sp + offset)
            // Always use an integer register for address calculation
            bool is_fp_reg = (dest_reg >= REG_FT0 && dest_reg <= REG_FA7);
            if (is_fp_reg)
            {
                // Can't use addi on FP register, use t5 temporarily
                if (offset >= -2048 && offset <= 2047)
                {
                    emit("addi t5, sp, " + to_string(offset));
                }
                else
                {
                    emit("li t5, " + to_string(offset));
                    emit("add t5, sp, t5");
                }
                // This shouldn't happen - arrays should use integer registers
                emit_comment("WARNING: Loading array address into FP register");
                emit("mv " + dest + ", t5  # Invalid: cannot move int to FP reg");
            }
            else
            {
                // Normal case: integer register
                if (offset >= -2048 && offset <= 2047)
                {
                    emit("addi " + dest + ", sp, " + to_string(offset));
                }
                else
                {
                    emit("li t6, " + to_string(offset));
                    emit("add " + dest + ", sp, t6");
                }
            }
        }
        else
        {
            // Check if this is a double type
            Type var_type = get_operand_type(operand);
            if (var_type.base_type == TYPE_DOUBLE && var_type.pointer_level == 0)
            {
                // For doubles, use fld (floating-point load double)
                emit_load_with_offset("fld", dest, offset, "sp");
            }
            else if (var_type.pointer_level > 0)
            {
                // For pointers, use ld (64-bit)
                emit_load_with_offset("ld", dest, offset, "sp");
            }
            else
            {
                // For other types (int, char, etc), use appropriate load
                string load_instr, store_instr;
                get_load_store_instructions(var_type.base_type, load_instr, store_instr);
                emit_load_with_offset(load_instr, dest, offset, "sp");
            }
        }
        break;
    }

    case TACOperand::OPERAND_STRING:
    {
        // Load address of string literal
        string label;
        
        // First, check if this string was already added in the first pass
        int found_idx = -1;
        for (size_t i = 0; i < string_literals.size(); i++)
        {
            if (string_literals[i].second == operand.name)
            {
                found_idx = i;
                break;
            }
        }
        
        if (found_idx >= 0)
        {
            label = string_literals[found_idx].first;
        }
        else
        {
            // String not found in first pass, add it now
            label = ".str" + to_string(string_counter++);
            string_literals.push_back({label, operand.name});
        }
        emit("la " + dest + ", " + label);
        break;
    }

    default:
        emit_comment("Unknown operand type");
        break;
    }
}

void RISCVCodeGenerator::store_to_operand(RISCVRegister src_reg, const TACOperand &operand)
{
    string src = reg_name(src_reg);

    if (operand.type == TACOperand::OPERAND_IDENTIFIER ||
        operand.type == TACOperand::OPERAND_TEMP)
    {
        int offset = get_var_offset(operand.name);
        
        // Check if source is FP register
        bool is_fp_reg = (src_reg >= REG_FT0 && src_reg <= REG_FA7);
        
        if (is_fp_reg)
        {
            // Use fsd for floating-point stores
            emit_store_with_offset("fsd", src, offset, "sp");
        }
        else
        {
            // Check operand type for proper store instruction
            Type var_type = get_operand_type(operand);
            if (var_type.pointer_level > 0)
            {
                emit_store_with_offset("sd", src, offset, "sp");
            }
            else
            {
                string load_instr, store_instr;
                get_load_store_instructions(var_type.base_type, load_instr, store_instr);
                emit_store_with_offset(store_instr, src, offset, "sp");
            }
        }
    }
}

// ============================================================================
// TAC Instruction Generators (Implementations in next part)
// ============================================================================

void RISCVCodeGenerator::generate_label(const TACInstruction *instr)
{
    // Remove the colon from label name if present
    string label_name = instr->result.name;
    if (!label_name.empty() && label_name.back() == ':')
    {
        label_name.pop_back();
    }
    emit_label(label_name);

    // Don't add prologue for built-in functions (they already have their own)
    if (label_name.find("print_") != string::npos ||
        label_name.find("scan_") != string::npos)
    {
        return;
    }

    // Add function prologue for user function labels (main_0, func_0, etc.)
    if (label_name.find("_0") != string::npos ||
        label_name.find("_1") != string::npos ||
        label_name.find("_2") != string::npos)
    {
        // Calculate total stack size: variables + saved registers (16 bytes for RV64) + padding
        // Need space for ra (8 bytes) and s0 (8 bytes) at the top
        current_frame_size = max_stack_size + 16; // Add space for ra, s0

        // Align to 16 bytes for RISC-V calling convention
        current_frame_size = (current_frame_size + 15) & ~15;

        int ra_offset = current_frame_size - 8;
        int s0_offset = current_frame_size - 16;

        // Handle large frame sizes (exceeding 12-bit signed immediate range)
        if (current_frame_size >= -2048 && current_frame_size <= 2047)
        {
            emit("addi sp, sp, -" + to_string(current_frame_size) + "   # allocate stack frame");
        }
        else
        {
            emit("li t6, -" + to_string(current_frame_size));
            emit("add sp, sp, t6       # allocate large stack frame");
        }
        
        emit_store_with_offset("sd", "ra", ra_offset, "sp");
        emit("# save return address");
        emit_store_with_offset("sd", "s0", s0_offset, "sp");
        emit("# save frame pointer");
        
        if (current_frame_size >= -2048 && current_frame_size <= 2047)
        {
            emit("addi s0, sp, " + to_string(current_frame_size) + "     # set frame pointer");
        }
        else
        {
            emit("li t6, " + to_string(current_frame_size));
            emit("add s0, sp, t6       # set frame pointer");
        }
    }
}

void RISCVCodeGenerator::generate_assign(const TACInstruction *instr)
{
    // Check if operands are doubles
    bool arg_is_double = is_double_operand(instr->arg1);
    bool result_is_double = is_double_operand(instr->result);
    
    if (arg_is_double && result_is_double)
    {
        // Double to double: use FP registers
        load_operand(instr->arg1, REG_FT0);
        store_to_operand(REG_FT0, instr->result);
    }
    else if (arg_is_double && !result_is_double)
    {
        // Double to int: convert
        load_operand(instr->arg1, REG_FT0);
        emit("fcvt.w.d t0, ft0, rtz");  // Convert double to int (round to zero)
        store_to_operand(REG_T0, instr->result);
    }
    else if (!arg_is_double && result_is_double)
    {
        // Int to double: convert
        load_operand(instr->arg1, REG_T0);
        emit("fcvt.d.w ft0, t0");  // Convert int to double
        store_to_operand(REG_FT0, instr->result);
    }
    else
    {
        // Int to int: normal assign
        load_operand(instr->arg1, REG_T0);
        store_to_operand(REG_T0, instr->result);
    }
}

void RISCVCodeGenerator::generate_unary(const TACInstruction *instr)
{
    // Load operand
    load_operand(instr->arg1, REG_T0);

    // Perform unary operation
    string dest = reg_name(REG_T0);

    switch (instr->op)
    {
    case TAC_UMINUS:
        // Negate: result = 0 - operand
        emit("neg " + dest + ", " + dest);
        break;
    case TAC_UPLUS:
        // Unary plus: no-op, value already in register
        break;
    case TAC_LOGICAL_NOT:
        // Logical NOT: result = (operand == 0) ? 1 : 0
        emit("seqz " + dest + ", " + dest);
        break;
    case TAC_BITWISE_NOT:
        // Bitwise NOT
        emit("not " + dest + ", " + dest);
        break;
    default:
        break;
    }

    // Store result
    store_to_operand(REG_T0, instr->result);
}

void RISCVCodeGenerator::generate_arithmetic(const TACInstruction *instr)
{
    // Check if operands are doubles
    bool arg1_is_double = is_double_operand(instr->arg1);
    bool arg2_is_double = is_double_operand(instr->arg2);
    
    if (arg1_is_double || arg2_is_double)
    {
        // At least one operand is double, use FP arithmetic
        // Load operands to FP registers
        if (arg1_is_double)
        {
            load_operand(instr->arg1, REG_FT0);
        }
        else
        {
            load_operand(instr->arg1, REG_T0);
            emit("fcvt.d.w ft0, t0");  // Convert int to double
        }
        
        if (arg2_is_double)
        {
            load_operand(instr->arg2, REG_FT1);
        }
        else
        {
            load_operand(instr->arg2, REG_T1);
            emit("fcvt.d.w ft1, t1");  // Convert int to double
        }
        
        // Perform FP operation
        string dest = reg_name(REG_FT0);
        string src1 = reg_name(REG_FT0);
        string src2 = reg_name(REG_FT1);
        
        switch (instr->op)
        {
        case TAC_ADD:
            emit("fadd.d " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_SUB:
            emit("fsub.d " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_MUL:
            emit("fmul.d " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_DIV:
            emit("fdiv.d " + dest + ", " + src1 + ", " + src2);
            break;
        default:
            emit_comment("Unsupported FP operation");
            break;
        }
        
        // Store result
        store_to_operand(REG_FT0, instr->result);
    }
    else
    {
        // Integer arithmetic
        load_operand(instr->arg1, REG_T0);
        load_operand(instr->arg2, REG_T1);

        string dest = reg_name(REG_T0);
        string src1 = reg_name(REG_T0);
        string src2 = reg_name(REG_T1);

        switch (instr->op)
        {
        case TAC_ADD:
            emit("add " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_SUB:
            emit("sub " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_MUL:
            emit("mul " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_DIV:
            emit("div " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_MOD:
            emit("rem " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_BITWISE_AND:
            emit("and " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_BITWISE_OR:
            emit("or " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_BITWISE_XOR:
            emit("xor " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_LEFT_SHIFT:
            emit("sll " + dest + ", " + src1 + ", " + src2);
            break;
        case TAC_RIGHT_SHIFT:
            emit("sra " + dest + ", " + src1 + ", " + src2);
            break;
        default:
            break;
        }

        // Store result
        store_to_operand(REG_T0, instr->result);
    }
}

void RISCVCodeGenerator::generate_comparison(const TACInstruction *instr)
{
    // Load operands
    load_operand(instr->arg1, REG_T0);
    load_operand(instr->arg2, REG_T1);

    // Perform comparison - result is 0 or 1
    string dest = reg_name(REG_T0);
    string src1 = reg_name(REG_T0);
    string src2 = reg_name(REG_T1);

    switch (instr->op)
    {
    case TAC_LT:
        // Set if less than (signed)
        emit("slt " + dest + ", " + src1 + ", " + src2);
        break;
    case TAC_GT:
        // Set if greater than: swap operands for slt
        emit("slt " + dest + ", " + src2 + ", " + src1);
        break;
    case TAC_LE:
        // Set if less than or equal: !(a > b) = !(b < a)
        emit("slt " + dest + ", " + src2 + ", " + src1);
        emit("xori " + dest + ", " + dest + ", 1");
        break;
    case TAC_GE:
        // Set if greater than or equal: !(a < b)
        emit("slt " + dest + ", " + src1 + ", " + src2);
        emit("xori " + dest + ", " + dest + ", 1");
        break;
    case TAC_EQ:
        // Set if equal: (a - b == 0)
        emit("sub " + dest + ", " + src1 + ", " + src2);
        emit("seqz " + dest + ", " + dest);
        break;
    case TAC_NE:
        // Set if not equal: (a - b != 0)
        emit("sub " + dest + ", " + src1 + ", " + src2);
        emit("snez " + dest + ", " + dest);
        break;
    default:
        break;
    }

    // Store result (0 or 1)
    store_to_operand(REG_T0, instr->result);
}

void RISCVCodeGenerator::generate_addr_of(const TACInstruction *instr)
{
    // Get address of a variable: result = &arg1
    // We need to compute the address: sp + offset
    int offset = get_var_offset(instr->arg1.name);

    string dest = reg_name(REG_T0);
    emit("addi " + dest + ", sp, " + to_string(offset));

    // Store the address in result
    store_to_operand(REG_T0, instr->result);
}

void RISCVCodeGenerator::get_load_store_instructions(PrimitiveType type, string &load_instr, string &store_instr)
{
    switch (type)
    {
    case TYPE_CHAR:
        load_instr = "lb";  // load byte (sign-extended)
        store_instr = "sb"; // store byte
        break;
    case TYPE_BOOL:
        load_instr = "lbu"; // load byte unsigned
        store_instr = "sb"; // store byte
        break;
    case TYPE_INT:
        load_instr = "lw";  // load word (4 bytes, sign-extended to 64-bit)
        store_instr = "sw"; // store word
        break;
    case TYPE_DOUBLE:
        // Double is 8 bytes, use FP load/store
        load_instr = "fld";  // load double (8 bytes to FP register)
        store_instr = "fsd"; // store double (8 bytes from FP register)
        break;
    case TYPE_VOID:
    case TYPE_ERROR:
    default:
        // Default to pointer size (64-bit)
        load_instr = "ld";  // load doubleword
        store_instr = "sd"; // store doubleword
        break;
    }
}

void RISCVCodeGenerator::generate_deref(const TACInstruction *instr)
{
    // Load value from pointer: result = *arg1
    // arg1 contains an address, load the value at that address
    
    // Determine the type being dereferenced
    string load_instr = "lw"; // Default to int
    string store_instr = "sw";
    bool is_double_deref = false;

    // Try to determine the element type from the pointer/array
    if (instr->arg1.type == TACOperand::OPERAND_IDENTIFIER ||
        instr->arg1.type == TACOperand::OPERAND_TEMP)
    {
        Symbol *sym = nullptr;

        // Check if it's a temporary (starts with $t)
        if (instr->arg1.name.length() >= 2 &&
            instr->arg1.name[0] == '$' && instr->arg1.name[1] == 't')
        {
            // Temporaries are not mangled, search all scopes
            for (auto &scope_entry : scope_map)
            {
                sym = scope_entry.second->lookup(instr->arg1.name);
                if (sym)
                    break;
            }
        }
        else
        {
            // Parse scope ID from the mangled name for regular variables
            size_t underscore_pos = instr->arg1.name.rfind('_');
            if (underscore_pos != string::npos)
            {
                string scope_str = instr->arg1.name.substr(underscore_pos + 1);
                bool is_scope_id = !scope_str.empty();
                for (char c : scope_str)
                {
                    if (!isdigit(c))
                    {
                        is_scope_id = false;
                        break;
                    }
                }

                if (is_scope_id)
                {
                    string base_name = instr->arg1.name.substr(0, underscore_pos);
                    int scope_id = stoi(scope_str);

                    auto it = scope_map.find(scope_id);
                    if (it != scope_map.end())
                    {
                        SymbolTable *scope_table = it->second;
                        sym = scope_table->lookup(base_name);
                    }
                }
            }
        }

        if (sym)
        {
            // If pointer_level > 1, dereferencing gives another pointer (8 bytes)
            // If pointer_level == 1, dereferencing gives the base type
            if (sym->type.pointer_level > 1)
            {
                load_instr = "ld"; // Pointers are 8 bytes
                store_instr = "sd";
            }
            else if (sym->type.pointer_level == 1)
            {
                // Dereferencing a single-level pointer gives the base type
                PrimitiveType elem_type = sym->type.base_type;
                get_load_store_instructions(elem_type, load_instr, store_instr);
                // Check if it's a double
                if (elem_type == TYPE_DOUBLE)
                {
                    is_double_deref = true;
                    load_instr = "fld";
                    store_instr = "fsd";
                }
            }
        }
    }

    // Load the address into appropriate register
    load_operand(instr->arg1, REG_T0);
    
    // Load from the address using appropriate register
    if (is_double_deref)
    {
        // For doubles, use FP register
        emit(load_instr + " ft0, 0(t0)");
        store_to_operand(REG_FT0, instr->result);
    }
    else
    {
        // For other types, use integer register
        emit(load_instr + " t0, 0(t0)");
        store_to_operand(REG_T0, instr->result);
    }
}

void RISCVCodeGenerator::generate_deref_store(const TACInstruction *instr)
{
    // Store value through pointer: *result = arg1
    // result contains an address, store arg1's value at that address
    
    // Determine the type being stored
    string load_instr = "lw"; // Default to int
    string store_instr = "sw";
    bool is_double_store = false;

    // Try to determine the element type from the pointer/array
    if (instr->result.type == TACOperand::OPERAND_IDENTIFIER ||
        instr->result.type == TACOperand::OPERAND_TEMP)
    {
        Symbol *sym = nullptr;

        // Check if it's a temporary (starts with $t)
        if (instr->result.name.length() >= 2 &&
            instr->result.name[0] == '$' && instr->result.name[1] == 't')
        {
            // Temporaries are not mangled, search all scopes
            for (auto &scope_entry : scope_map)
            {
                sym = scope_entry.second->lookup(instr->result.name);
                if (sym)
                    break;
            }
        }
        else
        {
            // Parse scope ID from the mangled name for regular variables
            size_t underscore_pos = instr->result.name.rfind('_');
            if (underscore_pos != string::npos)
            {
                string scope_str = instr->result.name.substr(underscore_pos + 1);
                bool is_scope_id = !scope_str.empty();
                for (char c : scope_str)
                {
                    if (!isdigit(c))
                    {
                        is_scope_id = false;
                        break;
                    }
                }

                if (is_scope_id)
                {
                    string base_name = instr->result.name.substr(0, underscore_pos);
                    int scope_id = stoi(scope_str);

                    auto it = scope_map.find(scope_id);
                    if (it != scope_map.end())
                    {
                        SymbolTable *scope_table = it->second;
                        sym = scope_table->lookup(base_name);
                    }
                }
            }
        }

        if (sym)
        {
            // If pointer_level > 1, dereferencing stores another pointer (8 bytes)
            // If pointer_level == 1, dereferencing stores the base type
            if (sym->type.pointer_level > 1)
            {
                load_instr = "ld"; // Pointers are 8 bytes
                store_instr = "sd";
            }
            else if (sym->type.pointer_level == 1)
            {
                // Storing through a single-level pointer stores the base type
                PrimitiveType elem_type = sym->type.base_type;
                get_load_store_instructions(elem_type, load_instr, store_instr);
                // Check if it's a double
                if (elem_type == TYPE_DOUBLE)
                {
                    is_double_store = true;
                    load_instr = "fld";
                    store_instr = "fsd";
                }
            }
        }
    }

    // Load the address
    load_operand(instr->result, REG_T1);
    
    // Load the value and store it
    if (is_double_store)
    {
        // For doubles, use FP register
        load_operand(instr->arg1, REG_FT0);
        emit(store_instr + " ft0, 0(t1)");
    }
    else
    {
        // For other types, use integer register
        load_operand(instr->arg1, REG_T0);
        emit(store_instr + " t0, 0(t1)");
    }
}

void RISCVCodeGenerator::generate_param(const TACInstruction *instr)
{
    // Collect parameters for upcoming call
    pending_params.push_back(instr->arg1);
}

void RISCVCodeGenerator::generate_call(const TACInstruction *instr)
{
    // Load parameters into argument registers (a0-a7 for int, fa0-fa7 for double)
    int int_arg_idx = 0;
    int fp_arg_idx = 0;
    
    for (size_t i = 0; i < pending_params.size() && (int_arg_idx < 8 || fp_arg_idx < 8); i++)
    {
        if (is_double_operand(pending_params[i]))
        {
            // Use FP argument register
            if (fp_arg_idx < 8)
            {
                load_operand(pending_params[i], static_cast<RISCVRegister>(REG_FA0 + fp_arg_idx));
                fp_arg_idx++;
            }
        }
        else
        {
            // Use integer argument register
            if (int_arg_idx < 8)
            {
                load_operand(pending_params[i], static_cast<RISCVRegister>(REG_A0 + int_arg_idx));
                int_arg_idx++;
            }
        }
    }

    // Call the function
    string func_name = instr->arg1.name;
    emit("jal ra, " + func_name);

    // Store return value if needed
    if (!instr->result.is_empty())
    {
        // Check if return value is double
        if (is_double_operand(instr->result))
        {
            store_to_operand(REG_FA0, instr->result);
        }
        else
        {
            store_to_operand(REG_A0, instr->result);
        }
    }

    // Clear pending params
    pending_params.clear();
}

void RISCVCodeGenerator::generate_return(const TACInstruction *instr)
{
    if (!instr->arg1.is_empty())
    {
        // Load return value into a0 (int) or fa0 (double)
        if (is_double_operand(instr->arg1))
        {
            load_operand(instr->arg1, REG_FA0);
        }
        else
        {
            load_operand(instr->arg1, REG_A0);
        }
    }

    // Function epilogue - use calculated frame size
    int ra_offset = current_frame_size - 8;
    int s0_offset = current_frame_size - 16;

    emit_load_with_offset("ld", "ra", ra_offset, "sp");
    emit("# restore return address");
    emit_load_with_offset("ld", "s0", s0_offset, "sp");
    emit("# restore frame pointer");
    
    if (current_frame_size >= -2048 && current_frame_size <= 2047)
    {
        emit("addi sp, sp, " + to_string(current_frame_size) + "     # deallocate stack frame");
    }
    else
    {
        emit("li t6, " + to_string(current_frame_size));
        emit("add sp, sp, t6       # deallocate large stack frame");
    }
    emit("ret");
}

void RISCVCodeGenerator::generate_goto(const TACInstruction *instr)
{
    // Get target label from target_line field
    string label = ".L" + to_string(instr->target_line);

    emit("j " + label);
}

void RISCVCodeGenerator::generate_conditional(const TACInstruction *instr)
{
    // Load condition into t0
    load_operand(instr->arg1, REG_T0);

    // Get target label from target_line field
    string label = ".L" + to_string(instr->target_line);

    if (instr->op == TAC_IF_GOTO)
    {
        // Branch if not zero
        emit("bnez " + reg_name(REG_T0) + ", " + label);
    }
    else // TAC_IF_FALSE_GOTO
    {
        // Branch if zero
        emit("beqz " + reg_name(REG_T0) + ", " + label);
    }
}

// ============================================================================
// Prologue and Epilogue (Placeholder for now)
// ============================================================================

void RISCVCodeGenerator::generate_prologue(const string &function_name)
{
    emit_label(function_name);
    emit("addi sp, sp, -64   # allocate stack frame");
    emit("sw ra, 60(sp)      # save return address");
    emit("sw s0, 56(sp)      # save frame pointer");
    emit("addi s0, sp, 64    # set frame pointer");
}

void RISCVCodeGenerator::generate_epilogue()
{
    emit("lw s0, 56(sp)      # restore frame pointer");
    emit("lw ra, 60(sp)      # restore return address");
    emit("addi sp, sp, 64    # deallocate stack frame");
    emit("ret                # return");
}
