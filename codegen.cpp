#include <sstream>
#include <string>
#include <memory>
#include <vector>
#include <stdexcept>

class QuelangToCruCodegen {
public:
    std::string generate(FileProgram* program) {
        out_.str("");
        out_.clear();

        for (const auto& node : program->decls) {
            emitNode(node.get());
        }

        return out_.str();
    }

private:
    std::ostringstream out_;

    void emitNode(Node* node) {
        if (!node) return;

        // Cek jenis node berdasarkan dynamic_cast atau struktur pointer
        if (auto func = dynamic_cast<Function*>(node)) {
            emitFunction(func);
        } else if (auto structDef = dynamic_cast<StructDef*>(node)) {
            emitStruct(structDef);
        } else if (auto unionDef = dynamic_cast<UnionDef*>(node)) {
            emitUnion(unionDef);
        } else if (auto varDecl = dynamic_cast<VarDecl*>(node)) {
            emitGlobalVar(varDecl);
        } else if (auto ns = dynamic_cast<NamespaceDecl*>(node)) {
            for (const auto& item : ns->items) {
                emitNode(item.get());
            }
        }
    }

    std::string emitType(Type* type) {
        if (!type) return "void";

        // Jika memiliki base qualified name
        if (type->base && !type->base->parts.empty()) {
            std::string name = type->base->parts.back();
            // Pemetaan tipe dasar Quelang ke CRU
            if (name == "int" || name == "i32") return "i32";
            if (name == "i8" || name == "char") return "i8"; // Char menjadi i8
            if (name == "i64" || name == "long") return "i64";
            if (name == "f32" || name == "float") return "f32";
            if (name == "f64" || name == "double") return "f64";
            if (name == "bool") return "bool";
            if (name == "string") return "ptr"; // String menjadi ptr
            return name;
        }

        // Cek modifiers untuk Pointer / Reference -> dipetakan ke ptr
        for (const auto& mod : type->modifiers) {
            if (mod.kind == TypeModifier::Kind::Pointer || 
                mod.kind == TypeModifier::Kind::Reference) {
                return "ptr";
            }
        }

        return "ptr";
    }

    void emitFunction(Function* func) {
        std::string retType = emitType(func->return_types.get());
        std::string name = func->name ? func->name->parts.back() : "anonymous";

        // Placeholder untuk coroutine
        if (func->is_coroutine) {
            out_ << "## [PLACEHOLDER] Coroutine function: " << name << "\n";
        }

        out_ << "def " << retType << " " << name << "(";
        
        for (size_t i = 0; i < func->params.size(); ++i) {
            const auto& p = func->params[i];
            out_ << emitType(p.type.get()) << " " << p.name;
            if (i + 1 < func->params.size()) out_ << ", ";
        }
        out_ << ")\n";

        if (func->body) {
            emitStmt(func->body.get());
        } else {
            out_ << "entry:\n    ret 0\n";
        }

        out_ << "end\n\n";
    }

    void emitStruct(StructDef* structDef) {
        std::string name = structDef->name ? structDef->name->parts.back() : "AnonymousStruct";
        out_ << "struct " << name << ":\n";
        
        for (const auto& member : structDef->members) {
            if (auto var = dynamic_cast<VarDecl*>(member.get())) {
                for (const auto& item : var->items) {
                    out_ << "    " << item.name << ": " << emitType(var->type.get()) << "\n";
                }
            }
        }
        out_ << "end\n\n";
    }

    void emitUnion(UnionDef* unionDef) {
        std::string name = unionDef->name ? unionDef->name->parts.back() : "AnonymousUnion";
        out_ << "union " << name << ":\n";
        for (const auto& field : unionDef->fields) {
            out_ << "    " << field.name << ": " << emitType(field.type.get()) << "\n";
        }
        out_ << "end\n\n";
    }

    void emitGlobalVar(VarDecl* varDecl) {
        for (const auto& item : varDecl->items) {
            std::string t = emitType(varDecl->type.get());
            if (varDecl->is_atomic) {
                out_ << "global.mutable " << item.name << ": " << t << "\n";
            } else {
                out_ << "constant " << item.name << ": " << t << "\n";
            }
        }
    }

    void emitStmt(Stmt* stmt) {
        if (!stmt) return;

        switch (stmt->kind) {
            case Stmt::Kind::Block:
                out_ << "entry:\n";
                for (const auto& s : stmt->block.stmts) {
                    emitStmt(s.get());
                }
                break;

            case Stmt::Kind::Return:
                if (stmt->return_values) {
                    // Sederhanakan ekspresi return
                    out_ << "    ret ";
                    emitExpr(stmt->return_values.get());
                    out_ << "\n";
                } else {
                    out_ << "    ret\n";
                }
                break;

            case Stmt::Kind::ExprStmt:
                if (stmt->expr_stmt) {
                    out_ << "    ";
                    emitExpr(stmt->expr_stmt.get());
                    out_ << "\n";
                }
                break;

            case Stmt::Kind::Yield:
                out_ << "    ## [PLACEHOLDER] yield statement\n";
                break;

            case Stmt::Kind::Resume:
                out_ << "    ## [PLACEHOLDER] resume statement\n";
                break;

            default:
                out_ << "    ## [PLACEHOLDER] unhandled statement\n";
                break;
        }
    }

    void emitExpr(Expr* expr) {
        if (!expr) return;

        if (expr->kind == Expr::Kind::Literal) {
            if (expr->literal) {
                if (expr->literal->kind == Literal::Kind::Char) {
                    out_ << "'" << expr->literal->value << "'";
                } else if (expr->literal->kind == Literal::Kind::String) {
                    out_ << "\"" << expr->literal->value << "\"";
                } else {
                    out_ << expr->literal->value;
                }
            }
        } else if (expr->kind == Expr::Kind::Ident) {
            if (expr->ident && !expr->ident->parts.empty()) {
                out_ << expr->ident->parts.back();
            }
        } else {
            out_ << "/* expr placeholder */";
        }
    }
};

// Fungsi antarmuka utama sesuai permintaan
std::string codegen(FileProgram* program) {
    QuelangToCruCodegen generator;
    return generator.generate(program);
}
