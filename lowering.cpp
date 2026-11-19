#include <memory>
#include <vector>
#include <string>
#include <unordered_set>
#include <stdexcept>

class LoweringEngine {
private:
    Type* current_return_type = nullptr;
    std::unordered_set<std::string> throwing_functions;
    int err_var_counter = 0;

    std::string nextErrVarName() {
        return "__err_slot_" + std::to_string(err_var_counter++);
    }

    bool isPointerLikeType(const Type* type) const {
        if (!type) return false;
        if (!type->modifiers.empty()) {
            const auto& lastMod = type->modifiers.back();
            if (lastMod.kind == TypeModifier::Kind::Pointer ||
                lastMod.kind == TypeModifier::Kind::Ref) {
                return true;
            }
        }
        if (type->base && !type->base->parts.empty()) {
            const std::string& baseName = type->base->parts.back();
            if (baseName == "ptr") return true;
        }
        return false;
    }

    std::unique_ptr<Type> cloneType(const Type* src) const {
        if (!src) return nullptr;
        auto dst = std::make_unique<Type>();
        dst->file = src->file;
        dst->line_start = src->line_start;
        dst->column_start = src->column_start;
        dst->modifiers = src->modifiers;

        if (src->base) {
            auto newBase = std::make_unique<QualifiedName>();
            newBase->parts = src->base->parts;
            dst->base = std::move(newBase);
        }
        return dst;
    }

    std::unique_ptr<Expr> makeIdentExpr(const std::string& name) const {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Ident;
        expr->ident = std::make_unique<QualifiedName>();
        expr->ident->parts.push_back(name);
        return expr;
    }

    std::unique_ptr<Expr> makeRefExpr(const std::string& name) const {
        auto refExpr = std::make_unique<Expr>();
        refExpr->kind = Expr::Kind::Unary;
        refExpr->unary.op = UnaryOp::Ref;
        refExpr->unary.expr = makeIdentExpr(name);
        return refExpr;
    }

    std::unique_ptr<Expr> makeNullLiteral() const {
        auto nullLit = std::make_unique<Expr>();
        nullLit->kind = Expr::Kind::Literal;
        nullLit->literal = std::make_unique<Literal>();
        nullLit->literal->kind = Literal::Kind::Null;
        nullLit->literal->value = "null";
        return nullLit;
    }

    // Catatan: pastikan AST Expr Anda mendukung clone jika init dipakai di 2 tempat
    // (inisialisasi awal dan perbandingan !=). Jika belum ada cloneExpr(),
    // buat fungsi helper cloneExpr(const Expr*) sesuai definisi struct Expr Anda.
    std::unique_ptr<Expr> cloneExprFallbackOrInit(Expr* expr) {
        if (!expr) return makeNullLiteral();
        // Ganti dengan cloneExpr(expr) yang sesuai dengan definisi Expr Anda
        return nullptr;
    }

public:
    void lowerProgram(FileProgram* program) {
        if (!program) return;

        // Pass 1: Kumpulkan semua fungsi yang memiliki klausa throws
        for (auto& node : program->decls) {
            if (auto func = dynamic_cast<Function*>(node.get())) {
                if (func->is_throws && func->name && !func->name->parts.empty()) {
                    throwing_functions.insert(func->name->parts.back());
                }
            }
        }

        // Pass 2: Lowering deklarasi fungsi
        for (auto& node : program->decls) {
            if (auto func = dynamic_cast<Function*>(node.get())) {
                lowerFunction(func);
            }
        }
    }

private:
    void lowerFunction(Function* func) {
        bool was_throws = func->is_throws && func->throws_type;

        if (was_throws) {
            Function::Param errorParam;
            errorParam.type = cloneType(func->throws_type.get());

            TypeModifier mod;
            mod.kind = TypeModifier::Kind::Pointer;
            errorParam.type->modifiers.push_back(std::move(mod));

            errorParam.name = "__throwed";
            errorParam.init = nullptr;

            func->params.push_back(std::move(errorParam));
        }

        current_return_type = func->return_types.get();

        if (func->body) {
            lowerStmt(func->body.get());
        }

        if (was_throws) {
            func->is_throws = false;
            func->throws_type = nullptr;
        }
        current_return_type = nullptr;
    }

