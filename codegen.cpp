#include <sstream>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <stdexcept>

class QuelangToCruCodegen {
public:
    std::string generate(FileProgram* program) {
        out_.str("");
        out_.clear();
        tempCounter_ = 0;
        symbolTypes_.clear();
        paramTypes_.clear();
        functionReturnTypes_.clear();
        structDefinitions_.clear();

        for (const auto& node : program->decls) {
            emitNode(node.get());
        }

        return out_.str();
    }

private:
    std::unordered_map<std::string, std::string> symbolTypes_;
    std::unordered_map<std::string, std::string> paramTypes_;
    std::unordered_map<std::string, std::string> globalTypes_;
    std::unordered_map<std::string, std::string> functionReturnTypes_; // Melacak tipe return fungsi secara dinamis
    std::vector<std::string> loopCondLabels_;
    std::vector<std::string> loopEndLabels_;
    std::ostringstream out_;
    int tempCounter_ = 0;

    struct FieldInfo {
        std::string name;
        std::string type;
        int index;
    };

    struct StructInfo {
        std::string name;
        std::vector<FieldInfo> fields;
    };

    std::unordered_map<std::string, StructInfo> structDefinitions_;

    std::string freshTemp(bool s = false) {
        if(s) return "t" + std::to_string(tempCounter_++);
        return "%t" + std::to_string(tempCounter_++);
    }

    std::string qualifyName(QualifiedName* qname, const std::string& fallback = "anonymous") {
        if (!qname || qname->parts.empty()) {
            return fallback;
        }

        std::string result = qname->parts[0];
        for (std::size_t i = 1; i < qname->parts.size(); ++i) {
            result += "_" + qname->parts[i];
        }
        return result;
    }

    bool endsWithReturn(Stmt* stmt) {
        if (!stmt) return false;
        if (stmt->kind == Stmt::Kind::Return ||
            stmt->kind == Stmt::Kind::Break ||
            stmt->kind == Stmt::Kind::Continue ||
            stmt->kind == Stmt::Kind::Jump) {
            return true;
        }
        if (stmt->kind == Stmt::Kind::Block) {
            if (!stmt->block.stmts.empty()) {
                return endsWithReturn(stmt->block.stmts.back().get());
            }
        }
        return false;
    }

    void emitNode(Node* node) {
        if (!node) return;

        if (auto func = dynamic_cast<Function*>(node)) {
            emitFunction(func);
        } else if (auto forwardDecl = dynamic_cast<ForwardDecl*>(node)) {
            if (forwardDecl->is_extern) {
                emitExtern(forwardDecl);
            }
        } else if (auto structDef = dynamic_cast<StructDef*>(node)) {
            emitStruct(structDef);
        } else if (auto unionDef = dynamic_cast<UnionDef*>(node)) {
            emitUnion(unionDef);
        } else if (auto varDecl = dynamic_cast<VarDecl*>(node)) {
            emitGlobalVar(varDecl);
        } else if (auto enumDef = dynamic_cast<EnumDef*>(node)) {
            emitEnum(enumDef);
        }
    }

    void emitEnum(EnumDef* enumDef) {
        std::string name = qualifyName(enumDef->name.get(), "AnonymousEnum");
        int currentValue = 0;
        for (const auto& item : enumDef->items) {
            if (item.value && item.value->kind == Expr::Kind::Literal && item.value->literal) {
                currentValue = std::stoi(item.value->literal->value);
            }
            out_ << "constant " << name << "_" << item.name << ": i32 = " << currentValue << "\n";
            currentValue++;
        }
        out_ << "\n";
    }

