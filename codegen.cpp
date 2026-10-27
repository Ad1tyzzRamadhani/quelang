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
        tempCounter_ = 0;

        for (const auto& node : program->decls) {
            emitNode(node.get());
        }

        return out_.str();
    }

private:
    std::ostringstream out_;
    int tempCounter_ = 0;

    std::string freshTemp() {
        return "%t" + std::to_string(tempCounter_++);
    }

    void emitNode(Node* node) {
        if (!node) return;

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

        if (type->base && !type->base->parts.empty() && type->modifiers.empty()) {
            std::string name = type->base->parts.back();
            if (name == "int" || name == "i32") return "i32";
            if (name == "i8" || name == "char") return "i8"; 
            if (name == "i64" || name == "long") return "i64";
            if (name == "f32" || name == "float") return "f32";
            if (name == "f64" || name == "double") return "f64";
            if (name == "bool") return "bool";
            return name;
        }

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
        tempCounter_ = 0;

        if (func->is_coroutine) {
            out_ << "## [COROUTINE PLACEHOLDER] Function: " << name << "\n";
        }

        out_ << "def " << retType << " " << name << "(";
        
        for (size_t i = 0; i < func->params.size(); ++i) {
            const auto& p = func->params[i];
            out_ << emitType(p.type.get())  << " " + p.name;
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

            case Stmt::Kind::VarDecl: {
                for (const auto& item : stmt->var_decl.items) {
                    std::string t = emitType(stmt->var_decl.type.get());
                    std::string reg = "%" + item.name;
                    out_ << "    " << reg << " = alloca." << t << "\n";
                    if (item.init) {
                        std::string valRes = emitExpr(item.init.get());
                        out_ << "    store." << t << " " << valRes << ", " << reg << "\n";
                    }
                }
                break;
            }

            case Stmt::Kind::Return:
                if (stmt->return_values) {
                    std::string val = emitExpr(stmt->return_values.get());
                    out_ << "    ret " << val << "\n";
                } else {
                    out_ << "    ret\n";
                }
                break;

            case Stmt::Kind::ExprStmt:
                if (stmt->expr_stmt) {
                    emitExpr(stmt->expr_stmt.get());
                }
                break;

            case Stmt::Kind::If: {
                std::string condVal = emitExpr(stmt->if_stmt.cond.get());
                std::string thenLabel = freshTemp() + "_then";
                std::string elseLabel = freshTemp() + "_else";
                std::string mergeLabel = freshTemp() + "_merge";

                out_ << "    br_if " << condVal << ", " << thenLabel << ", " << elseLabel << "\n";
                out_ << thenLabel << ":\n";
                emitStmt(stmt->if_stmt.then_block.get());
                out_ << "    jmp " << mergeLabel << "\n";

                out_ << elseLabel << ":\n";
                if (stmt->if_stmt.else_block) {
                    emitStmt(stmt->if_stmt.else_block.get());
                }
                out_ << "    jmp " << mergeLabel << "\n";
                out_ << mergeLabel << ":\n";
                break;
            }

            case Stmt::Kind::While: {
                std::string condLabel = freshTemp() + "_while_cond";
                std::string bodyLabel = freshTemp() + "_while_body";
                std::string endLabel = freshTemp() + "_while_end";

                out_ << "    jmp " << condLabel << "\n";
                out_ << condLabel << ":\n";
                std::string condVal = emitExpr(stmt->while_stmt.cond.get());
                out_ << "    br_if " << condVal << ", " << bodyLabel << ", " << endLabel << "\n";

                out_ << bodyLabel << ":\n";
                emitStmt(stmt->while_stmt.body.get());
                out_ << "    jmp " << condLabel << "\n";

                out_ << endLabel << ":\n";
                break;
            }

            default:
                out_ << "    ## [PLACEHOLDER] Unhandled statement kind\n";
                break;
        }
    }

    std::string emitExpr(Expr* expr) {
        if (!expr) return "0";

        switch (expr->kind) {
            case Expr::Kind::Literal:
                if (expr->literal) {
                    if (expr->literal->kind == Literal::Kind::Char) {
                        return "'" + expr->literal->value + "'";
                    } else if (expr->literal->kind == Literal::Kind::String) {
                        return "\"" + expr->literal->value + "\"";
                    } else if (expr->literal->kind == Literal::Kind::True) {
                        return "true";
                    } else if (expr->literal->kind == Literal::Kind::False) {
                        return "false";
                    } else {
                        return expr->literal->value;
                    }
                }
                break;

            case Expr::Kind::Ident:
                if (expr->ident && !expr->ident->parts.empty()) {
                    std::string varName = expr->ident->parts.back();
                    std::string loadRes = freshTemp();
                    out_ << "    " << loadRes << " = load.i32 %" << varName << "\n";
                    return loadRes;
                }
                break;

            case Expr::Kind::Binary: {
                std::string lhs = emitExpr(expr->binary.lhs.get());
                std::string rhs = emitExpr(expr->binary.rhs.get());
                std::string res = freshTemp();
                std::string opStr = "add";

                switch (expr->binary.op) {
                    case BinaryOp::Add: opStr = "add"; break;
                    case BinaryOp::Sub: opStr = "sub"; break;
                    case BinaryOp::Mul: opStr = "mul"; break;
                    case BinaryOp::Div: opStr = "div"; break;
                    case BinaryOp::Mod: opStr = "rem"; break;
                    case BinaryOp::Eq:  opStr = "eq"; break;
                    case BinaryOp::Ne:  opStr = "ne"; break;
                    case BinaryOp::Lt:  opStr = "lt"; break;
                    case BinaryOp::Lte: opStr = "le"; break;
                    case BinaryOp::Gt:  opStr = "gt"; break;
                    case BinaryOp::Gte: opStr = "ge"; break;
                    default: opStr = "add"; break;
                }

                out_ << "    " << res << " = " << opStr << ".i32 " << lhs << ", " << rhs << "\n";
                return res;
            }

            case Expr::Kind::Assign: {
                std::string rhsVal = emitExpr(expr->assign.rhs.get());
                if (expr->assign.lhs->kind == Expr::Kind::Ident && expr->assign.lhs->ident) {
                    std::string varName = "%" + expr->assign.lhs->ident->parts.back();
                    out_ << "    store.i32 " << rhsVal << ", " << varName << "\n";
                }
                return rhsVal;
            }

            case Expr::Kind::Postfix: {
                std::string baseName = "";
                if (expr->postfix.base && expr->postfix.base->kind == Expr::Kind::Ident) {
                    if (expr->postfix.base->ident && !expr->postfix.base->ident->parts.empty()) {
                        baseName = expr->postfix.base->ident->parts.back();
                    }
                }

                for (const auto& op : expr->postfix.ops) {
                    if (op.kind == Expr::PostfixOp::Kind::Call) {
                        std::vector<std::string> argVals;
                        for (const auto& arg : op.args) {
                            argVals.push_back(emitExpr(arg.get()));
                        }

                        std::string callRes = freshTemp();
                        out_ << "    " << callRes << " = call.i32 " << baseName << "(";
                        for (size_t i = 0; i < argVals.size(); ++i) {
                            out_ << argVals[i];
                            if (i + 1 < argVals.size()) out_ << ", ";
                        }
                        out_ << ")\n";
                        return callRes;
                    }
                }
                break;
            }

            default:
                break;
        }

        return "0";
    }
};

std::string codegen(FileProgram* program) {
    QuelangToCruCodegen generator;
    return generator.generate(program);
}
