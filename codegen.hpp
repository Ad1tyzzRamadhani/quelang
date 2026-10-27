
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
    // Program traversal
void emitStruct(const ::StructDef& structure);
void emitUnion(const ::UnionDef& union_def);
void emitEnum(const ::EnumDef& enum_def);
void emitNamespace(const ::NamespaceDecl& ns);
void emitForwardDecl(const ::ForwardDecl& decl);
std::string emitQualifiedName(const ::QualifiedName& name) const;
std::string emitFunctionName(const ::Function& function);
void emitFunctionSignature(const ::Function& function);

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

inline void Generator::emitProgram(
    const ::FileProgram& program
) {
    if (options_.emit_source_info && !program.file.empty()) {
        emitLine("// Generated from: " + program.file);
        emitLine();
    }

    bool first = true;

    for (const auto& decl : program.decls) {
        if (!decl) {
            fail("Null declaration found in FileProgram");
        }

        if (!first) {
            emitLine();
        }

        emitTopLevel(*decl);
        first = false;
    }
}

inline void Generator::emitTopLevel(
    const ::Node& node
) {
    if (const auto* function =
            dynamic_cast<const ::Function*>(&node)) {
        emitFunction(*function);
        return;
    }

    if (const auto* structure =
            dynamic_cast<const ::StructDef*>(&node)) {
        emitStruct(*structure);
        return;
    }

    if (const auto* union_def =
            dynamic_cast<const ::UnionDef*>(&node)) {
        emitUnion(*union_def);
        return;
    }

    if (const auto* enum_def =
            dynamic_cast<const ::EnumDef*>(&node)) {
        emitEnum(*enum_def);
        return;
    }

    if (const auto* ns =
            dynamic_cast<const ::NamespaceDecl*>(&node)) {
        emitNamespace(*ns);
        return;
    }

    if (const auto* forward =
            dynamic_cast<const ::ForwardDecl*>(&node)) {
        emitForwardDecl(*forward);
        return;
    }

    if (dynamic_cast<const ::VarDecl*>(&node)) {
        fail("Global variable codegen is not implemented", &node);
    }

    fail("Unsupported top-level declaration", &node);
}

inline void Generator::emitNamespace(
    const ::NamespaceDecl& ns
) {
    for (const auto& item : ns.items) {
        if (!item) {
            fail("Null declaration inside namespace", &ns);
        }

        emitTopLevel(*item);
    }
}

inline void Generator::emitFunction(
    const ::Function& function
) {
    emitFunctionSignature(function);

    if (!function.body) {
        // Declaration without a body.
        emitLine();
        return;
    }

    fail(
        "Function body codegen is not implemented yet",
        &function
    );
}

inline void Generator::emitStruct(
    const ::StructDef& structure
) {
    fail("Struct codegen is not implemented yet", &structure);
}

inline void Generator::emitUnion(
    const ::UnionDef& union_def
) {
    fail("Union codegen is not implemented yet", &union_def);
}

inline std::string Generator::emitQualifiedName(
    const ::QualifiedName& name
) const {
    if (name.parts.empty()) {
        throw CodegenError(
            "Invalid empty qualified type name"
        );
    }

    std::string result;

    for (const auto& part : name.parts) {
        if (part.empty()) {
            throw CodegenError(
                "Invalid empty component in qualified type name"
            );
        }

        if (!result.empty()) {
            result += "::";
        }

        result += part;
    }

    return result;
}

inline std::string Generator::emitType(
    const ::Type& type
) {
    if (!type.base) {
        fail("Type is missing its base name", &type);
    }

    if (type.bit_width) {
        fail(
            "Bit-width types are not implemented yet",
            &type
        );
    }

    std::string result = emitQualifiedName(*type.base);

    // Base types are passed through unchanged for now.
    // This assumes that QueLang and CRU share the same
    // spelling for the supported primitive types.
    for (const auto& modifier : type.modifiers) {
        switch (modifier.kind) {
        case ::TypeModifier::Kind::Pointer:
            // CRU currently uses a generic pointer type.
            result = "ptr";
            break;

        case ::TypeModifier::Kind::Reference:
            fail(
                "Reference type lowering is not implemented yet",
                &type
            );

        case ::TypeModifier::Kind::Restrict:
            fail(
                "Restrict type lowering is not implemented yet",
                &type
            );

        case ::TypeModifier::Kind::FuncPtr:
            fail(
                "Function pointer type lowering is not implemented yet",
                &type
            );
        }
    }

    for (const auto qualifier : type.qualifiers) {
        switch (qualifier) {
        case ::TypeQualifier::Const:
        case ::TypeQualifier::Volatile:
            // These qualifiers do not change the storage type.
            // Their semantic effects must be handled elsewhere.
            break;

        case ::TypeQualifier::Atomic:
            fail(
                "Atomic type lowering is not implemented yet",
                &type
            );

        case ::TypeQualifier::Unaligned:
            fail(
                "Unaligned type lowering is not implemented yet",
                &type
            );
        }
    }

    return result;
}

inline std::string Generator::emitFunctionName(
    const ::Function& function
) {
    if (!function.name) {
        fail("Function is missing its name", &function);
    }

    return emitQualifiedName(*function.name);
}

inline void Generator::emitFunctionSignature(
    const ::Function& function
) {
    if (!function.return_types) {
        fail("Function is missing its return type", &function);
    }

    const std::string return_type =
        emitType(*function.return_types);

    const std::string name =
        emitFunctionName(function);

    std::string signature =
        "def " + return_type + " " + name + "(";

    for (std::size_t i = 0; i < function.params.size(); ++i) {
        const auto& param = function.params[i];

        if (!param.type) {
            fail("Function parameter is missing its type", &function);
        }

        if (param.name.empty()) {
            fail("Function parameter is missing its name", &function);
        }

        if (i != 0) {
            signature += ", ";
        }

        signature += emitType(*param.type);
        signature += " ";
        signature += param.name;
    }

    signature += ")";
    emitLine(signature);
}

inline void Generator::emitEnum(
    const ::EnumDef& enum_def
) {
    fail("Enum codegen is not implemented yet", &enum_def);
}

inline void Generator::emitForwardDecl(
    const ::ForwardDecl& decl
) {
    fail("Forward declaration codegen is not implemented yet", &decl);
}

} // namespace quelang::codegen