    std::string getExprType(Expr* expr) {
        if (!expr) return "i32";
        
        switch (expr->kind) {
            case Expr::Kind::Literal:
                if (expr->literal) {
                    if (expr->literal->kind == Literal::Kind::Char) return "i8";
                    if (expr->literal->kind == Literal::Kind::String) return "ptr";
                    if (expr->literal->kind == Literal::Kind::Float) return "f32";
                    if (expr->literal->kind == Literal::Kind::True || 
                        expr->literal->kind == Literal::Kind::False) return "bool";
                    return "i32";
                }
                break;
            case Expr::Kind::Ident:
                if (expr->ident && !expr->ident->parts.empty()) {
                    std::string varName = expr->ident->parts.back();
                    if (symbolTypes_.find(varName) != symbolTypes_.end()) {
                        return symbolTypes_[varName];
                    }
                    if (paramTypes_.find(varName) != paramTypes_.end()) {
                        return paramTypes_[varName];
                    }
                }
                break;
            case Expr::Kind::Postfix: {
                std::string baseName = "";
                if (expr->postfix.base && expr->postfix.base->kind == Expr::Kind::Ident) {
                    if (expr->postfix.base->ident && !expr->postfix.base->ident->parts.empty()) {
                        baseName = expr->postfix.base->ident->parts.back();
                    }
                }
                std::string currentType = "i32";
                if (symbolTypes_.find(baseName) != symbolTypes_.end()) {
                    currentType = symbolTypes_[baseName];
                } else if (paramTypes_.find(baseName) != paramTypes_.end()) {
                    currentType = paramTypes_[baseName];
                }

                for (const auto& op : expr->postfix.ops) {
                    if (op.kind == Expr::PostfixOp::Kind::Index) {
                        std::string elemType = "i32";
                        if (currentType.rfind("[", 0) == 0) {
                            size_t bracketPos = currentType.find("]");
                            if (bracketPos != std::string::npos) {
                                elemType = currentType.substr(bracketPos + 1);
                            }
                        }
                        currentType = elemType;
                    } else if (op.kind == Expr::PostfixOp::Kind::MemberAccess || 
                               op.kind == Expr::PostfixOp::Kind::Arrow || 
                               op.kind == Expr::PostfixOp::Kind::SafeArrow) {
                        std::string fieldType = "i32";
                        if (structDefinitions_.find(currentType) != structDefinitions_.end()) {
                            const auto& sInfo = structDefinitions_[currentType];
                            for (const auto& f : sInfo.fields) {
                                if (f.name == op.name) {
                                    fieldType = f.type;
                                    break;
                                }
                            }
                        }
                        currentType = fieldType;
                    }
                }
                return currentType;
            }
            case Expr::Kind::Binary:
                return getExprType(expr->binary.lhs.get());
            case Expr::Kind::Unary:
                if (expr->unary.op == UnaryOp::Not) return "bool";
                return getExprType(expr->unary.expr.get());
            case Expr::Kind::Ternary:
                return getExprType(expr->ternary.then_expr.get());
            case Expr::Kind::Cast:
                if (expr->cast.target) {
                    return emitType(expr->cast.target.get());
                }
                break;
            default:
                break;
        }
        return "i32";
    }


    std::string emitType(Type* type) {
        if (!type) return "void";

        if (type->is_array) {
            if (type->array_dims.empty()) {
                return "ptr";
            }
            
            std::string sizeStr = type->array_dims[0]->value;
            std::string elemType = "i32";
            
            if (type->base && !type->base->parts.empty()) {
                std::string name = type->base->parts.back();
                if (name == "int") elemType = "i32";
                else if (name == "char") elemType = "i8";
                else if (name == "short") elemType = "i16";
                else if (name == "long") elemType = "i64";
                else if (name == "unchar") elemType = "u8";
                else if (name == "unshort") elemType = "u16";
                else if (name == "unint") elemType = "u32";
                else if (name == "unlong") elemType = "u64";
                else if (name == "isize") elemType = "isize";
                else if (name == "usize") elemType = "usize";
                else if (name == "float") elemType = "f32";
                else if (name == "flong") elemType = "f64";
                else if (name == "bool") elemType = "bool";
                else elemType = name;
            }
            return "[" + sizeStr + "]" + elemType;
        }

        for (const auto& mod : type->modifiers) {
            if (mod.kind == TypeModifier::Kind::Pointer || 
                mod.kind == TypeModifier::Kind::Reference ||
                mod.kind == TypeModifier::Kind::FuncPtr) {
                return "ptr";
            }
        }

        if (type->base && !type->base->parts.empty() && type->modifiers.empty()) {
            std::string name = type->base->parts.back();
            
            if (name == "int") return "i32";
            if (name == "char") return "i8";
            if (name == "short") return "i16";
            if (name == "long") return "i64";
            if (name == "isize") return "isize";
            if (name == "unchar") return "u8";
            if (name == "unshort") return "u16";
            if (name == "unint") return "u32";
            if (name == "unlong") return "u64";
            if (name == "usize") return "usize";
            if (name == "float") return "f32";
            if (name == "flong") return "f64";
            if (name == "bool") return "bool";
            
            return name;
        }

        return "ptr";
    }

