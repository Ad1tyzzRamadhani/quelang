#pragma once

#include "ast.hpp"

#include <sstream>
#include <string>

class SymEmitter {
public:
    explicit SymEmitter(const FileProgram* f)
        : file(f) {}

    std::string emit() {

        out << "SYM1\n\n";

        for (const auto& decl : file->decls) {

            if (auto* fwd =
                dynamic_cast<ForwardDecl*>(decl.get())) {

                emitForward(fwd);
                continue;
            }

            if (auto* fn =
                dynamic_cast<Function*>(decl.get())) {

                emitFunction(fn);
                continue;
            }

            if (auto* en =
                dynamic_cast<EnumDef*>(decl.get())) {

                emitEnum(en);
                continue;
            }

            if (auto* st =
                dynamic_cast<Stmt*>(decl.get())) {

                if (st->kind == Stmt::Kind::VarDecl) {
                    emitVar(st);
                }

                continue;
            }
        }

        return out.str();
    }

private:
    const FileProgram* file;
    std::ostringstream out;

private:

    // =====================================================
    // Helpers
    // =====================================================

    std::string visToString(Visibility v) {

        switch (v) {

            case Visibility::Public:
                return "public";

            case Visibility::Protect:
                return "protect";

            default:
                return "private";
        }
    }

    std::string qname(const QualifiedName* q) {

        if (!q)
            return "<anonymous>";

        std::ostringstream ss;

        for (size_t i = 0; i < q->parts.size(); ++i) {

            if (i)
                ss << "::";

            ss << q->parts[i];
        }

        return ss.str();
    }

    std::string literalToString(const Expr* e) {

        if (!e || !e->literal)
            return "0";

        return e->literal->value;
    }

    std::string typeToString(const Type* t) {

        std::ostringstream ss;

        // qualifiers

        for (auto q : t->qualifiers) {

            switch (q) {

                case TypeQualifier::Const:
                    ss << "const ";
                    break;

                case TypeQualifier::Volatile:
                    ss << "volatile ";
                    break;
            }
        }

        // base

        ss << qname(t->base.get());

        // modifiers

        for (const auto& mod : t->modifiers) {

            switch (mod.kind) {

                case TypeModifier::Kind::Pointer:

                    ss << "*";
                    break;

                case TypeModifier::Kind::Reference:

                    ss << "&";
                    break;

                case TypeModifier::Kind::Array: {

                    ss << "[";

                    if (!mod.array_dims.empty()) {

                        auto* dim =
                            mod.array_dims[0].get();

                        if (
                            dim &&
                            dim->literal
                        ) {
                            ss << dim->literal->value;
                        }
                    }

                    ss << "]";
                    break;
                }

                case TypeModifier::Kind::FuncPtr: {

                    ss << " fn(";

                    for (
                        size_t i = 0;
                        i < mod.func_params.size();
                        ++i
                    ) {

                        if (i)
                            ss << ",";

                        ss << typeToString(
                            mod.func_params[i].get()
                        );
                    }

                    ss << ")";
                    break;
                }
            }
        }

        return ss.str();
    }

    // =====================================================
    // ForwardDecl
    // =====================================================

    void emitForward(const ForwardDecl* f) {

        switch (f->kind) {

            case ForwardDecl::Kind::Struct: {

                out
                    << "struct "
                    << qname(f->name.get())
                    << "\n";

                out
                    << "vis "
                    << visToString(f->visibility)
                    << "\n";

                out
                    << "opaque true\n";

                out
                    << "end\n\n";

                break;
            }

            case ForwardDecl::Kind::Class: {

                out
                    << "class "
                    << qname(f->name.get())
                    << "\n";

                out
                    << "vis "
                    << visToString(f->visibility)
                    << "\n";

                out
                    << "opaque true\n";

                out
                    << "end\n\n";

                break;
            }

            case ForwardDecl::Kind::Function: {

                out
                    << "func "
                    << qname(f->name.get())
                    << "\n";

                out
                    << "vis "
                    << visToString(f->visibility)
                    << "\n";

                if (f->return_type) {

                    out
                        << "return "
                        << typeToString(
                            f->return_type.get()
                        )
                        << "\n";
                }

                for (const auto& p : f->params) {

                    out
                        << "param "
                        << typeToString(
                            p.get()
                        )
                        << "\n";
                }

                if (f->is_static)
                    out << "static true\n";

                out
                    << "opaque true\n";

                out
                    << "end\n\n";

                break;
            }
        }
    }

    // =====================================================
    // Enum
    // =====================================================

    void emitEnum(const EnumDef* en) {

        out
            << "enum "
            << qname(en->name.get())
            << "\n";

        out
            << "vis "
            << visToString(en->visibility)
            << "\n";

        if (en->underlying_type) {

            out
                << "type "
                << typeToString(
                    en->underlying_type.get()
                )
                << "\n";
        }

        // opaque enum

        if (en->items.empty()) {

            out
                << "opaque true\n";

            out
                << "end\n\n";

            return;
        }

        int64_t auto_value = 0;

        for (const auto& item : en->items) {

            int64_t value = auto_value;

            if (item.value) {

                value = std::stoll(
                    literalToString(
                        item.value.get()
                    ),
                    nullptr,
                    0
                );
            }

            out
                << "item "
                << item.name
                << " "
                << value
                << "\n";

            auto_value = value + 1;
        }

        out
            << "end\n\n";
    }

    // =====================================================
    // Function
    // =====================================================

    void emitFunction(const Function* fn) {

        out
            << "func "
            << qname(fn->name.get())
            << "\n";

        out
            << "vis "
            << visToString(fn->visibility)
            << "\n";

        out
            << "return "
            << typeToString(
                fn->return_type.get()
            )
            << "\n";

        for (const auto& param : fn->params) {

            out
                << "param "
                << typeToString(
                    param.type.get()
                );

            if (!param.name.empty()) {
                out << " " << param.name;
            }

            out << "\n";
        }

        if (fn->is_static)
            out << "static true\n";

        out
            << "end\n\n";
    }

    // =====================================================
    // Variable
    // =====================================================

    void emitVar(const Stmt* st) {

        const auto& vd = st->var_decl;

        for (const auto& item : vd.items) {

            switch (vd.storage) {

                case StorageKind::Static:

                    out << "static ";
                    break;

                default:

                    out << "global ";
                    break;
            }

            out
                << qname(item.name.get())
                << "\n";

            out
                << "type "
                << typeToString(
                    vd.type.get()
                )
                << "\n";

            out
                << "end\n\n";
        }
    }
};
