#include "tac.h"
#include "symbol_table.h"
#include <iostream>
#include <sstream>

using namespace std;

// Global TAC generator
TACGenerator tacGen;

// ============================================================================
// TACOperand Implementation
// ============================================================================

string TACOperand::to_string() const
{
    if (is_empty())
        return "";
    // For string operands, the name already includes quotes from tokenizer
    if (type == OPERAND_STRING)
        return name;
    return name;
}

// ============================================================================
// TACInstruction Implementation
// ============================================================================

string TACInstruction::to_string() const
{
    stringstream ss;

    switch (op)
    {
    case TAC_ASSIGN:
        ss << result.to_string() << " = " << arg1.to_string();
        break;

    case TAC_ADD:
        ss << result.to_string() << " = " << arg1.to_string()
           << " + " << arg2.to_string();
        break;

    case TAC_SUB:
        ss << result.to_string() << " = " << arg1.to_string()
           << " - " << arg2.to_string();
        break;

    case TAC_MUL:
        ss << result.to_string() << " = " << arg1.to_string()
           << " * " << arg2.to_string();
        break;

    case TAC_DIV:
        ss << result.to_string() << " = " << arg1.to_string()
           << " / " << arg2.to_string();
        break;

    case TAC_MOD:
        ss << result.to_string() << " = " << arg1.to_string()
           << " % " << arg2.to_string();
        break;

    case TAC_UMINUS:
        ss << result.to_string() << " = -" << arg1.to_string();
        break;

    case TAC_UPLUS:
        ss << result.to_string() << " = +" << arg1.to_string();
        break;

    case TAC_PRE_INC:
        ss << result.to_string() << " = " << result.to_string() << " + 1";
        break;

    case TAC_PRE_DEC:
        ss << result.to_string() << " = " << result.to_string() << " - 1";
        break;

    case TAC_POST_INC:
        ss << result.to_string() << " = " << result.to_string() << " + 1";
        break;

    case TAC_POST_DEC:
        ss << result.to_string() << " = " << result.to_string() << " - 1";
        break;

    case TAC_ADDR_OF:
        ss << result.to_string() << " = &" << arg1.to_string();
        break;

    case TAC_DEREF:
        ss << result.to_string() << " = *" << arg1.to_string();
        break;

    case TAC_DEREF_STORE:
        ss << "*" << result.to_string() << " = " << arg1.to_string();
        break;

    case TAC_MEMBER_ACCESS:
        ss << result.to_string() << " = " << arg1.to_string()
           << " + " << arg2.to_string() << " (member offset)";
        break;

    case TAC_BITWISE_AND:
        ss << result.to_string() << " = " << arg1.to_string()
           << " & " << arg2.to_string();
        break;

    case TAC_BITWISE_OR:
        ss << result.to_string() << " = " << arg1.to_string()
           << " | " << arg2.to_string();
        break;

    case TAC_BITWISE_XOR:
        ss << result.to_string() << " = " << arg1.to_string()
           << " ^ " << arg2.to_string();
        break;

    case TAC_BITWISE_NOT:
        ss << result.to_string() << " = ~" << arg1.to_string();
        break;

    case TAC_LEFT_SHIFT:
        ss << result.to_string() << " = " << arg1.to_string()
           << " << " << arg2.to_string();
        break;

    case TAC_RIGHT_SHIFT:
        ss << result.to_string() << " = " << arg1.to_string()
           << " >> " << arg2.to_string();
        break;

    case TAC_LT:
        ss << result.to_string() << " = " << arg1.to_string()
           << " < " << arg2.to_string();
        break;

    case TAC_GT:
        ss << result.to_string() << " = " << arg1.to_string()
           << " > " << arg2.to_string();
        break;

    case TAC_LE:
        ss << result.to_string() << " = " << arg1.to_string()
           << " <= " << arg2.to_string();
        break;

    case TAC_GE:
        ss << result.to_string() << " = " << arg1.to_string()
           << " >= " << arg2.to_string();
        break;

    case TAC_EQ:
        ss << result.to_string() << " = " << arg1.to_string()
           << " == " << arg2.to_string();
        break;

    case TAC_NE:
        ss << result.to_string() << " = " << arg1.to_string()
           << " != " << arg2.to_string();
        break;

    case TAC_LOGICAL_AND:
        ss << result.to_string() << " = " << arg1.to_string()
           << " && " << arg2.to_string();
        break;

    case TAC_LOGICAL_OR:
        ss << result.to_string() << " = " << arg1.to_string()
           << " || " << arg2.to_string();
        break;

    case TAC_LOGICAL_NOT:
        ss << result.to_string() << " = !" << arg1.to_string();
        break;

    // Control Flow
    case TAC_LABEL:
        ss << result.to_string() << ":";
        break;

    case TAC_GOTO:
        if (target_line >= 0)
            ss << "goto " << target_line;
        else if (!result.is_empty() && result.type == TACOperand::OPERAND_LABEL)
            ss << "goto " << result.to_string() << " [UNRESOLVED]";
        else
            ss << "goto [BACKPATCH]";
        break;

    case TAC_IF_GOTO:
        if (target_line >= 0)
            ss << "if " << arg1.to_string() << " goto " << target_line;
        else
            ss << "if " << arg1.to_string() << " goto [BACKPATCH]";
        break;

    case TAC_IF_FALSE_GOTO:
        if (target_line >= 0)
            ss << "ifFalse " << arg1.to_string() << " goto " << target_line;
        else
            ss << "ifFalse " << arg1.to_string() << " goto [BACKPATCH]";
        break;
    case TAC_RETURN:
        if (!arg1.is_empty())
            ss << "return " << arg1.to_string();
        else
            ss << "return";
        break;

    case TAC_PARAM:
        ss << "param " << arg1.to_string();
        break;

    case TAC_CALL:
        // result = call func, nArgs (arg1.name = func label/name, arg2.name = nArgs)
        if (!result.is_empty())
            ss << result.to_string() << " = ";
        ss << "call " << arg1.to_string();
        if (!arg2.is_empty())
            ss << ", " << arg2.to_string();
        break;
    }

    return ss.str();
}

