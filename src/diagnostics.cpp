#include "diagnostics.h"

void DiagnosticReporter::report(DiagnosticSeverity severity, const std::string &message, const SourceRange &range) {
    entries.push_back({severity, message, range});
}

bool DiagnosticReporter::has_errors() const {
    for (const auto &diag : entries) {
        if (diag.severity == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

void DiagnosticReporter::clear() {
    entries.clear();
}
