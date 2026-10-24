#pragma once

// **The Ast.h a function's body is, as text** (M2.11, doc-cxx3/M2-PLAN.md): every field a backend reads, a node a
// line, so two bodies built two ways can be told apart by a diff. -dump=ast writes it; -check=adapted compares with it.

#include "Ast.h"

#include <string>

// One function: its symbol, frame, parameters, and its body.
void dumpFunction(const Function &f, std::string &out);
// A body alone.
void dumpStmt(const Stmt &s, std::string &out);