// ============================================================================
// TACGenerator Implementation
// ============================================================================

TACGenerator::~TACGenerator()
{
    clear();
}

TACOperand TACGenerator::newTemp()
{
    stringstream ss;
    ss << "_t" << temp_counter++;
    return TACOperand(TACOperand::OPERAND_TEMP, ss.str());
}

TACOperand TACGenerator::newLabel()
{
    stringstream ss;
    ss << "L" << label_counter++;
    return TACOperand(TACOperand::OPERAND_LABEL, ss.str());
}

int TACGenerator::emit(TACOp op, TACOperand result, TACOperand arg1, TACOperand arg2)
{
    TACInstruction *instr = new TACInstruction(op, result, arg1, arg2);
    instr->line_number = code.size();
    code.push_back(instr);

    // Print as we generate (for debugging)
    if (debug)
        cout << "[TAC] " << instr->line_number << ": " << instr->to_string() << endl;

    return instr->line_number;
}

void TACGenerator::backpatch(InstructionList list, int target)
{
    for (int index : list)
    {
        if (index >= 0 && index < (int)code.size())
        {
            code[index]->target_line = target;
        }
    }
}

void TACGenerator::emit_label(const std::string &label_name, int instr_index)
{
    label_positions[label_name] = instr_index;

    // Backpatch any gotos waiting for this label
    auto it = label_patchlists.find(label_name);
    if (it != label_patchlists.end())
    {
        for (int idx : it->second)
        {
            code[idx]->target_line = instr_index;
        }
        label_patchlists.erase(it);
    }
}

void TACGenerator::emit_goto(const std::string &label_name, int instr_index)
{
    auto it = label_positions.find(label_name);
    if (it != label_positions.end())
    {
        code[instr_index]->target_line = it->second;
    }
    else
    {
        label_patchlists[label_name].push_back(instr_index);
    }
}

void TACGenerator::finalize_labels()
{
    for (const auto &entry : label_patchlists)
    {
        for (int idx : entry.second)
        {
            cout << "[Error] Unresolved label '" << entry.first << "' for instruction at line " << code[idx]->line_number << endl;
            semantic_error_count++;
        }
    }

    label_patchlists.clear();
}

void TACGenerator::print() const
{
    if (code.empty())
    {
        if (debug)
            cout << "\n[No TAC generated]\n"
                 << endl;
        return;
    }

    cout << "\n========== THREE-ADDRESS CODE ==========\n";
    for (const TACInstruction *instr : code)
    {
        // Print line number with padding for alignment
        cout << instr->line_number << ": " << instr->to_string() << endl;
    }
    cout << "========================================\n"
         << endl;
}

void TACGenerator::clear()
{
    for (TACInstruction *instr : code)
    {
        delete instr;
    }
    code.clear();
    temp_counter = 0;
    label_counter = 0;
}

// ============================================================================
// Backpatching Helper Functions
// ============================================================================

InstructionList makelist(int index)
{
    InstructionList list;
    list.push_back(index);
    return list;
}

InstructionList merge(InstructionList list1, InstructionList list2)
{
    InstructionList result = list1;
    result.insert(result.end(), list2.begin(), list2.end());
    return result;
}

void backpatch(InstructionList list, int target)
{
    tacGen.backpatch(list, target);
}
