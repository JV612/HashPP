#include "ir_generator.h"
#include <iostream>
#include <iomanip>

std::string IRGenerator::new_temp() {
    return "t" + std::to_string(temp_counter++);
}

std::string IRGenerator::new_label() {
    return "L" + std::to_string(label_counter++);
}

void IRGenerator::emit(IROpcode opcode, const std::string& result,
                       const std::string& operand1, const std::string& operand2,
                       const std::string& label, int line) {
    instructions.emplace_back(opcode, result, operand1, operand2, label, line);
}

std::string IRGenerator::opcode_to_string(IROpcode opcode) {
    switch (opcode) {
        case IROpcode::ADD: return "ADD";
        case IROpcode::SUB: return "SUB"; 
        case IROpcode::MUL: return "MUL";
        case IROpcode::DIV: return "DIV";
        case IROpcode::MOD: return "MOD";
        case IROpcode::AND: return "AND";
        case IROpcode::OR: return "OR";
        case IROpcode::NOT: return "NOT";
        case IROpcode::BIT_AND: return "BIT_AND";
        case IROpcode::BIT_OR: return "BIT_OR";
        case IROpcode::BIT_XOR: return "BIT_XOR";
        case IROpcode::BIT_NOT: return "BIT_NOT";
        case IROpcode::LEFT_SHIFT: return "LEFT_SHIFT";
        case IROpcode::RIGHT_SHIFT: return "RIGHT_SHIFT";
        case IROpcode::EQ: return "EQ";
        case IROpcode::NE: return "NE";
        case IROpcode::LT: return "LT";
        case IROpcode::LE: return "LE";
        case IROpcode::GT: return "GT";
        case IROpcode::GE: return "GE";
        case IROpcode::ASSIGN: return "ASSIGN";
        case IROpcode::LOAD: return "LOAD";
        case IROpcode::STORE: return "STORE";
        case IROpcode::LABEL: return "LABEL";
        case IROpcode::JUMP: return "JUMP";
        case IROpcode::CJUMP: return "CJUMP";
        default: return "UNKNOWN";
    }
}

std::string IRGenerator::get_operator_symbol(IROpcode opcode) {
    switch (opcode) {
        case IROpcode::ADD: return "+";
        case IROpcode::SUB: return "-"; 
        case IROpcode::MUL: return "*";
        case IROpcode::DIV: return "/";
        case IROpcode::MOD: return "%";
        case IROpcode::AND: return "&&";
        case IROpcode::OR: return "||";
        case IROpcode::NOT: return "!";
        case IROpcode::BIT_AND: return "&";
        case IROpcode::BIT_OR: return "|";
        case IROpcode::BIT_XOR: return "^";
        case IROpcode::BIT_NOT: return "~";
        case IROpcode::LEFT_SHIFT: return "<<";
        case IROpcode::RIGHT_SHIFT: return ">>";
        case IROpcode::EQ: return "==";
        case IROpcode::NE: return "!=";
        case IROpcode::LT: return "<";
        case IROpcode::LE: return "<=";
        case IROpcode::GT: return ">";
        case IROpcode::GE: return ">=";
        default: return "?";
    }
}

void IRGenerator::print_ir() const {
    std::cout << "\n=== Three Address Code (TAC) ===" << std::endl;
    
    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& instr = instructions[i];
        
        // Print line number prefix
        if (instr.line_number > 0) {
            std::cout << "L" << instr.line_number << ": ";
        } else {
            std::cout << "    : ";
        }
        
        if (instr.opcode == IROpcode::LABEL) {
            std::cout << instr.label << ":" << std::endl;
        } else if (instr.opcode == IROpcode::ASSIGN) {
            // Assignments: "result = operand1"
            std::cout << instr.result << " = " << instr.operand1 << std::endl;
        } else if (instr.opcode == IROpcode::JUMP) {
            // Unconditional jump: "goto label"
            std::cout << "goto " << instr.label << std::endl;
        } else if (instr.opcode == IROpcode::CJUMP) {
            // Conditional jump: "if operand1 == operand2 goto label"
            std::cout << "if " << instr.operand1 << " == " << instr.operand2 << " goto " << instr.label << std::endl;
        } else {
            // Binary operations: "result = operand1 OP operand2"
            std::cout << instr.result << " = " << instr.operand1;
            if (!instr.operand2.empty()) {
                std::cout << " " << get_operator_symbol(instr.opcode) << " " << instr.operand2;
            }
            std::cout << std::endl;
        }
    }
    std::cout << "===================================" << std::endl;
}