    void emitExtern(ForwardDecl* decl) {
        std::string name = decl->name ? decl->name->parts.back() : "unknown";
        std::string retType = emitType(decl->return_types.get());
        functionReturnTypes_[name] = retType; // Daftarkan return type extern
        
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
        
        // Simpan return type fungsi untuk keperluan pemanggilan dinamis (call)
        functionReturnTypes_[name] = retType;
        if (func->name && !func->name->parts.empty()) {
            functionReturnTypes_[func->name->parts.back()] = retType;
        }

        tempCounter_ = 0;
        paramTypes_.clear();

        out_ << "def " << retType << " @" << name << "(";
        
        for (size_t i = 0; i < func->params.size(); ++i) {
            const auto& p = func->params[i];
            std::string pType = emitType(p.type.get());
            paramTypes_[p.name] = pType;
            out_ << pType  << " " + p.name;
            if (i + 1 < func->params.size()) out_ << ", ";
        }
        out_ << ")\n";
        out_ << "entry:\n";

        if (func->body) {
            emitStmt(func->body.get());
            if (!endsWithReturn(func->body.get())) {
                if (retType == "void") {
                    out_ << "    ret void\n";
                } else {
                    out_ << "    ret 0\n";
                }
            }
        } else {
            if (retType == "void") {
                out_ << "    ret void\n";
            } else {
                out_ << "    ret 0\n";
            }
        }

        out_ << "end\n\n";
    }

