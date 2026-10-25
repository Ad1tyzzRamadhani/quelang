
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

    void emit(std::string_view text);
    void emitLine(std::string_view text = {});

    void emitIndent();
    void pushIndent() noexcept;
    void popIndent();

    // --------------------------------------------------------
    // Diagnostics
    // --------------------------------------------------------

    void report(
        CodegenDiagnostic::Severity severity,
        std::string message,
        const ::Node* node = nullptr
    );

    [[noreturn]] void fail(
        std::string message,
        const ::Node* node = nullptr
    );

    // --------------------------------------------------------
    // Top-level declarations
    // --------------------------------------------------------

    void emitTopLevel(const ::Node& node);
    void emitFunction(const ::Function& function);

    // --------------------------------------------------------
    // Types, expressions and statements
    // Implementations will be added incrementally.
    // --------------------------------------------------------

    std::string emitType(const ::Type& type);

    void emitExpr(const ::Expr& expr);
    void emitStmt(const ::Stmt& stmt);

    // Prevent accidental reuse of stale output state.
    void reset();
};

// ============================================================
// Public API
// ============================================================

std::string generateCRU(
    const ::FileProgram& program,
    const CodegenOptions& options = {}
);

} // namespace quelang::codegen
