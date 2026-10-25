
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// QueLang AST
#include "ast.hpp"

namespace quelang::codegen {

// ============================================================
// Codegen Options
// ============================================================

struct CodegenOptions {
    // Emit comments containing source locations, when available.
    bool emit_debug_comments = false;

    // Emit a source filename comment in the generated CRU.
    bool emit_source_info = true;

    // Reject AST constructs that are not implemented yet.
    bool strict = true;
};

// ============================================================
// Diagnostic
// ============================================================

struct CodegenDiagnostic {
    enum class Severity {
        Warning,
        Error
    };

    Severity severity = Severity::Error;
    std::string message;
    std::string file;

    int line = -1;
    int column = -1;
};

// ============================================================
// Codegen Error
// ============================================================

class CodegenError final : public std::runtime_error {
public:
    explicit CodegenError(const std::string& message)
        : std::runtime_error(message) {}
};

// ============================================================
// Generator
// ============================================================

class Generator {
public:
    explicit Generator(CodegenOptions options = {})
        : options_(options) {}

    // Generate CRU text from a complete QueLang program.
    std::string generate(const ::FileProgram& program);

    // Read-only access to diagnostics from the last generation.
    const std::vector<CodegenDiagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    // --------------------------------------------------------
    // State
    // --------------------------------------------------------

    CodegenOptions options_;
    std::vector<CodegenDiagnostic> diagnostics_;

    std::string output_;
    std::size_t indent_ = 0;

    // --------------------------------------------------------
    // Output helpers
    // --------------------------------------------------------

    // --------------------------------------------------------
// Output helpers
// --------------------------------------------------------

inline void emit(std::string_view text) {
    output_.append(text.data(), text.size());
}

inline void emitIndent() {
    output_.append(indent_ * 4, ' ');
}

inline void emitLine(std::string_view text = {}) {
    if (!text.empty()) {
        emitIndent();
        emit(text);
    }

    output_.push_back('\n');
}

inline void pushIndent() noexcept {
    ++indent_;
}

inline void popIndent() {
    if (indent_ == 0) {
        throw CodegenError(
            "Internal codegen error: indentation underflow"
        );
    }

    --indent_;
}

    // --------------------------------------------------------
    // Diagnostics
    // --------------------------------------------------------

    // --------------------------------------------------------
// Diagnostics
// --------------------------------------------------------

inline void report(
    CodegenDiagnostic::Severity severity,
    std::string message,
    const ::Node* node = nullptr
) {
    CodegenDiagnostic diagnostic;

    diagnostic.severity = severity;
    diagnostic.message = std::move(message);

    if (node != nullptr) {
        diagnostic.file = node->file;
        diagnostic.line = node->line_start;
        diagnostic.column = node->column_start;
    }

    diagnostics_.push_back(std::move(diagnostic));
}

[[noreturn]] inline void fail(
    std::string message,
    const ::Node* node = nullptr
) {
    if (node != nullptr) {
        message =
            node->file + ":" +
            std::to_string(node->line_start) + ":" +
            std::to_string(node->column_start) + ": " +
            message;
    }

    report(
        CodegenDiagnostic::Severity::Error,
        message,
        node
    );

    throw CodegenError(message);
}
    [[noreturn]] void fail(
        std::string message,
        const ::Node* node = nullptr
    );

    // --------------------------------------------------------
    // Top-level declarations
    // --------------------------------------------------------

    void emitProgram(const ::FileProgram& program);
    void emitTopLevel(const ::Node& node);
    void emitFunction(const ::Function& function);

    // --------------------------------------------------------
    // Types, expressions and statements
    // Implementations will be added incrementally.
    // --------------------------------------------------------

    std::string emitType(const ::Type& type);

    void emitExpr(const ::Expr& expr);
    void emitStmt(const ::Stmt& stmt);

    // --------------------------------------------------------
// State management
// --------------------------------------------------------

inline void reset() {
    output_.clear();
    diagnostics_.clear();
    indent_ = 0;
}
};

// ============================================================
// Public API
// ============================================================

inline std::string Generator::generate(
    const ::FileProgram& program
) {
    reset();

    emitProgram(program);

    return output_;
}

inline std::string generateCRU(
    const ::FileProgram& program,
    const CodegenOptions& options
) {
    Generator generator(options);
    return generator.generate(program);
}

} // namespace quelang::codegen
