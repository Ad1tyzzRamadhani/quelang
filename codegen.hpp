
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

std::string emitExpr(const ::Expr& expr);
std::string inferExprType(const ::Expr& expr);
std::string emitLiteral(const ::Literal& literal);
std::string emitUnary(const ::Expr& expr);
std::string emitBinary(const ::Expr& expr);
std::string emitCast(const ::Expr& expr);

std::string makeTemp();
std::string normalizeType(std::string type);

std::size_t next_temp_ = 0;
std::unordered_map<std::string, std::string> value_types_;

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

inline void Generator::reset() {
    output_.clear();
    diagnostics_.clear();
    indent_ = 0;
    next_temp_ = 0;
    value_types_.clear();
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

inline std::string Generator::makeTemp() {
    return "%v" + std::to_string(next_temp_++);
}

inline std::string Generator::normalizeType(std::string type) {
    if (type == "char") return "i8";
    if (type == "string" || type == "String") return "ptr";
    return type;
}

inline std::string Generator::emitLiteral(
    const ::Literal& literal
) {
    using Kind = ::Literal::Kind;

    std::string type;
    std::string value = literal.value;

    switch (literal.kind) {
    case Kind::Number:
        // Tipe integer default sementara.
        type = "i32";
        break;

    case Kind::Hex:
    case Kind::Binary:
        type = "i32";
        break;

    case Kind::Float:
        type = "f64";
        break;

    case Kind::True:
        type = "bool";
        value = "true";
        break;

    case Kind::False:
        type = "bool";
        value = "false";
        break;

    case Kind::Null:
        type = "ptr";
        value = "null";
        break;

    case Kind::Char: {
        // Grammar CRU saat ini tidak memiliki char literal.
        // Untuk ASCII satu karakter, turunkan menjadi integer i8.
        if (value.size() >= 3 &&
            value.front() == '\'' &&
            value.back() == '\'') {
            value = value.substr(1, value.size() - 2);
        }

        if (value.size() != 1) {
            fail(
                "Character literal must be a single-byte character "
                "until escape/Unicode lowering is implemented",
                &literal
            );
        }

        const auto byte =
            static_cast<unsigned char>(value.front());

        type = "i8";
        value = std::to_string(static_cast<unsigned int>(byte));
        break;
    }

    case Kind::String:
    case Kind::RawString:
        fail(
            "String literal lowering requires CRU string storage "
            "or a string constant representation",
            &literal
        );

    case Kind::None:
        fail(
            "'none' has no CRU literal representation",
            &literal
        );
    }

    type = normalizeType(std::move(type));

    const std::string result = makeTemp();
    emitLine(result + " = const." + type + " " + value);
    value_types_[result] = type;

    return result;
}

inline std::string Generator::inferExprType(
    const ::Expr& expr
) {
    using Kind = ::Expr::Kind;

    switch (expr.kind) {
    case Kind::Literal: {
        switch (expr.literal->kind) {
        case ::Literal::Kind::Number:
        case ::Literal::Kind::Hex:
        case ::Literal::Kind::Binary:
            return "i32";

        case ::Literal::Kind::Float:
            return "f64";

        case ::Literal::Kind::Char:
            return "i8";

        case ::Literal::Kind::String:
        case ::Literal::Kind::RawString:
        case ::Literal::Kind::Null:
            return "ptr";

        case ::Literal::Kind::True:
        case ::Literal::Kind::False:
            return "bool";

        case ::Literal::Kind::None:
            fail("'none' does not have a CRU value type", &expr);
        }
        break;
    }

    case Kind::Ident:
        fail(
            "Identifier type lookup requires a symbol table",
            &expr
        );

    case Kind::Cast:
        if (!expr.cast.target) {
            fail("Cast expression has no target type", &expr);
        }
        return normalizeType(emitType(*expr.cast.target));

    case Kind::Unary:
        if (!expr.unary.expr) {
            fail("Unary expression has no operand", &expr);
        }
        return inferExprType(*expr.unary.expr);

    case Kind::Binary:
        if (!expr.binary.lhs || !expr.binary.rhs) {
            fail("Binary expression has a missing operand", &expr);
        }
        return inferExprType(*expr.binary.lhs);

    default:
        fail(
            "Type inference for this expression kind "
            "is not implemented yet",
            &expr
        );
    }

    fail("Cannot infer expression type", &expr);
}

inline std::string Generator::emitUnary(const ::Expr& expr) {
    using Op = ::UnaryOp;

    if (!expr.unary.expr) {
        fail("Unary expression has no operand", &expr);
    }

    const std::string operand = emitExpr(*expr.unary.expr);
    const std::string type = inferExprType(*expr.unary.expr);

    std::string opcode;

    switch (expr.unary.op) {
    case Op::Neg:
        opcode = "neg";
        break;

    case Op::Not:
        opcode = "not";
        break;

    case Op::BitNot:
        opcode = "bitnot";
        break;
    }

    const std::string result = makeTemp();

    emitLine(
        result + " = " + opcode + "." + type + " " + operand
    );

    value_types_[result] = type;
    return result;
}

inline std::string Generator::emitBinary(const ::Expr& expr) {
    using Op = ::BinaryOp;

    if (!expr.binary.lhs || !expr.binary.rhs) {
        fail("Binary expression has a missing operand", &expr);
    }

    const std::string lhs = emitExpr(*expr.binary.lhs);
    const std::string rhs = emitExpr(*expr.binary.rhs);

    const std::string type = inferExprType(*expr.binary.lhs);

    std::string opcode;

    switch (expr.binary.op) {
    case Op::Add:   opcode = "add";   break;
    case Op::Sub:   opcode = "sub";   break;
    case Op::Mul:   opcode = "mul";   break;
    case Op::Div:   opcode = "div";   break;
    case Op::Rem:   opcode = "rem";   break;
    case Op::And:   opcode = "and";   break;
    case Op::Or:    opcode = "or";    break;
    case Op::Xor:   opcode = "xor";   break;
    case Op::Shl:   opcode = "shl";   break;
    case Op::Shr:   opcode = "shr";   break;
    }

    const std::string result = makeTemp();

    emitLine(
        result + " = " + opcode + "." + type + " " +
        lhs + ", " + rhs
    );

    value_types_[result] = type;
    return result;
}

inline std::string Generator::emitCast(const ::Expr& expr) {
    if (!expr.cast.base || !expr.cast.target) {
        fail("Invalid cast expression", &expr);
    }

    const std::string operand = emitExpr(*expr.cast.base);
    const std::string target =
        normalizeType(emitType(*expr.cast.target));

    const std::string result = makeTemp();

    emitLine(
        result + " = cast " + operand + " to " + target
    );

    value_types_[result] = target;
    return result;
}

inline std::string Generator::emitExpr(const ::Expr& expr) {
    using Kind = ::Expr::Kind;

    switch (expr.kind) {
    case Kind::Literal:
        if (!expr.literal) {
            fail("Literal expression has no literal value", &expr);
        }
        return emitLiteral(*expr.literal);

    case Kind::Unary:
        return emitUnary(expr);

    case Kind::Binary:
        return emitBinary(expr);

    case Kind::Cast:
        return emitCast(expr);

    case Kind::Ident:
        fail(
            "Identifier lowering requires local/global symbol lookup",
            &expr
        );

    case Kind::Assign:
        fail("Assignment lowering is not implemented yet", &expr);

    case Kind::Ternary:
        fail("Ternary lowering requires control-flow blocks", &expr);

    case Kind::Postfix:
        fail("Postfix lowering is not implemented yet", &expr);

    case Kind::New:
        fail("New expression lowering is not implemented yet", &expr);

    case Kind::Move:
        fail("Move expression lowering is not implemented yet", &expr);

    case Kind::StructInit:
        fail("Struct initializer lowering is not implemented yet", &expr);

    case Kind::ArrayLiteral:
        fail("Array literal lowering is not implemented yet", &expr);
    }

    fail("Unknown expression kind", &expr);
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

    pushIndent();
    emitStmt(*function.body);

    popIndent();
    emitLine("end");
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

inline void Generator::emitStmt(const ::Stmt& stmt) {
    using Kind = ::Stmt::Kind;

    switch (stmt.kind) {
    case Kind::Block: {
        for (const auto& child : stmt.block.stmts) {
            if (!child) {
                fail("Null statement found inside block", &stmt);
            }

            emitStmt(*child);
        }
        return;
    }

    case Kind::Return: {
        if (!stmt.return_values.empty()) {
            fail(
                "Return values require expression lowering, "
                "which is not implemented yet",
                &stmt
            );
        }

        emitLine("ret");
        return;
    }

    case Kind::ExprStmt:
        fail(
            "Expression statement lowering is not implemented yet",
            &stmt
        );

    case Kind::VarDecl:
        fail(
            "Local variable declaration lowering is not implemented yet",
            &stmt
        );

    case Kind::If:
        fail("If statement lowering is not implemented yet", &stmt);

    case Kind::While:
        fail("While statement lowering is not implemented yet", &stmt);

    case Kind::DoStmt:
        fail("Do-while statement lowering is not implemented yet", &stmt);

    case Kind::For:
        fail("For statement lowering is not implemented yet", &stmt);

    case Kind::SwitchCase:
        fail("Switch statement lowering is not implemented yet", &stmt);

    case Kind::Break:
        fail("Break statement lowering is not implemented yet", &stmt);

    case Kind::Continue:
        fail("Continue statement lowering is not implemented yet", &stmt);

    case Kind::Resume:
        fail("Resume statement lowering is not implemented yet", &stmt);

    case Kind::Yield:
        fail("Yield statement lowering is not implemented yet", &stmt);

    case Kind::ThrowStmt:
        fail("Throw statement lowering is not implemented yet", &stmt);

    case Kind::Drop:
        fail("Drop statement lowering is not implemented yet", &stmt);

    case Kind::Wipe:
        fail("Wipe statement lowering is not implemented yet", &stmt);

    case Kind::Unsafe:
        fail("Unsafe block lowering is not implemented yet", &stmt);

    case Kind::Label:
        fail("Label lowering is not implemented yet", &stmt);

    case Kind::Jump:
        fail("Jump statement lowering is not implemented yet", &stmt);

    case Kind::UseStmt:
        fail("Use statement lowering is not implemented yet", &stmt);
    }

    fail("Unknown statement kind", &stmt);
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
