#ifndef AST_BASE_H
#define AST_BASE_H

#include <string>

// ============================================================================
// AST Base - Core AST Infrastructure
// ============================================================================
// Base class for all AST nodes
// Each node tracks source location for error reporting
// ============================================================================

/**
 * Base class for all AST nodes
 * Provides common interface and location tracking
 */
class ASTNode
{
public:
    int line_no;   // Source line number (for error reporting)
    int column_no; // Source column number (for error reporting)

    ASTNode() : line_no(0), column_no(0) {}
    virtual ~ASTNode() {}

    // Every AST node must be able to convert itself to a string representation
    virtual std::string to_string() const = 0;
};

#endif // AST_BASE_H