    void emitStruct(StructDef* structDef) {
        std::string name = qualifyName(structDef->name.get(), "AnonymousStruct");
        out_ << "struct " << name << ":\n";
        StructInfo info;
        info.name = name;
        int index = 0;
        
        for (const auto& member : structDef->members) {
            if (auto var = dynamic_cast<VarDecl*>(member.get())) {
                std::string fieldType = emitType(var->type.get());
                for (const auto& item : var->items) {
                    out_ << "    " << item.name << ": " << fieldType << "\n";
                    info.fields.push_back({item.name, fieldType, index++});
                }
            }
        }
        out_ << "end\n\n";
        structDefinitions_[name] = info;
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
            bool is_const = false;
            for(auto& kind : varDecl->type->qualifiers) {
                if(kind == TypeQualifier::Const) is_const = true;
            }
            std::string t = emitType(varDecl->type.get());
            globalTypes_[item.name] = t;
            if (varDecl->is_atomic) {
                out_ << "global " << item.name << ": " << t << "\n";
            } else if(is_const) {
                out_ << "constant " << item.name << ": " << t << "\n";
            } else {
                out_ << "global " << item.name << ": " << t << "\n";
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
                for (const auto& s : stmt->block.stmts) {
                    emitStmt(s.get());
                }
                break;

            case Stmt::Kind::VarDecl: {
                for (const auto& item : stmt->var_decl.items) {
                    std::string t = emitType(stmt->var_decl.type.get());
                    symbolTypes_[item.name] = t;
                    std::string reg = "%" + item.name;
                    
                    if (stmt->var_decl.type->is_array && !stmt->var_decl.type->array_dims.empty()) {
                        std::string sizeStr = stmt->var_decl.type->array_dims[0]->value;
                        int totalSize = std::stoi(sizeStr);
                        out_ << "    " << reg << " = alloca " << t << "\n";
                        
                        // String-to-Array Memory Compatibility Handling:
                        // Jika array diinisialisasi dengan string literal, salin karakter per karakter ke memori array.
                        if (item.init && item.init->kind == Expr::Kind::Literal && 
                            item.init->literal->kind == Literal::Kind::String) {
                            std::string strVal = item.init->literal->value;
                            std::string elemType = "i32";
                            if (stmt->var_decl.type->base && !stmt->var_decl.type->base->parts.empty()) {
                                std::string bName = stmt->var_decl.type->base->parts.back();
                                if (bName == "char") elemType = "i8";
                                else if (bName == "int") elemType = "i32";
                            }
                            if (t.rfind("[", 0) == 0) {
                                size_t bracketPos = t.find("]");
                                if (bracketPos != std::string::npos) {
                                    elemType = t.substr(bracketPos + 1);
                                }
                            }
                            for (size_t i = 0; i < strVal.size() && (int)i < totalSize; ++i) {
                                std::string elemAddr = freshTemp();
                                out_ << "    " << elemAddr << " = elem addr " << elemType << " " << reg << ", " << i << "\n";
                                out_ << "    store " << elemType << " '" << strVal[i] << "', " << elemAddr << "\n";
                            }
                            // Tambahkan null terminator jika masih ada sisa kapasitas array
                            if ((int)strVal.size() < totalSize) {
                                std::string elemAddr = freshTemp();
                                out_ << "    " << elemAddr << " = elem addr " << elemType << " " << reg << ", " << strVal.size() << "\n";
                                out_ << "    store " << elemType << " 0, " << elemAddr << "\n";
                            }
                        }
                    } else {
                        out_ << "    " << reg << " = alloca " << t << "\n";
                        if (item.init) {
                            std::string valRes = emitExpr(item.init.get());
                            out_ << "    store " << t << " " << valRes << ", " << reg << "\n";
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
                // Buat satu basis ID unik di awal untuk seluruh blok if-elif-else ini
                std::string baseId = freshTemp(true);
                std::string mergeLabel = baseId + "_if_merge";
                std::string thenLabel = baseId + "_then";
                std::string elseLabel = baseId + "_else";
                
                // Tentukan target jika kondisi 'if' utama bernilai salah (false)
                std::string firstFalseTarget;
                if (!stmt->if_stmt.elifs.empty()) {
                    firstFalseTarget = baseId + "_elif_cond_0";
                } else if (stmt->if_stmt.else_block) {
                    firstFalseTarget = elseLabel;
                } else {
                    firstFalseTarget = mergeLabel;
                }

                // Pra-hitung label untuk setiap cabang elif
                std::vector<std::string> elifCondLabels;
                std::vector<std::string> elifThenLabels;
                for (size_t i = 0; i < stmt->if_stmt.elifs.size(); ++i) {
                    elifCondLabels.push_back(baseId + "_elif_cond_" + std::to_string(i));
                    elifThenLabels.push_back(baseId + "_elif_then_" + std::to_string(i));
                }

                // Evaluasi ekspresi kondisi utama (aman karena label sudah ditentukan di atas)
                std::string condVal = emitExpr(stmt->if_stmt.cond.get());

                out_ << "    br_if " << condVal << ", " << thenLabel << ", " << firstFalseTarget << "\n";
                
                // Blok 'Then' Utama
                out_ << thenLabel << ":\n";
                emitStmt(stmt->if_stmt.then_block.get());
                if (!endsWithReturn(stmt->if_stmt.then_block.get())) {
                    out_ << "    jmp " << mergeLabel << "\n";
                }

                // Penanganan Cabang 'Elif'
                for (size_t i = 0; i < stmt->if_stmt.elifs.size(); ++i) {
                    out_ << elifCondLabels[i] << ":\n";
                    std::string elifCondVal = emitExpr(stmt->if_stmt.elifs[i].cond.get());
                    
                    std::string nextFalseTarget;
                    if (i + 1 < stmt->if_stmt.elifs.size()) {
                        nextFalseTarget = elifCondLabels[i + 1];
                    } else if (stmt->if_stmt.else_block) {
                        nextFalseTarget = elseLabel;
                    } else {
                        nextFalseTarget = mergeLabel;
                    }

                    out_ << "    br_if " << elifCondVal << ", " << elifThenLabels[i] << ", " << nextFalseTarget << "\n";

                    // Blok 'Elif'
                    out_ << elifThenLabels[i] << ":\n";
                    emitStmt(stmt->if_stmt.elifs[i].block.get());
                    if (!endsWithReturn(stmt->if_stmt.elifs[i].block.get())) {
                        out_ << "    jmp " << mergeLabel << "\n";
                    }
                }

                // Blok 'Else' Opsional
                if (stmt->if_stmt.else_block) {
                    out_ << elseLabel << ":\n";
                    emitStmt(stmt->if_stmt.else_block.get());
                    if (!endsWithReturn(stmt->if_stmt.else_block.get())) {
                        out_ << "    jmp " << mergeLabel << "\n";
                    }
                }

                out_ << mergeLabel << ":\n";
                break;
            }


            case Stmt::Kind::SwitchCase: {
                std::string switchExprVal = emitExpr(stmt->switch_stmt.expr.get());
                std::string switchEndLabel = freshTemp(true) + "_switch_end";
                loopEndLabels_.push_back(switchEndLabel);

                for (size_t i = 0; i < stmt->switch_stmt.cases.size(); ++i) {
                    const auto& c = stmt->switch_stmt.cases[i];
                    std::string caseBodyLabel = freshTemp(true) + "_case_" + std::to_string(i);
                    std::string nextCheckLabel = (i + 1 < stmt->switch_stmt.cases.size()) 
                                                 ? freshTemp(true) + "_case_check_" + std::to_string(i + 1) 
                                                 : switchEndLabel;

                    if (c.is_default) {
                        out_ << "    jmp " << caseBodyLabel << "\n";
                    } else {
                        for (size_t v = 0; v < c.values.size(); ++v) {
                            std::string valStr = "0";
                            if (c.values[v]->kind == Expr::Kind::Literal && c.values[v]->literal) {
                                valStr = c.values[v]->literal->value;
                            }
                            
                            std::string cmpRes = freshTemp();
                            out_ << "    " << cmpRes << " = eq i32 " << switchExprVal << ", " << valStr << "\n";
                            
                            std::string evalNext = (v + 1 < c.values.size()) ? freshTemp(true) + "_val_next" : nextCheckLabel;
                            out_ << "    br_if " << cmpRes << ", " << caseBodyLabel << ", " << evalNext << "\n";
                            
                            if (v + 1 < c.values.size()) {
                                out_ << evalNext << ":\n";
                            }
                        }
                    }

                    out_ << caseBodyLabel << ":\n";
                    if (c.body) {
                        emitStmt(c.body.get());
                    }
                    if (!endsWithReturn(c.body.get())) {
                        out_ << "    jmp " << switchEndLabel << "\n";
                    }

                    if (i + 1 < stmt->switch_stmt.cases.size()) {
                        out_ << nextCheckLabel << ":\n";
                    }
                }

                out_ << switchEndLabel << ":\n";
                loopEndLabels_.pop_back();
                break;
            }

            case Stmt::Kind::Break:
                if (!loopEndLabels_.empty()) {
                    out_ << "    jmp " << loopEndLabels_.back() << "\n";
                }
                break;

            case Stmt::Kind::Continue:
                if (!loopCondLabels_.empty()) {
                    out_ << "    jmp " << loopCondLabels_.back() << "\n";
                }
                break;

            case Stmt::Kind::DoStmt: {
                std::string bodyLabel = freshTemp(true) + "_do_body";
                std::string condLabel = freshTemp(true) + "_do_cond";
                std::string endLabel = freshTemp(true) + "_do_end";

                loopCondLabels_.push_back(condLabel);
                loopEndLabels_.push_back(endLabel);

                out_ << "    jmp " << bodyLabel << "\n";
                out_ << bodyLabel << ":\n";
                if (stmt->do_stmt.body) {
                    emitStmt(stmt->do_stmt.body.get());
                }

                if (!endsWithReturn(stmt->do_stmt.body.get())) {
                    out_ << "    jmp " << condLabel << "\n";
                }

                out_ << condLabel << ":\n";
                if (stmt->do_stmt.cond) {
                    std::string condVal = emitExpr(stmt->do_stmt.cond.get());
                    out_ << "    br_if " << condVal << ", " << bodyLabel << ", " << endLabel << "\n";
                } else {
                    out_ << "    jmp " << bodyLabel << "\n";
                }

                out_ << endLabel << ":\n";

                loopCondLabels_.pop_back();
                loopEndLabels_.pop_back();
                break;
            }

            case Stmt::Kind::While: {
                std::string condLabel = freshTemp(true) + "_while_cond";
                std::string bodyLabel = freshTemp(true) + "_while_body";
                std::string endLabel = freshTemp(true) + "_while_end";

                loopCondLabels_.push_back(condLabel);
                loopEndLabels_.push_back(endLabel);

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
            case Stmt::Kind::Label:
                out_ << stmt->label << ":\n";
                break;

            case Stmt::Kind::Jump:
                out_ << "    jmp " << stmt->jump_target << "\n";
                break;

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
                if (expr->ident) {
                    if (expr->ident->is_absolute) {
                        std::string globalName = flattenQualifiedName(expr->ident.get());
                        std::string addrRes = freshTemp();
                        out_ << "    " << addrRes << " = global addr @" << globalName << "\n";
                        std::string gType = "i32";
                        if (globalTypes_.find(globalName) != globalTypes_.end()) {
                            gType = globalTypes_[globalName];
                        }
                        
                        // Otomatis lakukan load agar yang dikembalikan adalah nilainya, bukan alamatnya
                        std::string loadRes = freshTemp();
                        out_ << "    " << loadRes << " = load " << gType << " " << addrRes << "\n";
                        return loadRes;
                    }

                    if (!expr->ident->parts.empty()) {
                        std::string varName = expr->ident->parts.back();
                        if (paramTypes_.find(varName) != paramTypes_.end()) {
                            return "%" + varName;
                        }
                        if (symbolTypes_.find(varName) != symbolTypes_.end()) {
                            std::string vType = symbolTypes_[varName];
                            if (vType == "ptr" || vType.rfind("[", 0) == 0) {
                                return "%" + varName;
                            }
                            std::string loadRes = freshTemp();
                            out_ << "    " << loadRes << " = load " << vType << " %" << varName << "\n";
                            return loadRes;
                        }
                        return "%" + varName;
                    }
                }
                break;

            case Expr::Kind::StructInit: {
                std::string structName = "AnonymousStruct";
                if (expr->struct_init.type_name && expr->struct_init.type_name->base) {
                    structName = qualifyName(expr->struct_init.type_name->base.get());
                } 

                std::string allocRes = freshTemp();
                out_ << "    " << allocRes << " = alloca ptr\n";

                int fieldIdx = 0;
                for (const auto& fieldPair : expr->struct_init.fields) {
                    std::string valRes = emitExpr(fieldPair.second.get());
                    std::string fieldAddrRes = freshTemp();
                    std::string fieldType = "i32";
                    
                    if (valRes.rfind('"', 0) == 0 && valRes.back() == '"') {
                        fieldType = "ptr";
                    } else if (valRes == "true" || valRes == "false") {
                        fieldType = "bool";
                    }

                    out_ << "    " << fieldAddrRes << " = field addr " << structName << ", " << allocRes << ", " << fieldIdx << "\n";
                    out_ << "    store " << fieldType << " " << valRes << ", " << fieldAddrRes << "\n";
                    fieldIdx++;
                }
                return allocRes;
            }

            case Expr::Kind::ArrayLiteral: {
                std::string allocRes = freshTemp();
                size_t size = expr->array_items.size();
                
                std::string elemType = "i32";
                if (!expr->array_items.empty() && expr->array_items[0]) {
                    elemType = getExprType(expr->array_items[0].get());
                }

                out_ << "    " << allocRes << " = alloca [" << size << "]" << elemType << "\n";
                
                for (size_t i = 0; i < size; ++i) {
                    std::string valRes = emitExpr(expr->array_items[i].get());
                    std::string elemAddrRes = freshTemp();
                    
                    out_ << "    " << elemAddrRes << " = elem addr " << elemType << " " << allocRes << ", " << i << "\n";
                    out_ << "    store " << elemType << " " << valRes << ", " << elemAddrRes << "\n";
                }
                return allocRes;
            }

            case Expr::Kind::Binary: {
                std::string lhs = emitExpr(expr->binary.lhs.get());
                std::string rhs = emitExpr(expr->binary.rhs.get());
                std::string res = freshTemp();
                std::string opStr = "add";
                bool isFloatOp = false;
                
                std::string varType = getExprType(expr->binary.lhs.get());
                if (varType == "f32" || varType == "f64") {
                    isFloatOp = true;
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

                out_ << "    " << res << " = " << opStr << " " << varType + " " << lhs << ", " << rhs << "\n";
                return res;
            }

            case Expr::Kind::Assign: {
                std::string rhsVal = emitExpr(expr->assign.rhs.get());

                // Tangani Compound Assignment (+=, -=, *=, /=, %=)
                if (expr->assign.op != AssignOp::Assign) {
                    std::string lhsVal = emitExpr(expr->assign.lhs.get());
                    std::string opStr = "add";
                    switch (expr->assign.op) {
                        case AssignOp::AddEq: opStr = "add"; break;
                        case AssignOp::SubEq: opStr = "sub"; break;
                        case AssignOp::MulEq: opStr = "mul"; break;
                        case AssignOp::DivEq: opStr = "div"; break;
                        case AssignOp::ModEq: opStr = "rem"; break;
                        default: opStr = "add"; break;
                    }
                    std::string varType = getExprType(expr->assign.lhs.get());
                    std::string binRes = freshTemp();
                    out_ << "    " << binRes << " = " << opStr << " " << varType << " " << lhsVal << ", " << rhsVal << "\n";
                    rhsVal = binRes; // Nilai RHS yang baru adalah hasil dari operasi biner
                }

                // Penugasan ke elemen array (Postfix Index, cth: txt[0] += 1)
                if (expr->assign.lhs->kind == Expr::Kind::Postfix) {
                    auto& postfix = expr->assign.lhs->postfix;
                    if (!postfix.ops.empty() && postfix.ops.back().kind == Expr::PostfixOp::Kind::Index) {
                        std::string baseName = "";
                        if (postfix.base && postfix.base->kind == Expr::Kind::Ident && 
                            postfix.base->ident && !postfix.base->ident->parts.empty()) {
                            baseName = postfix.base->ident->parts.back();
                        }
                        
                        std::string currentReg = "%" + baseName;
                        std::string currentType = "i32";
                        if (symbolTypes_.find(baseName) != symbolTypes_.end()) {
                            currentType = symbolTypes_[baseName];
                        }

                        // Evaluasi ekspresi indeks
                        auto& lastOp = postfix.ops.back();
                        std::string indexVal = emitExpr(lastOp.index.get());

                        // Ekstrak tipe elemen dari tipe array (misal "[12]i8" menjadi "i8")
                        std::string elemType = "i32";
                        if (currentType.rfind("[", 0) == 0) {
                            size_t bracketPos = currentType.find("]");
                            if (bracketPos != std::string::npos) {
                                elemType = currentType.substr(bracketPos + 1);
                            }
                        }

                        std::string elemAddrRes = freshTemp();
                        out_ << "    " << elemAddrRes << " = elem addr " << elemType << " " << currentReg << ", " << indexVal << "\n";
                        out_ << "    store " << elemType << " " << rhsVal << ", " << elemAddrRes << "\n";
                        return rhsVal;
                    }
                }

                // Penugasan standar ke Variabel Global atau Lokal biasa
                if (expr->assign.lhs->kind == Expr::Kind::Ident && expr->assign.lhs->ident) {
                    if (expr->assign.lhs->ident->is_absolute) {
                        std::string globalName = flattenQualifiedName(expr->assign.lhs->ident.get());
                        std::string addrRes = freshTemp();
                        out_ << "    " << addrRes << " = global addr @" << globalName << "\n";
                        out_ << "    store i32 " << rhsVal << ", " << addrRes << "\n";
                        return rhsVal;
                    }

                    std::string varType = "i32";
                    std::string lhsName = expr->assign.lhs->ident->parts.back();
                    if (symbolTypes_.find(lhsName) != symbolTypes_.end()) {
                        varType = symbolTypes_[lhsName];
                    }
                    std::string varName = "%" + lhsName;
                    out_ << "    store " + varType + " " << rhsVal << ", " << varName << "\n";
                }
                return rhsVal;
            }

            case Expr::Kind::Ternary: {
                std::string condVal = emitExpr(expr->ternary.cond.get());
                
                std::string thenLabel = freshTemp(true) + "_tern_then";
                std::string elseLabel = freshTemp(true) + "_tern_else";
                std::string mergeLabel = freshTemp(true) + "_tern_merge";
                
                std::string resType = getExprType(expr->ternary.then_expr.get());
                
                std::string resultAlloc = freshTemp();
                out_ << "    " << resultAlloc << " = alloca " << resType << "\n";
                
                out_ << "    br_if " << condVal << ", " << thenLabel << ", " << elseLabel << "\n";
                
                out_ << thenLabel << ":\n";
                std::string thenVal = emitExpr(expr->ternary.then_expr.get());
                out_ << "    store " << resType << " " << thenVal << ", " << resultAlloc << "\n";
                out_ << "    jmp " << mergeLabel << "\n";
                
                out_ << elseLabel << ":\n";
                std::string elseVal = emitExpr(expr->ternary.else_expr.get());
                out_ << "    store " << resType << " " << elseVal << ", " << resultAlloc << "\n";
                out_ << "    jmp " << mergeLabel << "\n";
                
                out_ << mergeLabel << ":\n";
                std::string finalVal = freshTemp();
                out_ << "    " << finalVal << " = load " << resType << " " << resultAlloc << "\n";
                
                return finalVal;
            }

            case Expr::Kind::Cast: {
                std::string operandVal = emitExpr(expr->cast.base.get());
                std::string res = freshTemp();
                std::string targetType = "i32";
                if (expr->cast.target) {
                    targetType = emitType(expr->cast.target.get());
                }

                out_ << "    " << res << " = cast " << operandVal << " to " << targetType << "\n";
                return res;
            }

            case Expr::Kind::Unary: {
                std::string operandVal = emitExpr(expr->unary.expr.get());
                std::string res = freshTemp();
                
                switch (expr->unary.op) {
                    case UnaryOp::Ref: {
                        if (expr->unary.expr->kind == Expr::Kind::Ident && 
                            expr->unary.expr->ident) {
                            if (expr->unary.expr->ident->is_absolute) {
                                std::string globalName = flattenQualifiedName(expr->unary.expr->ident.get());
                                std::string addrRes = freshTemp();
                                out_ << "    " << addrRes << " = global addr @" << globalName << "\n";
                                return addrRes;
                            }
                            if (!expr->unary.expr->ident->parts.empty()) {
                                return "%" + expr->unary.expr->ident->parts.back();
                            }
                        }
                        return operandVal;
                    }

                    case UnaryOp::Deref: {
                        std::string varType = "i32";
                        if (expr->unary.expr->kind == Expr::Kind::Ident && 
                            expr->unary.expr->ident && 
                            !expr->unary.expr->ident->parts.empty()) {
                            std::string idName = expr->unary.expr->ident->parts.back();
                            if (symbolTypes_.find(idName) != symbolTypes_.end()) {
                                std::string baseType = symbolTypes_[idName];
                                varType = (baseType == "ptr") ? "i32" : baseType;
                            }
                        }

                        out_ << "    " << res << " = load " << varType << " " << operandVal << "\n";
                        return res;
                    }

                    case UnaryOp::Neg: {
                        // DINAMIS: Menggunakan tipe operand aktual alih-alih hardcode i32
                        std::string operandType = getExprType(expr->unary.expr.get());
                        out_ << "    " << res << " = sub " << operandType << " 0, " << operandVal << "\n";
                        return res;
                    }

                    case UnaryOp::Not: {
                        out_ << "    " << res << " = xor bool " << operandVal << ", true\n";
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
                std::string currentType = "i32";
                if (symbolTypes_.find(baseName) != symbolTypes_.end()) {
                    currentType = symbolTypes_[baseName];
                }

                for (const auto& op : expr->postfix.ops) {
                    if (op.kind == Expr::PostfixOp::Kind::Call) {
                        std::vector<std::string> argVals;
                        for (const auto& arg : op.args) {
                            argVals.push_back(emitExpr(arg.get()));
                        }

                        // DINAMIS: Resolusi return type fungsi dari map functionReturnTypes_
                        std::string retType = "i32";
                        if (functionReturnTypes_.find(baseName) != functionReturnTypes_.end()) {
                            retType = functionReturnTypes_[baseName];
                        }

                        std::string callRes = freshTemp();
                        out_ << "    " << callRes << " = call." << retType << " @" << baseName << "(";
                        for (size_t i = 0; i < argVals.size(); ++i) {
                            out_ << argVals[i];
                            if (i + 1 < argVals.size()) out_ << ", ";
                        }
                        out_ << ")\n";
                        return callRes;
                    } else if (op.kind == Expr::PostfixOp::Kind::Index) {
                        std::string indexVal = emitExpr(op.index.get());
                        std::string elemType = "i32";
                        if (currentType.rfind("[", 0) == 0) {
                            size_t bracketPos = currentType.find("]");
                            if (bracketPos != std::string::npos) {
                                elemType = currentType.substr(bracketPos + 1);
                            }
                        }
                        std::string elemAddrRes = freshTemp();
                        out_ << "    " << elemAddrRes << " = elem addr " << elemType << " " << currentReg << ", " << indexVal << "\n";
                        
                        std::string loadRes = freshTemp();
                        out_ << "    " << loadRes << " = load " << elemType << " " << elemAddrRes << "\n";
                        currentReg = loadRes;
                        currentType = elemType;
                    } else if (op.kind == Expr::PostfixOp::Kind::MemberAccess || 
                               op.kind == Expr::PostfixOp::Kind::Arrow || 
                               op.kind == Expr::PostfixOp::Kind::SafeArrow) 
                    {
                        int fieldIndex = 0;
                        std::string fieldType = "i32";
            
                        if (structDefinitions_.find(currentType) != structDefinitions_.end()) {
                            const auto& sInfo = structDefinitions_[currentType];
                            for (const auto& f : sInfo.fields) {
                                if (f.name == op.name) {
                                    fieldIndex = f.index;
                                    fieldType = f.type;
                                    break;
                                }
                            }  
                        }

                        std::string fieldAddrRes = freshTemp();
                        out_ << "    " << fieldAddrRes << " = field addr " << currentType << ", " << currentReg << ", " << fieldIndex << "\n";
            
                        std::string loadRes = freshTemp();
                        out_ << "    " << loadRes << " = load " << fieldType << " " << fieldAddrRes << "\n";
             
                        currentReg = loadRes;
                        currentType = fieldType;
                    }
                }
                return currentReg;
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
