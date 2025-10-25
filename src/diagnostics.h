#ifndef HASHPP_DIAGNOSTICS_H
#define HASHPP_DIAGNOSTICS_H

#include <cstdarg>

enum class DiagnosticStage {
    Lexical,
    Parse,
    Semantic
};

enum class DiagnosticLevel {
    Error,
    Warning,
    Note
};

void report_diagnostic(DiagnosticLevel level,
                       DiagnosticStage stage,
                       int line,
                       const char *fmt,
                       ...);

void report_lexical_error(int line, const char *fmt, ...);
void report_parse_error(int line, const char *fmt, ...);
void report_semantic_error(int line, const char *fmt, ...);
void report_semantic_warning(int line, const char *fmt, ...);
void report_internal_error(const char *fmt, ...);

#endif // HASHPP_DIAGNOSTICS_H
