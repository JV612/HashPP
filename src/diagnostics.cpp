#include "diagnostics.h"

#include <cstdio>
#include <cstdarg>
#include <string>

namespace {

const char *stage_to_string(DiagnosticStage stage) {
    switch (stage) {
    case DiagnosticStage::Lexical:
        return "Lexical";
    case DiagnosticStage::Parse:
        return "Parse";
    case DiagnosticStage::Semantic:
        return "Semantic";
    }
    return "Unknown";
}

const char *level_to_string(DiagnosticLevel level) {
    switch (level) {
    case DiagnosticLevel::Error:
        return "Error";
    case DiagnosticLevel::Warning:
        return "Warning";
    case DiagnosticLevel::Note:
        return "Note";
    }
    return "Message";
}

void print_prefix(DiagnosticLevel level, DiagnosticStage stage, int line) {
    const char *level_str = level_to_string(level);
    const char *stage_str = stage_to_string(stage);

    if (line > 0) {
        std::fprintf(stderr, "[%s %s] Line %d: ", stage_str, level_str, line);
    } else {
        std::fprintf(stderr, "[%s %s] ", stage_str, level_str);
    }
}

void vreport(DiagnosticLevel level, DiagnosticStage stage, int line, const char *fmt, va_list args) {
    print_prefix(level, stage, line);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
}

} // namespace

void report_diagnostic(DiagnosticLevel level, DiagnosticStage stage, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(level, stage, line, fmt, args);
    va_end(args);
}

void report_lexical_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(DiagnosticLevel::Error, DiagnosticStage::Lexical, line, fmt, args);
    va_end(args);
}

void report_parse_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(DiagnosticLevel::Error, DiagnosticStage::Parse, line, fmt, args);
    va_end(args);
}

void report_semantic_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(DiagnosticLevel::Error, DiagnosticStage::Semantic, line, fmt, args);
    va_end(args);
}

void report_semantic_warning(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(DiagnosticLevel::Warning, DiagnosticStage::Semantic, line, fmt, args);
    va_end(args);
}

void report_internal_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(DiagnosticLevel::Error, DiagnosticStage::Semantic, -1, fmt, args);
    va_end(args);
}
