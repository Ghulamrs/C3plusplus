#pragma once

#include "Backend.h"
#include "Regions.h"

#include <cstddef>
#include <string>
#include <vector>

class Source;

// **A generator that walks `Ast.h`**: Visitor's dispatch is held, not inherited, so the chain CodeGen <- Regions <-
// AstCodeGen keeps one polymorphic base a class, which is all cxx3 lays out on the Microsoft ABI (M3b.7 round 4 F).
class AstCodeGen : public Regions {
public:
    AstCodeGen() : dispatch_(this) {}
    Visitor &asVisitor() { return dispatch_; }

    virtual void visit(const Num &n) = 0;
    virtual void visit(const Var &n) = 0;
    virtual void visit(const Assign &n) = 0;
    virtual void visit(const Unary &n) = 0;
    virtual void visit(const Binary &n) = 0;
    virtual void visit(const Postfix &n) = 0;
    virtual void visit(const Call &n) = 0;
    virtual void visit(const Cast &n) = 0;
    virtual void visit(const StrLit &n) = 0;
    virtual void visit(const VaStart &n) = 0;
    virtual void visit(const VaArg &n) = 0;
    virtual void visit(const MemberAccess &n) = 0;
    virtual void visit(const ExprStmt &n) = 0;
    virtual void visit(const Return &n) = 0;
    virtual void visit(const Block &n) = 0;
    virtual void visit(const If &n) = 0;
    virtual void visit(const While &n) = 0;
    virtual void visit(const For &n) = 0;
    virtual void visit(const DoWhile &n) = 0;
    virtual void visit(const Switch &n) = 0;
    virtual void visit(const Case &n) = 0;
    virtual void visit(const Goto &n) = 0;
    virtual void visit(const Label &n) = 0;
    virtual void visit(const Conditional &n) = 0;
    virtual void visit(const Comma &n) = 0;
    virtual void visit(const Break &n) = 0;
    virtual void visit(const Continue &n) = 0;
    virtual void visit(const FuncletLeave &n) = 0;
    virtual void visit(const Try &n) = 0;

private:
    AstCodeGen(const AstCodeGen &);
    AstCodeGen &operator=(const AstCodeGen &);
    class Dispatch : public Visitor {
    public:
        explicit Dispatch(AstCodeGen *g) : g_(g) {}
        void visit(const Num &n) override { g_->visit(n); }
        void visit(const Var &n) override { g_->visit(n); }
        void visit(const Assign &n) override { g_->visit(n); }
        void visit(const Unary &n) override { g_->visit(n); }
        void visit(const Binary &n) override { g_->visit(n); }
        void visit(const Postfix &n) override { g_->visit(n); }
        void visit(const Call &n) override { g_->visit(n); }
        void visit(const Cast &n) override { g_->visit(n); }
        void visit(const StrLit &n) override { g_->visit(n); }
        void visit(const VaStart &n) override { g_->visit(n); }
        void visit(const VaArg &n) override { g_->visit(n); }
        void visit(const MemberAccess &n) override { g_->visit(n); }
        void visit(const ExprStmt &n) override { g_->visit(n); }
        void visit(const Return &n) override { g_->visit(n); }
        void visit(const Block &n) override { g_->visit(n); }
        void visit(const If &n) override { g_->visit(n); }
        void visit(const While &n) override { g_->visit(n); }
        void visit(const For &n) override { g_->visit(n); }
        void visit(const DoWhile &n) override { g_->visit(n); }
        void visit(const Switch &n) override { g_->visit(n); }
        void visit(const Case &n) override { g_->visit(n); }
        void visit(const Goto &n) override { g_->visit(n); }
        void visit(const Label &n) override { g_->visit(n); }
        void visit(const Conditional &n) override { g_->visit(n); }
        void visit(const Comma &n) override { g_->visit(n); }
        void visit(const Break &n) override { g_->visit(n); }
        void visit(const Continue &n) override { g_->visit(n); }
        void visit(const FuncletLeave &n) override { g_->visit(n); }
        void visit(const Try &n) override { g_->visit(n); }
    private:
        AstCodeGen *g_;
    };
    Dispatch dispatch_;
};

// **The statement visitors over `Ast.h`**, with the hooks only they write through; the regions, tables and blocks they
// fill are Regions', which the SSA selector fills too (M3b.7 round 4 B).
class Walker : public AstCodeGen {
public:
    void visit(const ExprStmt &n) override;
    void visit(const Block &n) override;
    void visit(const If &n) override;
    void visit(const While &n) override;
    void visit(const For &n) override;
    void visit(const DoWhile &n) override;
    void visit(const Switch &n) override;
    void visit(const Case &n) override;
    void visit(const Goto &n) override;
    void visit(const Label &n) override;
    void visit(const Conditional &n) override;
    void visit(const Comma &n) override;
    void visit(const Break &n) override;
    void visit(const FuncletLeave &n) override;
    void visit(const Continue &n) override;
    void visit(const Try &n) override;

    void setLineSource(const Source *s, const std::string &dir) override { holdLineSource(s, dir); }

protected:

    void msTryStatement(const Try &n);

    using AstCodeGen::markLine;
    void markLine(const Stmt &n);

    virtual void jump(const std::string &l) = 0;
    virtual void branchIfZero(const std::string &l) = 0;
    virtual void branchIfNotZero(const std::string &l) = 0;

    virtual void genTruth(const Expr &e) = 0;

    // FH3's scratch word set to -2 before the body's first call; see Walker.cpp.
    void storeUnwindHelpAtEntry(const Stmt &body);

    struct JumpTargets { std::string brk; std::string cont; };
    std::vector<JumpTargets> jumps_;
};
