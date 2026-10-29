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
        currentNamespace_.clear();

        for (const auto& node : program->decls) {
            emitNode(node.get());
        }

        return out_.str();
    }

private:
    std::unordered_map<std::string, std::string> symbolTypes_;
    std::ostringstream out_;
    int tempCounter_ = 0;
    std::vector<std::string> currentNamespace_;

    std::string freshTemp(bool s = false) {
        if(s) return "t" + std::to_string(tempCounter_++);
        return "%t" + std::to_string(tempCounter_++);
    }

    std::string qualifyName(QualifiedName* qname, const std::string& fallback = "anonymous") {
        std::vector<std::string> parts = currentNamespace_;
        if (qname && !qname->parts.empty()) {
            parts.insert(parts.end(), qname->parts.begin(), qname->parts.end());
        }

        if (parts.empty()) {
            return fallback;
        }

        std::string result = parts[0];
        for (std::size_t i = 1; i < parts.size(); ++i) {
            result += "_" + parts[i];
        }
        return result;
    }

    void emitNamespace(NamespaceDecl* ns) {
        if (!ns) return;

        // 1. Masukkan bagian nama namespace ke dalam stack konteks aktif
        std::vector<std::string> pushedParts;
        if (ns->name) {
            pushedParts = ns->name->parts;
            currentNamespace_.insert(currentNamespace_.end(), pushedParts.begin(), pushedParts.end());
        }

        // 2. Iterasi seluruh item di dalam namespace
        for (const auto& item : ns->items) {
            emitNode(item.get());
        }

        // 3. Keluar dari konteks namespace (pop stack)
        if (!pushedParts.empty()) {
            currentNamespace_.erase(
                currentNamespace_.end() - pushedParts.size(), 
                currentNamespace_.end()
            );
        }
    }


    void emitNode(Node* node) {
        if (!node) return;

        if (auto func = dynamic_cast<Function*>(node)) {
            emitFunction(func);
        } else if (auto forwardDecl = dynamic_cast<ForwardDecl*>(node)) {
            // Menangani deklarasi extern / forward declaration
            if (forwardDecl->is_extern) {
                emitExtern(forwardDecl);
            }
        } else if (auto structDef = dynamic_cast<StructDef*>(node)) {
            emitStruct(structDef);
        } else if (auto unionDef = dynamic_cast<UnionDef*>(node)) {
            emitUnion(unionDef);
        } else if (auto varDecl = dynamic_cast<VarDecl*>(node)) {
            emitGlobalVar(varDecl);
        } else if (auto ns = dynamic_cast<NamespaceDecl*>(node)) {
            emitNamespace(ns);
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

    bool isFloatType(Type* type) {
        if (!type || !type->base || type->base->parts.empty()) return false;
        std::string name = type->base->parts.back();
        return name == "f32" || name == "float" || name == "f64" || name == "double";
    }

    void emitExtern(ForwardDecl* decl) {
        std::string name = decl->name ? decl->name->parts.back() : "unknown";
        std::string retType = emitType(decl->return_types.get());
        
        out_ << "extern def " << retType << " " << name << "(";
        for (size_t i = 0; i < decl->params.size(); ++i) {
            out_ << emitType(decl->params[i].get());
            if (i + 1 < decl->params.size()) out_ << ", ";
        }
        out_ << ")\n";
    }


    void emitFunction(Function* func) {
        std::string retType = emitType(func->return_types.get());
        std::string name = qualifyName(func->name.get(), "AnonymousFunction");
        tempCounter_ = 0;

        if (func->is_coroutine) {
            out_ << "## [COROUTINE PLACEHOLDER] Function: " << name << "\n";
        }

        out_ << "def " << retType << " " << name << "(";
        
        for (size_t i = 0; i < func->params.size(); ++i) {
            const auto& p = func->params[i];
            std::string pType = emitType(p.type.get());
            symbolTypes_[p.name] = pType;
            out_ << pType  << " " + p.name;
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
        std::string name = qualifyName(structDef->name.get(), "AnonymousStruct");
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
        std::string name = qualifyName(unionDef->name.get(), "AnonymousUnion");
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

    std::string flattenQualifiedName(QualifiedName* qname, const std::string& fallback = "anonymous") {
        if (!qname || qname->parts.empty()) {
            return fallback;
        }
    
        std::string result = qname->parts[0];
        for (std::size_t i = 1; i < qname->parts.size(); ++i) {
            result += "_" + qname->parts[i];
        }
        return result;
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
                    symbolTypes_[item.name] = t;
                    std::string reg = "%" + item.name;
                    if (item.is_array && !item.array_dims.empty()) {
                        // Alokasi array lokal menggunakan ukuran dimensi pertama
                        std::string sizeStr = item.array_dims[0]->value;
                        out_ << "    " << reg << " = alloca [" << sizeStr << "]" << t << "\n";
                    } else {
                        out_ << "    " << reg << " = alloca." << t << "\n";
                        if (item.init) {
                            std::string valRes = emitExpr(item.init.get());
                            out_ << "    store." << t << " " << valRes << ", " << reg << "\n";
                        }
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
                std::string thenLabel = freshTemp(true) + "_then";
                std::string elseLabel = freshTemp(true) + "_else";
                std::string mergeLabel = freshTemp(true) + "_merge";

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

            case Stmt::Kind::SwitchCase: {
                // 1. Evaluasi ekspresi utama yang di-switch
                std::string condVal = emitExpr(stmt->switch_stmt.expr.get());
                std::string switchEndLabel = freshTemp(true) + "_switch_end";

                // Simpan label untuk setiap case agar bisa dilompati
                std::vector<std::string> caseLabels;
                for (size_t i = 0; i < stmt->switch_stmt.cases.size(); ++i) {
                    caseLabels.push_back(freshTemp(true) + "_case_" + std::to_string(i));
                }
                std::string defaultLabel = freshTemp(true) + "_default";
                std::string targetDefault = defaultLabel;

                // Cari apakah ada case default
                for (size_t i = 0; i < stmt->switch_stmt.cases.size(); ++i) {
                    if (stmt->switch_stmt.cases[i].is_default) {
                        targetDefault = caseLabels[i];
                        break;
                    }
                }

                // 2. Emit perbandingan untuk setiap case (pencocokan nilai)
                for (size_t i = 0; i < stmt->switch_stmt.cases.size(); ++i) {
                    const auto& c = stmt->switch_stmt.cases[i];
                    if (c.is_default) continue;

                    for (const auto& valLit : c.values) {
                        std::string cmpRes = freshTemp();
                        std::string litVal = valLit->value; // Nilai literal pembanding
                        
                        // Lakukan perbandingan kesamaan (eq)
                        out_ << "    " << cmpRes << " = eq.i32 " << condVal << ", " << litVal << "\n";
                        
                        // Tentukan target lompatan berikutnya (jika cocok ke case[i], jika tidak lanjut ke cek berikutnya/default)
                        std::string nextCheck = (i + 1 < stmt->switch_stmt.cases.size()) ? caseLabels[i + 1] : targetDefault;
                        out_ << "    br_if " << cmpRes << ", " << caseLabels[i] << ", " << nextCheck << "\n";
                    }
                }

                // Jika tidak ada yang cocok sama sekali, langsung lompat ke default/end
                out_ << "    jmp " << targetDefault << "\n";

                // 3. Emit blok tubuh (*body*) untuk masing-masing case
                for (size_t i = 0; i < stmt->switch_stmt.cases.size(); ++i) {
                    const auto& c = stmt->switch_stmt.cases[i];
                    out_ << caseLabels[i] << ":\n";
                    if (c.body) {
                        emitStmt(c.body.get());
                    }
                    out_ << "    jmp " << switchEndLabel << "\n";
                }

                // Emit blok default jika didefinisikan secara terpisah
                out_ << defaultLabel << ":\n";
                // (Badan default bisa ditaruh di sini jika ditangani khusus)
                out_ << "    jmp " << switchEndLabel << "\n";

                // Label akhir switch
                out_ << switchEndLabel << ":\n";
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
        std::string varName = flattenQualifiedName(expr->ident.get());

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
                    return "%" + varName;
                }
                break;


            case Expr::Kind::StructInit: {
                // 1. Alokasikan memori struct di stack (menggunakan nama struct atau generic ptr)
                std::string structName = "AnonymousStruct";
                if (symbolTypes_.find(varName) != symbolTypes_.end()) {
                    structName = symbolTypes_[varName];
                }

                std::string allocRes = freshTemp();
                out_ << "    " << allocRes << " = alloca.ptr\n"; // Alokasi pointer penampung struct

                // 2. Iterasi dan isi setiap field menggunakan store atau field.addr
                int fieldIdx = 0;
                for (const auto& fieldPair : expr->struct_init.fields) {
                    std::string valRes = emitExpr(fieldPair.second.get());
                    std::string fieldAddrRes = freshTemp();
                    
                    // Mendapatkan alamat field menggunakan field.addr CRU
                    out_ << "    " << fieldAddrRes << " = field.addr " << structName << ", " << allocRes << ", " << fieldIdx << "\n";
                    out_ << "    store.i32 " << valRes << ", " << fieldAddrRes << "\n"; // Sesuaikan tipe jika ada penelusuran tipe field
                    
                    fieldIdx++;
                }
                return allocRes;
            }

            case Expr::Kind::Binary: {
                std::string lhs = emitExpr(expr->binary.lhs.get());
                std::string rhs = emitExpr(expr->binary.rhs.get());
                std::string res = freshTemp();
                std::string opStr = "add";
                bool isFloatOp = false;
                std::string varType = "i32";
                if (symbolTypes_.find(varName) != symbolTypes_.end()) {
                    varType = symbolTypes_[varName]; // Ambil tipe asli yang tersimpan
                }

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
                if (isFloatOp) {
                    opStr = "f" + opStr;
                }

                out_ << "    " << res << " = " << opStr << "." + varType + " " << lhs << ", " << rhs << "\n";
                return res;
            }

            case Expr::Kind::Assign: {
                std::string rhsVal = emitExpr(expr->assign.rhs.get());
                if (expr->assign.lhs->kind == Expr::Kind::Ident && expr->assign.lhs->ident) {
                    std::string varType = "i32";
                    if (symbolTypes_.find(varName) != symbolTypes_.end()) {
                        varType = symbolTypes_[varName]; // Ambil tipe asli yang tersimpan
                    }
                    std::string varName = "%" + expr->assign.lhs->ident->parts.back();
                    out_ << "    store." + varType + " " << rhsVal << ", " << varName << "\n";
                }
                return rhsVal;
            }
            case Expr::Kind::Cast: {
                // 1. Evaluasi ekspresi basis yang akan di-cast
                std::string operandVal = emitExpr(expr->cast.base.get());
                std::string res = freshTemp();

                // 2. Tentukan tipe target konversi (misal: i32, i64, ptr, dll.)
                std::string targetType = "i32"; // Fallback default
                if (expr->cast.target) {
                    targetType = emitType(expr->cast.target.get());
                }

                // 3. Emit instruksi cast sesuai format IR CRU
                // Format CRU cast: %result = cast operand to TargetType (atau instruksi cast khusus)
                out_ << "    " << res << " = cast " << operandVal << " to " << targetType << "\n";
                
                return res;
            }

            case Expr::Kind::Unary: {
                std::string operandVal = emitExpr(expr->unary.expr.get());
                std::string res = freshTemp();
                
                switch (expr->unary.op) {
                    case UnaryOp::Ref: {
                        // Operator referensi (&): Mengambil alamat dari identifier
                        if (expr->unary.expr->kind == Expr::Kind::Ident && 
                            expr->unary.expr->ident && 
                            !expr->unary.expr->ident->parts.empty()) {
                            return "%" + expr->unary.expr->ident->parts.back();
                        }
                        return operandVal;
                    }

                    case UnaryOp::Deref: {
                        // Operator dereferensi (*): Melakukan load dari alamat pointer
                        std::string varType = "i32"; // Default fallback tipe data
                        if (expr->unary.expr->kind == Expr::Kind::Ident && 
                            expr->unary.expr->ident && 
                            !expr->unary.expr->ident->parts.empty()) {
                            std::string idName = expr->unary.expr->ident->parts.back();
                            if (symbolTypes_.find(idName) != symbolTypes_.end()) {
                                varType = symbolTypes_[idName];
                            }
                        }

                        out_ << "    " << res << " = load." << varType << " " << operandVal << "\n";
                        return res;
                    }

                    case UnaryOp::Neg: {
                        out_ << "    " << res << " = sub.i32 0, " << operandVal << "\n";
                        return res;
                    }

                    case UnaryOp::Not: {
                        out_ << "    " << res << " = xor.bool " << operandVal << ", true\n";
                        return res;
                    }

                    default:
                        break;
                }
                return operandVal;
            }


            case Expr::Kind::Postfix: {
                std::string baseName = "";
                if (expr->postfix.base && expr->postfix.base->kind == Expr::Kind::Ident) {
                    if (expr->postfix.base->ident && !expr->postfix.base->ident->parts.empty()) {
                        baseName = expr->postfix.base->ident->parts.back();
                    }
                }

                std::string currentReg = "%" + baseName;

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
                    } else if (op.kind == Expr::PostfixOp::Kind::Index) {
                        // Dukungan akses elemen array menggunakan element.addr CRU
                        std::string indexVal = emitExpr(op.index.get());
                        std::string elemAddrRes = freshTemp();
                        std::string varType = "i32";
                        if (symbolTypes_.find(varName) != symbolTypes_.end()) {
                        varType = symbolTypes_[varName]; // Ambil tipe asli yang tersimpan
                        }
                        out_ << "    " << elemAddrRes << " = elem.addr." + varType + " %" << baseName << ", " << indexVal << "\n";
                        
                        std::string loadRes = freshTemp();
                        out_ << "    " << loadRes << " = load." + varType + " " << elemAddrRes << "\n";
                        return loadRes;
                    } else if (op.kind == Expr::PostfixOp::Kind::MemberAccess) {
                        // Dukungan akses field struct menggunakan field.addr CRU
                        std::string fieldAddrRes = freshTemp();
                        // Catatan: Indeks field diasumsikan berbasis posisi atau dicari dari metadata struct. 
                        // Untuk contoh dasar, kita gunakan indeks 0 atau parse nama field.
                        int fieldIndex = 0; 
                        std::string structTypeName = "AnonymousStruct";
                        if (symbolTypes_.find(baseName) != symbolTypes_.end()) {
                            structTypeName = symbolTypes_[baseName];
                        }


                        out_ << "    " << fieldAddrRes << " = field.addr " << structTypeName << ", " << currentReg << ", " << fieldIndex << "\n";
                        
                        std::string loadRes = freshTemp();
                        out_ << "    " << loadRes << " = load.i32 " << fieldAddrRes << "\n";
                        currentReg = loadRes;
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
