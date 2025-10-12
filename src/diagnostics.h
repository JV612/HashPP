#pragma once

#include <string>
#include <vector>

#include "ast.h"

enum class DiagnosticSeverity {
    Note,
    Warning,
    Error,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    SourceRange range;
};

class DiagnosticReporter {
public:
    void report(DiagnosticSeverity severity, const std::string &message, const SourceRange &range = {});

    const std::vector<Diagnostic>& diagnostics() const { return entries; }
    bool has_errors() const;

    void clear();

private:
    std::vector<Diagnostic> entries;
};