    std::unique_ptr<Expr> createDefaultReturnExpr(Type* type) {
        if (!type || !type->base || type->base->parts.empty()) {
            return nullptr;
        }

        const std::string& typeName = type->base->parts.back();

        if (typeName == "int" || typeName == "i32" || typeName == "i64" ||
            typeName == "u32" || typeName == "u64" || typeName == "byte" || typeName == "char") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::Number;
            expr->literal->value = "0";
            return expr;
        }

        if (typeName == "float" || typeName == "double" || typeName == "f32" || typeName == "f64") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::Float;
            expr->literal->value = "0.0";
            return expr;
        }

        if (typeName == "bool") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::False;
            expr->literal->value = "false";
            return expr;
        }

        auto structExpr = std::make_unique<Expr>();
        structExpr->kind = Expr::Kind::StructInit;

        auto copiedType = std::make_unique<Type>();
        if (type && type->base) {
            copiedType->file = type->file;
            copiedType->line_start = type->line_start;
            copiedType->column_start = type->column_start;

            auto newBase = std::make_unique<QualifiedName>();
            newBase->parts = type->base->parts;
            copiedType->base = std::move(newBase);
        }

        structExpr->struct_init.type_name = std::move(copiedType);
        structExpr->struct_init.fields.clear();
        return structExpr;
    }

    void lowerStmt(Stmt* stmt) {
        if (!stmt) return;

        switch (stmt->kind) {
            case Stmt::Kind::Block: {
                for (auto& s : stmt->block.stmts) {
                    lowerStmt(s.get());
                }
                break;
            }

            case Stmt::Kind::If: {
                if (stmt->if_stmt.then_block) {
                    lowerStmt(stmt->if_stmt.then_block.get());
                }
                for (auto& elif : stmt->if_stmt.elifs) {
                    if (elif.block) lowerStmt(elif.block.get());
                }
                if (stmt->if_stmt.else_block) {
                    lowerStmt(stmt->if_stmt.else_block.get());
                }
                break;
            }

            case Stmt::Kind::While: {
                if (stmt->while_stmt.body) lowerStmt(stmt->while_stmt.body.get());
                break;
            }

            case Stmt::Kind::DoStmt: {
                if (stmt->do_stmt.body) {
                    lowerStmt(stmt->do_stmt.body.get());
                }

                if (stmt->do_stmt.iscatch) {
                    std::string slotName = nextErrVarName();

                    // Tentukan tipe & nilai awal dari error_var (catch(T name = val))
                    Type* errTypePtr = stmt->do_stmt.error_var.type.get();
                    bool isPtrLike = isPointerLikeType(errTypePtr);

                    std::unique_ptr<Expr> initForSlot;
                    std::unique_ptr<Expr> sentinelForCheck;

                    if (!stmt->do_stmt.error_var.items.empty() && stmt->do_stmt.error_var.items[0].init) {
                        // Ambil nilai inisialisasi dari catch(T name = val)
                        // Pastikan Anda memiliki fungsi cloneExpr() agar AST tidak double-move
                        initForSlot = cloneExprFallbackOrInit(stmt->do_stmt.error_var.items[0].init.get());
                        sentinelForCheck = cloneExprFallbackOrInit(stmt->do_stmt.error_var.items[0].init.get());
                    } else if (isPtrLike) {
                        initForSlot = makeNullLiteral();
                        sentinelForCheck = makeNullLiteral();
                    } else {
                        initForSlot = createDefaultReturnExpr(errTypePtr);
                        sentinelForCheck = createDefaultReturnExpr(errTypePtr);
                    }

                    // Inject &__err_slot_N ke pemanggilan throwing_functions di dalam body
                    if (stmt->do_stmt.body &&
                        stmt->do_stmt.body->kind == Stmt::Kind::ExprStmt &&
                        stmt->do_stmt.body->expr_stmt &&
                        stmt->do_stmt.body->expr_stmt->kind == Expr::Kind::Postfix) {

                        auto& postfix = stmt->do_stmt.body->expr_stmt->postfix;
                        if (postfix.base && postfix.base->kind == Expr::Kind::Ident &&
                            postfix.base->ident && !postfix.base->ident->parts.empty()) {

                            std::string funcName = postfix.base->ident->parts.back();
                            if (throwing_functions.count(funcName)) {
                                for (auto& op : postfix.ops) {
                                    if (op.kind == Expr::PostfixOp::Kind::Call) {
                                        op.args.push_back(makeRefExpr(slotName));
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    auto desugaredBlock = std::make_unique<Stmt>();
                    desugaredBlock->kind = Stmt::Kind::Block;

                    // 1. T __err_slot_N = <init>;
                    auto varDeclStmt = std::make_unique<Stmt>();
                    varDeclStmt->kind = Stmt::Kind::VarDecl;
                    VarDecl vdecl;
                    vdecl.type = cloneType(errTypePtr);

                    VarDecl::Item item;
                    item.name = slotName;
                    item.init = std::move(initForSlot);
                    vdecl.items.push_back(std::move(item));
                    varDeclStmt->var_decl = std::move(vdecl);
                    desugaredBlock->block.stmts.push_back(std::move(varDeclStmt));

                    // 2. Eksekusi body asli
                    desugaredBlock->block.stmts.push_back(std::move(stmt->do_stmt.body));

                    // 3. Cek kondisi:
                    // - Jika pointer/ref: __err_slot_N != null
                    // - Jika value/stack: __err_slot_N != <nilai_awal>
                    auto ifStmt = std::make_unique<Stmt>();
                    ifStmt->kind = Stmt::Kind::If;

                    auto condExpr = std::make_unique<Expr>();
                    condExpr->kind = Expr::Kind::Binary;
                    condExpr->binary.op = BinaryOp::Ne;
                    condExpr->binary.lhs = makeIdentExpr(slotName);
                    condExpr->binary.rhs = isPtrLike ? makeNullLiteral() : std::move(sentinelForCheck);
                    ifStmt->if_stmt.cond = std::move(condExpr);

                    // 4. Blok catch: T name = __err_slot_N; <catch_body>
                    auto catchBlock = std::make_unique<Stmt>();
                    catchBlock->kind = Stmt::Kind::Block;

                    if (!stmt->do_stmt.error_var.items.empty()) {
                        stmt->do_stmt.error_var.items[0].init = makeIdentExpr(slotName);

                        auto errorVarStmt = std::make_unique<Stmt>();
                        errorVarStmt->kind = Stmt::Kind::VarDecl;
                        errorVarStmt->var_decl = std::move(stmt->do_stmt.error_var);
                        catchBlock->block.stmts.push_back(std::move(errorVarStmt));
                    }

                    if (stmt->do_stmt.catch_body) {
                        lowerStmt(stmt->do_stmt.catch_body.get());
                        catchBlock->block.stmts.push_back(std::move(stmt->do_stmt.catch_body));
                    }

                    ifStmt->if_stmt.then_block = std::move(catchBlock);
                    desugaredBlock->block.stmts.push_back(std::move(ifStmt));

                    *stmt = std::move(*desugaredBlock);
                }
                break;
            }

            case Stmt::Kind::SwitchCase: {
                for (auto& c : stmt->switch_stmt.cases) {
                    if (c.body) lowerStmt(c.body.get());
                }
                break;
            }

            case Stmt::Kind::Unsafe: {
                if (stmt->unsafe_stmt.stmt) lowerStmt(stmt->unsafe_stmt.stmt.get());
                break;
            }

            case Stmt::Kind::ThrowStmt: {
                // *__throwed = <expr>; return <default>;
                auto assignExpr = std::make_unique<Expr>();
                assignExpr->kind = Expr::Kind::Assign;
                assignExpr->assign.op = AssignOp::Assign;

                auto lhsDeref = std::make_unique<Expr>();
                lhsDeref->kind = Expr::Kind::Unary;
                lhsDeref->unary.op = UnaryOp::Deref;
                lhsDeref->unary.expr = makeIdentExpr("__throwed");

                assignExpr->assign.lhs = std::move(lhsDeref);
                assignExpr->assign.rhs = std::move(stmt->throw_stmt.expr);

                auto storeStmt = std::make_unique<Stmt>();
                storeStmt->kind = Stmt::Kind::ExprStmt;
                storeStmt->expr_stmt = std::move(assignExpr);

                auto returnStmt = std::make_unique<Stmt>();
                returnStmt->kind = Stmt::Kind::Return;
                returnStmt->return_values = createDefaultReturnExpr(current_return_type);

                stmt->kind = Stmt::Kind::Block;
                stmt->block.stmts.clear();
                stmt->block.stmts.push_back(std::move(storeStmt));
                stmt->block.stmts.push_back(std::move(returnStmt));
                break;
            }

            default:
                break;
        }
    }
};

void lowerAST(FileProgram* program) {
    LoweringEngine engine;
    engine.lowerProgram(program);
}
