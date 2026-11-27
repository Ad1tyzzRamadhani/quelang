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
        pointerStructTypes_.clear();

                for (const auto& node : program->decls) {
            if (auto structDef = dynamic_cast<StructDef*>(node.get())) {
                emitStruct(structDef);
            } else if (auto unionDef = dynamic_cast<UnionDef*>(node.get())) {
                emitUnion(unionDef);
            }
        }
        for (const auto& node : program->decls) {
            if (dynamic_cast<StructDef*>(node.get()) || dynamic_cast<UnionDef*>(node.get())) {
                continue;
            }
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
    std::unordered_map<std::string, std::string> pointerStructTypes_;
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
    std::vector<std::unordered_map<std::string, std::string>> scopeTypes_;
    std::unordered_map<std::string, std::string> paramElementTypes_;
    std::unordered_map<std::string, std::string> symbolElementTypes_;

    std::ostringstream out_;
    int tempCounter_ = 0;

    struct FieldInfo {
        std::string name;
        std::string type;
        std::string elementType;
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

    bool lookupVar(const std::string& name, std::string& outReg, std::string& outType) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            if (it->find(name) != it->end()) {
                outReg = (*it)[name];
                for (auto tit = scopeTypes_.rbegin(); tit != scopeTypes_.rend(); ++tit) {
                    if (tit->find(name) != tit->end()) {
                        outType = (*tit)[name];
                        break;
                    }
                }
                return true;
            }
        }
        return false;
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

    void recordPointerStructType(const std::string& name, Type* type) {
    if (!type || !type->base || type->base->parts.empty()) return;
    bool hasPtr = false;
    for (const auto& mod : type->modifiers) {
        if (mod.kind == TypeModifier::Kind::Pointer ||
            mod.kind == TypeModifier::Kind::Reference) {
            hasPtr = true;
            break;
        }
    }
    if (hasPtr) {
        std::string baseName = type->base->parts.back();
        if (structDefinitions_.count(baseName)) {
            pointerStructTypes_[name] = baseName;
        }
    }
}


            std::string getIndexElemType(const std::string& baseName, const std::string& currentType) {
        if (currentType.rfind("[", 0) == 0) {
            size_t bracketPos = currentType.find("]");
            if (bracketPos != std::string::npos) {
                std::string elem = currentType.substr(bracketPos + 1);
                if (elem == "ptr" && !baseName.empty() && pointerStructTypes_.count(baseName)) {
                    return "ptr";
                }
                return elem;
            }
        }
        if (currentType == "ptr") {
            if (!baseName.empty() && pointerStructTypes_.count(baseName)) {
                return pointerStructTypes_[baseName];
            }
            if (paramElementTypes_.find(baseName) != paramElementTypes_.end()) {
                return paramElementTypes_[baseName];
            }
            if (symbolElementTypes_.find(baseName) != symbolElementTypes_.end()) {
                return symbolElementTypes_[baseName];
            }
            return "i8"; // Default aman untuk char*
        }
        return "i32";
    }


    std::string getPointeeType(Expr* expr) {
    if (!expr) return "i32";

    if (expr->kind == Expr::Kind::Ident && expr->ident && !expr->ident->parts.empty()) {
        std::string name = expr->ident->parts.back();
        if (symbolElementTypes_.count(name)) return symbolElementTypes_[name];
        if (paramElementTypes_.count(name)) return paramElementTypes_[name];
    }
    if (expr->kind == Expr::Kind::Group && expr->group.expr) {
        return getPointeeType(expr->group.expr.get());
    }

    if (expr->kind == Expr::Kind::Postfix) {
        std::string currentType = getExprType(expr->postfix.base.get());
        std::string lastElemType = "i8";
        if (expr->postfix.base && expr->postfix.base->kind == Expr::Kind::Ident &&
            expr->postfix.base->ident && !expr->postfix.base->ident->parts.empty()) {
            std::string baseName = expr->postfix.base->ident->parts.back();
            if (symbolElementTypes_.count(baseName)) lastElemType = symbolElementTypes_[baseName];
            else if (paramElementTypes_.count(baseName)) lastElemType = paramElementTypes_[baseName];
        }

        for (const auto& op : expr->postfix.ops) {
            if (op.kind == Expr::PostfixOp::Kind::MemberAccess ||
                op.kind == Expr::PostfixOp::Kind::Arrow ||
                op.kind == Expr::PostfixOp::Kind::SafeArrow) {
                if (structDefinitions_.count(currentType)) {
                    for (const auto& f : structDefinitions_[currentType].fields) {
                        if (f.name == op.name) {
                            currentType = f.type;
                            lastElemType = f.elementType;
                            break;
                        }
                    }
                }
                        
             
            
             } else if (op.kind == Expr::PostfixOp::Kind::Index) {
                if (currentType.rfind("[", 0) == 0) {
                    size_t closeBracket = currentType.find("]");
                    if (closeBracket != std::string::npos) {
                        std::string elem = currentType.substr(closeBracket + 1);
                        if (elem == "ptr") {
                            currentType = "ptr";
                            lastElemType = "ptr";
                            continue;
                        }
                    }
                }
                currentType = lastElemType;
            }

        }
        return lastElemType;
    }

    if (expr->kind == Expr::Kind::Unary && expr->unary.op == UnaryOp::Ref) {
        return getExprType(expr->unary.expr.get());
    }

    if (expr->kind == Expr::Kind::Cast && expr->cast.target) {
        return getElementType(expr->cast.target.get());
    }

    return "i8";
}


    std::string getElementType(Type* type) {
        if (!type) return "i32";
        if (type->is_array) {
            if (type->base && !type->base->parts.empty()) {
                std::string name = type->base->parts.back();
                if (name == "char") return "i8";
                if (name == "int") return "i32";
                if (name == "short") return "i16";
                if (name == "long") return "i64";
                return name;
            }
            return "i32";
        }

        int ptrCount = 0;
        for (const auto& mod : type->modifiers) {
            if (mod.kind == TypeModifier::Kind::Pointer || 
                mod.kind == TypeModifier::Kind::Reference ||
                mod.kind == TypeModifier::Kind::FuncPtr) {
                ptrCount++;
            }
        }

        if (ptrCount > 0) {
            // Jika pointer berlapis (misal T** / char**), hasil indexing-nya adalah ptr
            if (ptrCount > 1) return "ptr";
            
            // Jika pointer tunggal (misal char* s), ambil tipe dasarnya
            if (type->base && !type->base->parts.empty()) {
                std::string name = type->base->parts.back();
                if (name == "char") return "i8";
                if (name == "int") return "i32";
                if (name == "short") return "i16";
                if (name == "long") return "i64";
            }
            return "i8"; // Default aman untuk char*
        }

        if (type->base && !type->base->parts.empty()) {
            std::string name = type->base->parts.back();
            if (name == "char") return "i8";
            if (name == "int") return "i32";
            return name;
        }
        return "i32";
    }

    std::string escapeString(const std::string& str) {
        std::string result = "";
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '\n') {
                result += "\\n"; // Ubah enter fisik menjadi representasi escape \n
            } else if (str[i] == '\r') {
                result += "\\r";
            } else if (str[i] == '\t') {
                result += "\\t";
            } else {
                result += str[i];
            }
        }
        return result;
    }

    std::string resolveStructTypeForArrow(const std::string& baseName, const std::string& curType) const {
    if (structDefinitions_.count(curType)) {
        return curType;
    }
    if (!baseName.empty() && pointerStructTypes_.count(baseName)) {
        return pointerStructTypes_.at(baseName);
    }
    return curType;
    }

    std::string emitFieldAddr(Expr* expr, std::string& outFieldType) {
        if (!expr) {
            outFieldType = "i32";
            return "0";
        }

        Expr* baseExpr = unwrapGroup(expr->postfix.base.get());
        std::string baseName;
        std::string curReg;
        std::string curType = "i32";

        if (baseExpr && baseExpr->kind == Expr::Kind::Unary && baseExpr->unary.op == UnaryOp::Deref) {
            Expr* ptrExpr = unwrapGroup(baseExpr->unary.expr.get());
            if (ptrExpr && ptrExpr->kind == Expr::Kind::Ident && ptrExpr->ident && !ptrExpr->ident->parts.empty()) {
                baseName = ptrExpr->ident->parts.back();
            }

            curReg = emitExpr(ptrExpr);
            curType = resolvePointeeStructType(ptrExpr);
            if (curType.empty() && !baseName.empty()) {
                if (pointerStructTypes_.count(baseName)) curType = pointerStructTypes_[baseName];
                else if (paramElementTypes_.count(baseName)) curType = paramElementTypes_[baseName];
                else if (symbolElementTypes_.count(baseName)) curType = symbolElementTypes_[baseName];
            }
        } else {
            if (baseExpr && baseExpr->kind == Expr::Kind::Ident && baseExpr->ident && !baseExpr->ident->parts.empty()) {
                baseName = baseExpr->ident->parts.back();
            }

            std::string varReg, varType;
            if (!baseName.empty() && lookupVar(baseName, varReg, varType)) {
                if (!resolveStructTypeName(varType).empty()) {
                    curReg = varReg;
                    curType = resolveStructTypeName(varType);
                } else if (varType == "ptr") {
                    std::string loadedPtr = freshTemp();
                    out_ << "    " << loadedPtr << " = load ptr " << varReg << "\n";
                    curReg = loadedPtr;
                    curType = pointerStructTypes_.count(baseName)
                                  ? pointerStructTypes_[baseName]
                                  : (paramElementTypes_.count(baseName) ? paramElementTypes_[baseName] : "ptr");
                } else {
                    std::string loadedVal = freshTemp();
                    out_ << "    " << loadedVal << " = load " << varType << " " << varReg << "\n";
                    curReg = loadedVal;
                    curType = varType;
                }
            } else if (baseExpr) {
                curReg = emitExpr(baseExpr);
                curType = getExprType(baseExpr);
            } else {
                curReg = "%" + baseName;
            }
        }

        for (size_t i = 0; i < expr->postfix.ops.size(); ++i) {
            const auto& op = expr->postfix.ops[i];
            if (op.kind == Expr::PostfixOp::Kind::MemberAccess ||
                op.kind == Expr::PostfixOp::Kind::Arrow ||
                op.kind == Expr::PostfixOp::Kind::SafeArrow) {

                std::string structType = resolveStructTypeName(curType);
                if (structType.empty() && !baseName.empty() && pointerStructTypes_.count(baseName)) {
                    structType = pointerStructTypes_[baseName];
                }
                if (structType.empty()) {
                    structType = curType;
                }

                int fieldIndex = 0;
                std::string fieldType = "i32";
                if (structDefinitions_.count(structType)) {
                    for (const auto& f : structDefinitions_[structType].fields) {
                        if (f.name == op.name) {
                            fieldIndex = f.index;
                            fieldType = f.type;
                            break;
                        }
                    }
                }

                std::string fieldAddr = freshTemp();
                out_ << "    " << fieldAddr << " = field addr " << structType << ", " << curReg << ", " << fieldIndex << "\n";

                bool isLast = (i + 1 == expr->postfix.ops.size());
                if (isLast) {
                    outFieldType = fieldType;
                    return fieldAddr;
                }

                if (!resolveStructTypeName(fieldType).empty() || fieldType.rfind("[", 0) == 0) {
                    curReg = fieldAddr;
                    curType = fieldType;
                } else {
                    std::string loaded = freshTemp();
                    out_ << "    " << loaded << " = load " << fieldType << " " << fieldAddr << "\n";
                    curReg = loaded;
                    curType = fieldType;
                }
            }
        }

        outFieldType = curType;
        return curReg;
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
            case Expr::Kind::Group:
                if (expr->group.expr) {
                    return getExprType(expr->group.expr.get());
                }
                return "i32";
            case Expr::Kind::Intrinsic:
                if (expr->intrinsic.type) {
                    return emitType(expr->intrinsic.type.get());
                }
                return "i32";
            case Expr::Kind::Postfix: {
                std::string baseName = "";
                if (expr->postfix.base && expr->postfix.base->kind == Expr::Kind::Ident) {
                    if (expr->postfix.base->ident && !expr->postfix.base->ident->parts.empty()) {
                        baseName = expr->postfix.base->ident->parts.back();
                    }
                }

                std::string currentType = "i32";
                Expr* unwrappedBase = unwrapGroup(expr->postfix.base.get());
                if (unwrappedBase && unwrappedBase->kind == Expr::Kind::Ident &&
                    unwrappedBase->ident && !unwrappedBase->ident->parts.empty()) {
                    std::string baseName = unwrappedBase->ident->parts.back();
                    if (symbolTypes_.find(baseName) != symbolTypes_.end()) {
                        currentType = symbolTypes_[baseName];
                    } else if (paramTypes_.find(baseName) != paramTypes_.end()) {
                        currentType = paramTypes_[baseName];
                    }
                } else if (expr->postfix.base) {
                    currentType = getExprType(expr->postfix.base.get());
                }


                for (const auto& op : expr->postfix.ops) {
                    if (op.kind == Expr::PostfixOp::Kind::Index) {
                        currentType = getIndexElemType(baseName, currentType);
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
                switch (expr->binary.op) {
                    case BinaryOp::Eq:
                    case BinaryOp::Ne:
                    case BinaryOp::Lt:
                    case BinaryOp::Lte:
                    case BinaryOp::Gt:
                    case BinaryOp::Gte:
                    case BinaryOp::And:
                    case BinaryOp::Or:
                    case BinaryOp::Xor:
                        return "bool";
                    default:
                        return getExprType(expr->binary.lhs.get());
                }
            case Expr::Kind::Unary:
                if (expr->unary.op == UnaryOp::Not) return "bool";
                if (expr->unary.op == UnaryOp::Ref) return "ptr";
                if (expr->unary.op == UnaryOp::Deref) return getPointeeType(expr->unary.expr.get());
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

    std::string resolvePointeeStructType(Expr* ptrExpr) const {
        Expr* e = unwrapGroup(ptrExpr);
        if (!e) return "";

        if (e->kind == Expr::Kind::Ident && e->ident && !e->ident->parts.empty()) {
            std::string name = e->ident->parts.back();
            if (pointerStructTypes_.count(name)) return pointerStructTypes_.at(name);
            if (symbolElementTypes_.count(name) && structDefinitions_.count(symbolElementTypes_.at(name))) {
                return symbolElementTypes_.at(name);
            }
            if (paramElementTypes_.count(name) && structDefinitions_.count(paramElementTypes_.at(name))) {
                return paramElementTypes_.at(name);
            }
        }

        if (e->kind == Expr::Kind::Postfix && e->postfix.base) {
            Expr* b = unwrapGroup(e->postfix.base.get());
            if (b && b->kind == Expr::Kind::Ident && b->ident && !b->ident->parts.empty()) {
                std::string baseName = b->ident->parts.back();
                for (const auto& op : e->postfix.ops) {
                    if (op.kind == Expr::PostfixOp::Kind::Index) {
                        if (symbolElementTypes_.count(baseName) && structDefinitions_.count(symbolElementTypes_.at(baseName))) {
                            return symbolElementTypes_.at(baseName);
                        }
                        if (paramElementTypes_.count(baseName) && structDefinitions_.count(paramElementTypes_.at(baseName))) {
                            return paramElementTypes_.at(baseName);
                        }
                        if (pointerStructTypes_.count(baseName)) {
                            return pointerStructTypes_.at(baseName);
                        }
                    }
                }
            }
        }
        return "";
    }


    Expr* unwrapGroup(Expr* e) const {
        while (e && e->kind == Expr::Kind::Group && e->group.expr) {
            e = e->group.expr.get();
        }
        return e;
    }

    std::string resolveStructTypeName(const std::string& t) const {
        if (structDefinitions_.count(t)) {
            return t;
        }
        if (pointerStructTypes_.count(t)) {
            return pointerStructTypes_.at(t);
        }
        if (symbolElementTypes_.count(t) && structDefinitions_.count(symbolElementTypes_.at(t))) {
            return symbolElementTypes_.at(t);
        }
        if (paramElementTypes_.count(t) && structDefinitions_.count(paramElementTypes_.at(t))) {
            return paramElementTypes_.at(t);
        }
        return "";
    }


    std::string emitType(Type* type) {
        if (!type) return "void";

        bool has_pointer = false;
        for (const auto& mod : type->modifiers) {
            if (mod.kind == TypeModifier::Kind::Pointer ||
                mod.kind == TypeModifier::Kind::Reference ||
                mod.kind == TypeModifier::Kind::FuncPtr) {
                has_pointer = true;
                break;
            }
        }

        if (type->is_array) {
            if (type->array_dims.empty()) {
                return "ptr";
            }

            std::string sizeStr = type->array_dims[0]->value;
            std::string elemType = "i32";

            if (has_pointer) {
                elemType = "ptr";
            } else if (type->base && !type->base->parts.empty()) {
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

        if (has_pointer) {
            return "ptr";
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
        scopes_.clear();
        scopeTypes_.clear();
        scopes_.push_back({});
        scopeTypes_.push_back({});

        out_ << "def " << retType << " @" << name << "(";
        
        for (size_t i = 0; i < func->params.size(); ++i) {
            const auto& p = func->params[i];
            std::string pType = emitType(p.type.get());
            paramTypes_[p.name] = pType;
            std::string elemT = getElementType(p.type.get());
            paramElementTypes_[p.name] = elemT;
            scopes_.back()[p.name] = "%" + p.name;
            scopeTypes_.back()[p.name] = pType;
            recordPointerStructType(p.name, p.type.get());
            out_ << pType  << " " + p.name;
            if (i + 1 < func->params.size()) out_ << ", ";
        }
        out_ << ")\n";
        out_ << "entry:\n";

        for (const auto& p : func->params) {
            bool is_const = false;
            for (auto& kind : p.type->qualifiers) {
                if (kind == TypeQualifier::Const) is_const = true;
            }

            if (!is_const) {
                std::string pType = emitType(p.type.get());
                std::string stackReg = freshTemp();
                out_ << "    " << stackReg << " = alloca " << pType << "\n";
                out_ << "    store " << pType << " %" << p.name << ", " << stackReg << "\n";
                
                // Timpa scope dengan register stack alokasi agar bisa di-assign ulang
                scopes_.back()[p.name] = stackReg;
                scopeTypes_.back()[p.name] = pType;
            }
        }

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
                std::string fieldElemType = getElementType(var->type.get());
                for (const auto& item : var->items) {
                    out_ << "    " << item.name << ": " << fieldType << "\n";
                    info.fields.push_back({item.name, fieldType, fieldElemType, index++});
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

    bool tryFormatGlobalLiteral(Expr* expr, std::string& outStr) {
    if (!expr) return false;

    if (expr->kind == Expr::Kind::Literal && expr->literal) {
        switch (expr->literal->kind) {
            case Literal::Kind::String:
                outStr = "\"" + escapeString(expr->literal->value) + "\"";
                return true;
            case Literal::Kind::Char:
                outStr = "'" + escapeString(expr->literal->value) + "'";
                return true;
            case Literal::Kind::True:
                outStr = "true";
                return true;
            case Literal::Kind::False:
                outStr = "false";
                return true;
            case Literal::Kind::Number:
            case Literal::Kind::Float:
            case Literal::Kind::Hex:
            case Literal::Kind::Binary:
                outStr = expr->literal->value;
                return true;
            default:
                return false;
        }
    }

    // Dukung literal negatif seperti -10 atau -2.5 bila direpresentasikan sebagai Unary Neg
    if (expr->kind == Expr::Kind::Unary &&
        expr->unary.op == UnaryOp::Neg &&
        expr->unary.expr &&
        expr->unary.expr->kind == Expr::Kind::Literal &&
        expr->unary.expr->literal) {
        auto k = expr->unary.expr->literal->kind;
        if (k == Literal::Kind::Number || k == Literal::Kind::Float) {
            outStr = "-" + expr->unary.expr->literal->value;
            return true;
        }
    }

    return false;
}

void emitGlobalVar(VarDecl* varDecl) {
    if (!varDecl || !varDecl->type) return;

    for (const auto& item : varDecl->items) {
        bool is_const = false;
        for (auto& kind : varDecl->type->qualifiers) {
            if (kind == TypeQualifier::Const) is_const = true;
        }

        std::string t = emitType(varDecl->type.get());
        globalTypes_[item.name] = t;

        std::string initStr = "";
        std::string litStr;
        if (item.init && tryFormatGlobalLiteral(item.init.get(), litStr)) {
            initStr = " = " + litStr;
        }

        std::string prefix = is_const ? "constant " : "global ";
        out_ << prefix << item.name << ": " << t << initStr << "\n";
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
                scopes_.push_back({});
                scopeTypes_.push_back({});
                for (const auto& s : stmt->block.stmts) {
                    emitStmt(s.get());
                }
                scopeTypes_.pop_back();
                scopes_.pop_back();
                break;

            case Stmt::Kind::VarDecl: {
                for (const auto& item : stmt->var_decl.items) {
                    std::string t = emitType(stmt->var_decl.type.get());
                    symbolTypes_[item.name] = t;
                    std::string elemT = getElementType(stmt->var_decl.type.get());
                    symbolElementTypes_[item.name] = elemT;
                    recordPointerStructType(item.name, stmt->var_decl.type.get());
                    std::string reg = "%" + item.name + "_" + std::to_string(tempCounter_++);
                    if (!scopes_.empty()) {
                        scopes_.back()[item.name] = reg;
                        scopeTypes_.back()[item.name] = t;
                    }
                    if (item.init && item.init->kind == Expr::Kind::StructInit) {
                        std::string structReg = emitExpr(item.init.get());
                        if (!scopes_.empty()) {
                            scopes_.back()[item.name] = structReg;
                            scopeTypes_.back()[item.name] = t;
                        }
                    }
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
                    out_ << "    ret void\n";
                }
                break;
            case Stmt::Kind::ArrayStream: {
                // 1. Dapatkan register alokasi stack target di sisi kiri (lvalue)
                std::string targetReg = emitExpr(stmt->array_stream.target.get());
                
                std::string targetType = getExprType(stmt->array_stream.target.get());
                std::string elemType = "i8"; 
                if (targetType.rfind("[", 0) == 0) {
                    size_t bracketPos = targetType.find("]");
                    if (bracketPos != std::string::npos) {
                        elemType = targetType.substr(bracketPos + 1);
                    }
                }

                // 2. OTOMATIS CARI OFFSET TERAKHIR (Appending State):
                // Alih-alih selalu mulai dari 0, kita hitung posisi null-terminator ('\0') 
                // yang sudah ada di dalam buffer target agar bisa menyambung (append).
                std::string offsetReg = freshTemp();
                out_ << "    " << offsetReg << " = alloca i32\n";
                
                // Cari panjang string saat ini di buffer target (inline strlen)
                std::string scanIndex = freshTemp();
                out_ << "    " << scanIndex << " = alloca i32\n";
                out_ << "    store i32 0, " << scanIndex << "\n";

                std::string scanCond = freshTemp(true) + "_append_scan_cond";
                std::string scanBody = freshTemp(true) + "_append_scan_body";
                std::string scanEnd  = freshTemp(true) + "_append_scan_end";

                out_ << "    jmp " << scanCond << "\n";
                out_ << scanCond << ":\n";
                std::string curScan = freshTemp();
                std::string scanElemAddr = freshTemp();
                std::string scanChar = freshTemp();
                out_ << "    " << curScan << " = load i32 " << scanIndex << "\n";
                out_ << "    " << scanElemAddr << " = elem addr " << elemType << " " << targetReg << ", " << curScan << "\n";
                out_ << "    " << scanChar << " = load " << elemType << " " << scanElemAddr << "\n";
                
                std::string isNotEnd = freshTemp();
                out_ << "    " << isNotEnd << " = ne " << elemType << " " << scanChar << ", 0\n";
                out_ << "    br_if " << isNotEnd << ", " << scanBody << ", " << scanEnd << "\n";

                out_ << scanBody << ":\n";
                std::string nextScan = freshTemp();
                out_ << "    " << nextScan << " = add i32 " << curScan << ", 1\n";
                out_ << "    store i32 " << nextScan << ", " << scanIndex << "\n";
                out_ << "    jmp " << scanCond << "\n";

                out_ << scanEnd << ":\n";
                // Simpan posisi akhir penemuan null-terminator sebagai offset awal penyalinan baru
                std::string finalScanPos = freshTemp();
                out_ << "    " << finalScanPos << " = load i32 " << scanIndex << "\n";
                out_ << "    store i32 " << finalScanPos << ", " << offsetReg << "\n";

                // 3. Proses penyalinan sumber (sources) dilanjutkan mulai dari offset tersebut
                for (const auto& srcExpr : stmt->array_stream.sources) {
                    std::string srcVal = emitExpr(srcExpr.get());
                    std::string srcType = getExprType(srcExpr.get());
                    
                    bool isStringLiteral = (srcExpr->kind == Expr::Kind::Literal && 
                                            srcExpr->literal && 
                                            srcExpr->literal->kind == Literal::Kind::String);

                    if (isStringLiteral && elemType == "i8") {
                        std::string strContent = srcExpr->literal->value;
                        for (size_t i = 0; i < strContent.size(); ++i) {
                            std::string currentOffset = freshTemp();
                            std::string destAddr = freshTemp();
                            
                            out_ << "    " << currentOffset << " = load i32 " << offsetReg << "\n";
                            out_ << "    " << destAddr << " = elem addr " << elemType << " " << targetReg << ", " << currentOffset << "\n";
                            out_ << "    store " << elemType << " '" << strContent[i] << "', " << destAddr << "\n";
                            
                            std::string newOffset = freshTemp();
                            out_ << "    " << newOffset << " = add i32 " << currentOffset << ", 1\n";
                            out_ << "    store i32 " << newOffset << ", " << offsetReg << "\n";
                        }
                    } 
                    else if (srcType == "ptr" || srcType.rfind("[", 0) == 0) {
                        std::string srcIndexReg = freshTemp();
                        out_ << "    " << srcIndexReg << " = alloca i32\n";
                        out_ << "    store i32 0, " << srcIndexReg << "\n";

                        std::string loopCondLabel = freshTemp(true) + "_append_ptr_cond";
                        std::string loopBodyLabel = freshTemp(true) + "_append_ptr_body";
                        std::string loopEndLabel  = freshTemp(true) + "_append_ptr_end";

                        out_ << "    jmp " << loopCondLabel << "\n";
                        out_ << loopCondLabel << ":\n";
                        
                        std::string currentSrcIdx = freshTemp();
                        std::string srcElemAddr = freshTemp();
                        out_ << "    " << currentSrcIdx << " = load i32 " << srcIndexReg << "\n";
                        out_ << "    " << srcElemAddr << " = elem addr " << elemType << " " << srcVal << ", " << currentSrcIdx << "\n";
                        
                        std::string charVal = freshTemp();
                        out_ << "    " << charVal << " = load " << elemType << " " << srcElemAddr << "\n";
                        
                        std::string isNotZero = freshTemp();
                        out_ << "    " << isNotZero << " = ne " << elemType << " " << charVal << ", 0\n";
                        out_ << "    br_if " << isNotZero << ", " << loopBodyLabel << ", " << loopEndLabel << "\n";

                        out_ << loopBodyLabel << ":\n";
                        std::string currentDestOffset = freshTemp();
                        std::string destAddr = freshTemp();
                        out_ << "    " << currentDestOffset << " = load i32 " << offsetReg << "\n";
                        out_ << "    " << destAddr << " = elem addr " << elemType << " " << targetReg << ", " << currentDestOffset << "\n";
                        out_ << "    store " << elemType << " " << charVal << ", " << destAddr << "\n";

                        std::string newDestOffset = freshTemp();
                        out_ << "    " << newDestOffset << " = add i32 " << currentDestOffset << ", 1\n";
                        out_ << "    store i32 " << newDestOffset << ", " << offsetReg << "\n";

                        std::string newSrcIndex = freshTemp();
                        out_ << "    " << newSrcIndex << " = add i32 " << currentSrcIdx << ", 1\n";
                        out_ << "    store i32 " << newSrcIndex << ", " << srcIndexReg << "\n";
                        
                        out_ << "    jmp " << loopCondLabel << "\n";

                        out_ << loopEndLabel << ":\n";
                    } 
                    else {
                        std::string currentOffset = freshTemp();
                        std::string destAddr = freshTemp();
                        
                        out_ << "    " << currentOffset << " = load i32 " << offsetReg << "\n";
                        out_ << "    " << destAddr << " = elem addr " << elemType << " " << targetReg << ", " << currentOffset << "\n";
                        out_ << "    store " << elemType << " " << srcVal << ", " << destAddr << "\n";
                        
                        std::string newOffset = freshTemp();
                        out_ << "    " << newOffset << " = add i32 " << currentOffset << ", 1\n";
                        out_ << "    store i32 " << newOffset << ", " << offsetReg << "\n";
                    }
                }

                // 4. Tutup selalu dengan null terminator (0) di ujung akhir
                if (elemType == "i8") {
                    std::string finalOffset = freshTemp();
                    std::string endAddr = freshTemp();
                    out_ << "    " << finalOffset << " = load i32 " << offsetReg << "\n";
                    out_ << "    " << endAddr << " = elem addr " << elemType << " " << targetReg << ", " << finalOffset << "\n";
                    out_ << "    store " << elemType << " 0, " << endAddr << "\n";
                }
                break;
            }

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
                        return "\"" + escapeString(expr->literal->value) + "\"";
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
                        std::string varReg, varType;
                        if (paramTypes_.find(varName) != paramTypes_.end()) {
                            return "%" + varName;
                        }
                        if (lookupVar(varName, varReg, varType)) {
                            if (varType == "ptr") {
                                std::string ptrVal = freshTemp();
                                out_ << "    " << ptrVal << " = load ptr " << varReg << "\n";
                                return ptrVal;
                            }
                            if (varType.rfind("[", 0) == 0) {
                                return varReg;
                            }
                            std::string loadRes = freshTemp();
                            out_ << "    " << loadRes << " = load " << varType << " " << varReg << "\n";
                            return loadRes;
                        }
                        if (symbolTypes_.find(varName) != symbolTypes_.end()) {
                            std::string vType = symbolTypes_[varName];
                            std::string addrReg = "%" + varName;
                            if (vType == "ptr") {
                                std::string ptrVal = freshTemp();
                                out_ << "    " << ptrVal << " = load ptr " << addrReg << "\n";
                                return ptrVal;
                            }
                            if (vType.rfind("[", 0) == 0) {
                                return addrReg;
                            }
                            std::string loadRes = freshTemp();
                            out_ << "    " << loadRes << " = load " << vType << " " << addrReg << "\n";
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
                out_ << "    " << allocRes << " = alloca " << structName << "\n";

                int fieldIdx = 0;
                for (const auto& fieldPair : expr->struct_init.fields) {
                    std::string valRes = emitExpr(fieldPair.second.get());
                    std::string fieldAddrRes = freshTemp();
                    std::string fieldType = "i32";

                    auto it = structDefinitions_.find(structName);
                    if (it != structDefinitions_.end()) {
                        for (const auto& f : it->second.fields) {
                            if (f.name == fieldPair.first || f.index == fieldIdx) {
                                fieldType = f.type;
                                 break;
                            }
                        }
                    }
                    
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

            case Expr::Kind::Group:
                if (expr->group.expr) {
                    return emitExpr(expr->group.expr.get());
                }
                return "0";

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
                bool isRelational = false;

                switch (expr->binary.op) {
                    case BinaryOp::Add: opStr = "add"; break;
                    case BinaryOp::Sub: opStr = "sub"; break;
                    case BinaryOp::Mul: opStr = "mul"; break;
                    case BinaryOp::Div: opStr = "div"; break;
                    case BinaryOp::Mod: opStr = "rem"; break;
                    case BinaryOp::Eq:  opStr = "eq"; isRelational = true; break;
                    case BinaryOp::Ne:  opStr = "ne"; isRelational = true; break;
                    case BinaryOp::Lt:  opStr = "lt"; isRelational = true; break;
                    case BinaryOp::Lte: opStr = "le"; isRelational = true; break;
                    case BinaryOp::Gt:  opStr = "gt"; isRelational = true; break;
                    case BinaryOp::Gte: opStr = "ge"; isRelational = true; break;
                    case BinaryOp::And:
                    case BinaryOp::BitAnd: opStr = "and"; isRelational = true; break;
                    case BinaryOp::Or:
                    case BinaryOp::BitOr:  opStr = "or";  isRelational = true; break;
                    case BinaryOp::Xor:
                    case BinaryOp::BitXor: opStr = "xor"; isRelational = true; break;
                    default: opStr = "add"; break;
                }
                if (isFloatOp && !isRelational) {
                    opStr = "f" + opStr;
                }

                out_ << "    " << res << " = " << opStr << " " << varType + " " << lhs << ", " << rhs << "\n";
                return res;
            }
            
            case Expr::Kind::Assign: {
                std::string rhsVal = emitExpr(expr->assign.rhs.get());

                if (expr->assign.lhs->kind == Expr::Kind::Unary &&
                    expr->assign.lhs->unary.op == UnaryOp::Deref) {

                    Expr* innerExpr = expr->assign.lhs->unary.expr.get();
                    std::string ptrAddrReg;

                    if (innerExpr->kind == Expr::Kind::Ident && innerExpr->ident && !innerExpr->ident->parts.empty()) {
                        std::string varName = innerExpr->ident->parts.back();
                        std::string varReg, varType;

                        if (paramTypes_.find(varName) != paramTypes_.end()) {
                            if (lookupVar(varName, varReg, varType)) {
                                ptrAddrReg = freshTemp();
                                out_ << "    " << ptrAddrReg << " = load ptr " << varReg << "\n";
                            } else {
                                ptrAddrReg = "%" + varName;
                            }
                        } else if (lookupVar(varName, varReg, varType)) {
                            ptrAddrReg = freshTemp();
                            out_ << "    " << ptrAddrReg << " = load ptr " << varReg << "\n";
                        } else {
                            ptrAddrReg = emitExpr(innerExpr);
                        }
                    } else {
                        ptrAddrReg = emitExpr(innerExpr);
                    }

                    std::string rhsType = getExprType(expr->assign.rhs.get());
                    out_ << "    store " << rhsType << " " << rhsVal << ", " << ptrAddrReg << "\n";
                    return rhsVal;
                }

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
                    rhsVal = binRes;
                }

                // Penugasan ke elemen array / field (Postfix)
                if (expr->assign.lhs->kind == Expr::Kind::Postfix) {
                    auto& postfix = expr->assign.lhs->postfix;
                    if (!postfix.ops.empty() && postfix.ops.back().kind == Expr::PostfixOp::Kind::Index) {
                            Expr* unwrappedBase = unwrapGroup(postfix.base.get());
                            std::string baseName = "";
                            if (unwrappedBase && unwrappedBase->kind == Expr::Kind::Ident &&
                                unwrappedBase->ident && !unwrappedBase->ident->parts.empty()) {
                                baseName = unwrappedBase->ident->parts.back();
                            }

                            std::string currentReg = "%" + baseName;
                            std::string currentType = "i32";
                            std::string varReg, varType;
                            if (!baseName.empty() && lookupVar(baseName, varReg, varType)) {
                                currentReg = varReg;
                                currentType = varType;
                            } else if (!baseName.empty() && symbolTypes_.find(baseName) != symbolTypes_.end()) {
                                currentType = symbolTypes_[baseName];
                            } else if (unwrappedBase && unwrappedBase->kind == Expr::Kind::Unary &&
                                       unwrappedBase->unary.op == UnaryOp::Deref) {
                                currentReg = emitExpr(unwrappedBase->unary.expr.get());
                                std::string pStruct = resolvePointeeStructType(unwrappedBase->unary.expr.get());
                                currentType = pStruct.empty() ? getPointeeType(unwrappedBase->unary.expr.get()) : pStruct;
                            } else if (unwrappedBase) {
                                currentReg = emitExpr(unwrappedBase);
                                currentType = getExprType(unwrappedBase);
                            }


                        if (currentType == "ptr" && pointerStructTypes_.count(baseName)) {
                            std::string ptrLoaded = freshTemp();
                            out_ << "    " << ptrLoaded << " = load ptr " << currentReg << "\n";
                            currentReg = ptrLoaded;
                            currentType = pointerStructTypes_[baseName];
                        }

                        // Telusuri ops[0 .. ops.size() - 2] untuk mendapatkan alamat/register target terakhir
                        for (size_t i = 0; i + 1 < postfix.ops.size(); ++i) {
                            const auto& op = postfix.ops[i];
                            if (op.kind == Expr::PostfixOp::Kind::MemberAccess ||
                                op.kind == Expr::PostfixOp::Kind::Arrow ||
                                op.kind == Expr::PostfixOp::Kind::SafeArrow) {
                                std::string segStruct = currentType;
                                if (currentType == "ptr" && pointerStructTypes_.count(baseName)) {
                                    segStruct = pointerStructTypes_[baseName];
                                    currentType = segStruct;
                                }

                                int fieldIndex = 0;
                                std::string fieldType = "i32";
                                if (structDefinitions_.count(segStruct)) {
                                    for (const auto& f : structDefinitions_[segStruct].fields) {
                                        if (f.name == op.name) {
                                            fieldIndex = f.index;
                                            fieldType = f.type;
                                            break;
                                        }
                                    }
                                }

                                std::string fieldAddr = freshTemp();
                                out_ << "    " << fieldAddr << " = field addr " << segStruct << ", " << currentReg << ", " << fieldIndex << "\n";
                                if (fieldType.rfind("[", 0) == 0) {
                                    currentReg = fieldAddr;
                                    currentType = fieldType;
                                } else {
                                    std::string fVal = freshTemp();
                                    out_ << "    " << fVal << " = load " << fieldType << " " << fieldAddr << "\n";
                                    currentReg = fVal;
                                    currentType = fieldType;
                                }
                            } else if (op.kind == Expr::PostfixOp::Kind::Index) {
                                std::string idxVal = emitExpr(op.index.get());
                                std::string elemType = getIndexElemType(baseName, currentType);
                                std::string elemAddr = freshTemp();
                                out_ << "    " << elemAddr << " = elem addr " << elemType << " " << currentReg << ", " << idxVal << "\n";
                                if (elemType == "ptr") {
                                    std::string eVal = freshTemp();
                                    out_ << "    " << eVal << " = load ptr " << elemAddr << "\n";
                                    currentReg = eVal;
                                    currentType = "ptr";
                                } else {
                                    currentReg = elemAddr;
                                    currentType = elemType;
                                }
                            }
                        }

                        // Evaluasi ekspresi indeks terakhir
                        auto& lastOp = postfix.ops.back();
                        std::string indexVal = emitExpr(lastOp.index.get());
                        std::string elemType = getIndexElemType(baseName, currentType);

                        std::string elemAddrRes = freshTemp();
                        out_ << "    " << elemAddrRes << " = elem addr " << elemType << " " << currentReg << ", " << indexVal << "\n";
                        out_ << "    store " << elemType << " " << rhsVal << ", " << elemAddrRes << "\n";
                        return rhsVal;
                    }

                    if (!postfix.ops.empty()) {
                        auto& lastOp = postfix.ops.back();
                        if (lastOp.kind == Expr::PostfixOp::Kind::MemberAccess ||
                            lastOp.kind == Expr::PostfixOp::Kind::Arrow ||
                            lastOp.kind == Expr::PostfixOp::Kind::SafeArrow) {

                            Expr* unwrappedBase = unwrapGroup(postfix.base.get());
                            std::string baseName = "";
                            if (unwrappedBase && unwrappedBase->kind == Expr::Kind::Ident &&
                                unwrappedBase->ident && !unwrappedBase->ident->parts.empty()) {
                                baseName = unwrappedBase->ident->parts.back();
                            }

                            std::string currentReg = "%" + baseName;
                            std::string currentType = "i32";
                            std::string varReg, varType;
                            if (!baseName.empty() && lookupVar(baseName, varReg, varType)) {
                                currentReg = varReg;
                                currentType = varType;
                            } else if (!baseName.empty() && symbolTypes_.find(baseName) != symbolTypes_.end()) {
                                currentType = symbolTypes_[baseName];
                            } else if (unwrappedBase && unwrappedBase->kind == Expr::Kind::Unary &&
                                       unwrappedBase->unary.op == UnaryOp::Deref) {
                                currentReg = emitExpr(unwrappedBase->unary.expr.get());
                                std::string pStruct = resolvePointeeStructType(unwrappedBase->unary.expr.get());
                                currentType = pStruct.empty() ? getPointeeType(unwrappedBase->unary.expr.get()) : pStruct;
                            } else if (unwrappedBase) {
                                currentReg = emitExpr(unwrappedBase);
                                currentType = getExprType(unwrappedBase);
                            }

                            if (currentType == "ptr" && pointerStructTypes_.count(baseName)) {
                                std::string ptrLoaded = freshTemp();
                                out_ << "    " << ptrLoaded << " = load ptr " << currentReg << "\n";
                                currentReg = ptrLoaded;
                                currentType = pointerStructTypes_[baseName];
                            }

                            for (size_t i = 0; i + 1 < postfix.ops.size(); ++i) {
                                const auto& op = postfix.ops[i];
                                if (op.kind == Expr::PostfixOp::Kind::MemberAccess ||
                                    op.kind == Expr::PostfixOp::Kind::Arrow ||
                                    op.kind == Expr::PostfixOp::Kind::SafeArrow) {
                                    std::string segStruct = currentType;
                                    /*if ((op.kind == Expr::PostfixOp::Kind::Arrow || op.kind == Expr::PostfixOp::Kind::SafeArrow) &&
                                        currentType == "ptr" && pointerStructTypes_.count(baseName)) {
                                        std::string ptrVal = freshTemp();
                                        out_ << "    " << ptrVal << " = load ptr " << currentReg << "\n";
                                        currentReg = ptrVal;
                                        segStruct = pointerStructTypes_[baseName];
                                    }*/
                                    if (currentType == "ptr" && pointerStructTypes_.count(baseName)) {
                                        segStruct = pointerStructTypes_[baseName];
                                        currentType = segStruct;
                                    }

                                    int fieldIndex = 0;
                                    std::string fieldType = "i32";
                                    if (structDefinitions_.count(segStruct)) {
                                        for (const auto& f : structDefinitions_[segStruct].fields) {
                                            if (f.name == op.name) {
                                                fieldIndex = f.index;
                                                fieldType = f.type;
                                                break;
                                            }
                                        }
                                    }

                                    std::string fieldAddr = freshTemp();
                                    out_ << "    " << fieldAddr << " = field addr " << segStruct << ", " << currentReg << ", " << fieldIndex << "\n";
                                    if (fieldType.rfind("[", 0) == 0) {
                                        currentReg = fieldAddr;
                                        currentType = fieldType;
                                    } else {
                                        std::string fVal = freshTemp();
                                        out_ << "    " << fVal << " = load " << fieldType << " " << fieldAddr << "\n";
                                        currentReg = fVal;
                                        currentType = fieldType;
                                    }
                                } else if (op.kind == Expr::PostfixOp::Kind::Index) {
                                    std::string idxVal = emitExpr(op.index.get());
                                    std::string elemType = getIndexElemType(baseName, currentType);
                                    std::string elemAddr = freshTemp();
                                    out_ << "    " << elemAddr << " = elem addr " << elemType << " " << currentReg << ", " << idxVal << "\n";
                                    if (elemType == "ptr") {
                                        std::string eVal = freshTemp();
                                        out_ << "    " << eVal << " = load ptr " << elemAddr << "\n";
                                        currentReg = eVal;
                                        currentType = "ptr";
                                    } else {
                                        currentReg = elemAddr;
                                        currentType = elemType;
                                    }
                                }
                            }

                            std::string targetStruct = currentType;
                            /*if ((lastOp.kind == Expr::PostfixOp::Kind::Arrow || lastOp.kind == Expr::PostfixOp::Kind::SafeArrow) &&
                                currentType == "ptr" && pointerStructTypes_.count(baseName)) {
                                std::string ptrVal = freshTemp();
                                out_ << "    " << ptrVal << " = load ptr " << currentReg << "\n";
                                currentReg = ptrVal;
                                targetStruct = pointerStructTypes_[baseName];
                            }*/
                            if (currentType == "ptr" && pointerStructTypes_.count(baseName)) {
                                targetStruct = pointerStructTypes_[baseName];
                                currentType = targetStruct;
                            }

                            int fieldIndex = 0;
                            std::string fieldType = "i32";
                            if (structDefinitions_.find(targetStruct) != structDefinitions_.end()) {
                                const auto& sInfo = structDefinitions_[targetStruct];
                                for (const auto& f : sInfo.fields) {
                                    if (f.name == lastOp.name) {
                                        fieldIndex = f.index;
                                        fieldType = f.type;
                                        break;
                                    }
                                }
                            }

                            std::string fieldAddrRes = freshTemp();
                            out_ << "    " << fieldAddrRes << " = field addr " << targetStruct << ", " << currentReg << ", " << fieldIndex << "\n";
                            out_ << "    store " << fieldType << " " << rhsVal << ", " << fieldAddrRes << "\n";
                            return rhsVal;
                        }
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

                    std::string varReg, varType = "i32";
                    std::string lhsName = expr->assign.lhs->ident->parts.back();
                    if (symbolTypes_.find(lhsName) != symbolTypes_.end()) {
                        varType = symbolTypes_[lhsName];
                    }
                    if (lookupVar(lhsName, varReg, varType)) {
                        out_ << "    store " + varType + " " << rhsVal << ", " << varReg << "\n";
                    } else {
                        std::string varName = "%" + lhsName;
                        out_ << "    store " + varType + " " << rhsVal << ", " << varName << "\n";
                    }
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
                        if (expr->unary.expr && expr->unary.expr->kind == Expr::Kind::Postfix) {
                            std::string ignoredType;
                            return emitFieldAddr(expr->unary.expr.get(), ignoredType);
                        }
                        if (expr->unary.expr->kind == Expr::Kind::Ident && 
                            expr->unary.expr->ident) {
                            if (expr->unary.expr->ident->is_absolute) {
                                std::string globalName = flattenQualifiedName(expr->unary.expr->ident.get());
                                std::string addrRes = freshTemp();
                                out_ << "    " << addrRes << " = global addr @" << globalName << "\n";
                                return addrRes;
                            }
                            if (!expr->unary.expr->ident->parts.empty()) {
                                std::string varName = expr->unary.expr->ident->parts.back();
                                std::string varReg, varType;
                                // Gunakan lookupVar untuk mendapatkan register stack alloca yang valid (misal: %t_13)
                                if (lookupVar(varName, varReg, varType)) {
                                    return varReg;
                                }
                                return "%" + varName;
                            }
                        }
                        return operandVal;
                    }

                    case UnaryOp::Deref: {
                        std::string loadType = getPointeeType(expr->unary.expr.get());
                        out_ << "    " << res << " = load " << loadType << " " << operandVal << "\n";
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

            case Expr::Kind::Intrinsic: {
                std::string callRes = freshTemp();
                std::string retType = "i32";
                if (expr->intrinsic.type) {
                    retType = emitType(expr->intrinsic.type.get());
                }
                out_ << "    " << callRes << " = " << expr->intrinsic.name << " " << retType;
                for (size_t i = 0; i < expr->intrinsic.args.size(); ++i) {
                    out_ << " " << emitExpr(expr->intrinsic.args[i].get());
                    if (i + 1 < expr->intrinsic.args.size()) out_ << ",";
                }
                out_ << "\n";
                return callRes;
            }

            case Expr::Kind::Postfix: {
                    Expr* unwrappedBase = unwrapGroup(expr->postfix.base.get());
                    std::string baseName = "";
                    if (unwrappedBase && unwrappedBase->kind == Expr::Kind::Ident &&
                        unwrappedBase->ident && !unwrappedBase->ident->parts.empty()) {
                        baseName = unwrappedBase->ident->parts.back();
                    }

                    std::string currentReg = "%" + baseName;
                    std::string currentType = "i32";
                    std::string varReg, varType;
                    if (!baseName.empty() && lookupVar(baseName, varReg, varType)) {
                        currentReg = varReg;
                        currentType = varType;
                    } else if (!baseName.empty() && symbolTypes_.find(baseName) != symbolTypes_.end()) {
                        currentType = symbolTypes_[baseName];
                    } else if (unwrappedBase && unwrappedBase->kind == Expr::Kind::Unary &&
                        unwrappedBase->unary.op == UnaryOp::Deref) {
                        std::string pStruct = resolvePointeeStructType(unwrappedBase->unary.expr.get());
                        bool followedByField = !expr->postfix.ops.empty() &&
                            (expr->postfix.ops[0].kind == Expr::PostfixOp::Kind::MemberAccess ||
                             expr->postfix.ops[0].kind == Expr::PostfixOp::Kind::Arrow ||
                             expr->postfix.ops[0].kind == Expr::PostfixOp::Kind::SafeArrow);
                        if (followedByField && !pStruct.empty()) {
                            currentReg = emitExpr(unwrappedBase->unary.expr.get());
                            currentType = pStruct;
                        } else {
                            currentReg = emitExpr(unwrappedBase);
                            currentType = pStruct.empty() ? getPointeeType(unwrappedBase->unary.expr.get()) : pStruct;
                        }
                    } else if (unwrappedBase) {
                        currentReg = emitExpr(unwrappedBase);
                        currentType = getExprType(unwrappedBase);
                    }
                if (currentType == "ptr" && !expr->postfix.ops.empty() &&
                    expr->postfix.ops[0].kind == Expr::PostfixOp::Kind::Index) {
                    std::string loadedPtr = freshTemp();
                    out_ << "    " << loadedPtr << " = load ptr " << currentReg << "\n";
                    currentReg = loadedPtr;
                }

                for (size_t i = 0; i < expr->postfix.ops.size(); ++i) {
                    const auto& op = expr->postfix.ops[i];
                    bool hasNextOp = (i + 1 < expr->postfix.ops.size());
                    const Expr::PostfixOp* nextOp = hasNextOp ? &expr->postfix.ops[i + 1] : nullptr;

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
                        if (retType != "void")
                            out_ << "    " << callRes << " = call " << retType << " @" << baseName << "(";
                        else
                            out_ << "    call " << retType << " @" << baseName << "(";
                        for (size_t a = 0; a < argVals.size(); ++a) {
                            out_ << argVals[a];
                            if (a + 1 < argVals.size()) out_ << ", ";
                        }
                        out_ << ")\n";
                        return callRes;
                    } else if (op.kind == Expr::PostfixOp::Kind::Index) {
                        std::string indexVal = emitExpr(op.index.get());
                        std::string elemType = getIndexElemType(baseName, currentType);
                        std::string elemAddrRes = freshTemp();
                        out_ << "    " << elemAddrRes << " = elem addr " << elemType << " " << currentReg << ", " << indexVal << "\n";

                        bool nextIsField = nextOp && (
                            nextOp->kind == Expr::PostfixOp::Kind::MemberAccess ||
                            nextOp->kind == Expr::PostfixOp::Kind::Arrow ||
                            nextOp->kind == Expr::PostfixOp::Kind::SafeArrow
                        );

                        if (nextIsField && structDefinitions_.count(elemType)) {
                            currentReg = elemAddrRes;
                            currentType = elemType;
                        } else {
                            std::string loadRes = freshTemp();
                            out_ << "    " << loadRes << " = load " << elemType << " " << elemAddrRes << "\n";
                            currentReg = loadRes;
                            currentType = elemType;
                            /*if (elemType == "ptr" && !baseName.empty() && pointerStructTypes_.count(baseName)) {
                                currentType = pointerStructTypes_[baseName];
                            } else if (elemType == "ptr" && !baseName.empty() && symbolElementTypes_.count(baseName) &&
                                structDefinitions_.count(symbolElementTypes_[baseName])) {
                                currentType = symbolElementTypes_[baseName];
                            } else {
                                std::string loadRes = freshTemp();
                                out_ << "    " << loadRes << " = load " << elemType << " " << elemAddrRes << "\n";
                                currentReg = loadRes;
                                currentType = elemType;
                            }*/

                        }
                    } else if (op.kind == Expr::PostfixOp::Kind::MemberAccess ||
                               op.kind == Expr::PostfixOp::Kind::Arrow ||
                               op.kind == Expr::PostfixOp::Kind::SafeArrow) {
                        std::string structType = currentType;
                        if(currentType == "ptr") {
                            if (!baseName.empty() && pointerStructTypes_.count(baseName)) {
                                structType = pointerStructTypes_[baseName];
                            } else if (!baseName.empty() && symbolElementTypes_.count(baseName) &&
                                       structDefinitions_.count(symbolElementTypes_[baseName])) {
                                structType = symbolElementTypes_[baseName];
                            } else if (!baseName.empty() && paramElementTypes_.count(baseName) &&
                                       structDefinitions_.count(paramElementTypes_[baseName])) {
                                structType = paramElementTypes_[baseName];
                            }
                        }

                        int fieldIndex = 0;
                        std::string fieldType = "i32";
                        if (structDefinitions_.count(structType)) {
                            const auto& sInfo = structDefinitions_[structType];
                            for (const auto& f : sInfo.fields) {
                                if (f.name == op.name) {
                                    fieldIndex = f.index;
                                    fieldType = f.type;
                                    break;
                                }
                            }
                        }

                        std::string fieldAddrRes = freshTemp();
                        out_ << "    " << fieldAddrRes << " = field addr " << structType << ", " << currentReg << ", " << fieldIndex << "\n";

                        if (fieldType.rfind("[", 0) == 0) {
                            currentReg = fieldAddrRes;
                            currentType = fieldType;
                        } else {
                            std::string loadRes = freshTemp();
                            out_ << "    " << loadRes << " = load " << fieldType << " " << fieldAddrRes << "\n";
                            currentReg = loadRes;
                            currentType = fieldType;
                        }
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
