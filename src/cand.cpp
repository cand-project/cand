// C& — P0.2 CFG-based flow-sensitive ownership analysis.
//
// Design: ownership state is attached to program points (CFG basic blocks)
// rather than to source-order statements. A standard worklist computes the
// least fixed point over a small finite lattice. Every heap-relevant
// operation classifies as SUPPORTED, KNOWN SAFE, KNOWN VIOLATION or
// UNSUPPORTED/INCOMPLETE; there is no "unknown but PASS" (ADR-0010).

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "clang/Analysis/CFG.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

namespace {

using clang::ASTConsumer;
using clang::ASTContext;
using clang::ArraySubscriptExpr;
using clang::AsmStmt;
using clang::BinaryOperator;
using clang::CFG;
using clang::CFGBlock;
using clang::CFGElement;
using clang::CFGStmt;
using clang::CallExpr;
using clang::CompoundLiteralExpr;
using clang::CompoundStmt;
using clang::ConditionalOperator;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::GotoStmt;
using clang::IfStmt;
using clang::IndirectGotoStmt;
using clang::MemberExpr;
using clang::ParmVarDecl;
using clang::RecursiveASTVisitor;
using clang::ReturnStmt;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Stmt;
using clang::StmtExpr;
using clang::SwitchStmt;
using clang::TranslationUnitDecl;
using clang::UnaryExprOrTypeTraitExpr;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::WhileStmt;
using clang::dyn_cast;
using clang::isa;

llvm::cl::OptionCategory CandCategory("cand options");
llvm::cl::opt<std::string> OutputFormat(
    "format",
    llvm::cl::desc("Output format: human|json"),
    llvm::cl::init("human"),
    llvm::cl::cat(CandCategory));

struct Location {
    std::string file;
    unsigned line = 0;
    unsigned column = 0;
};

bool sameLocation(const Location &a, const Location &b) {
    return a.file == b.file && a.line == b.line && a.column == b.column;
}

Location minLocation(const Location &a, const Location &b) {
    if (a.file.empty()) {
        return b;
    }
    if (b.file.empty()) {
        return a;
    }
    if (a.file != b.file) {
        return a.file < b.file ? a : b;
    }
    if (a.line != b.line) {
        return a.line < b.line ? a : b;
    }
    return a.column <= b.column ? a : b;
}

struct TraceEvent {
    std::string event;
    std::string state;
    Location location;
};

struct Finding {
    std::string id;
    std::string rule_id;
    std::string message;
    std::string repair_class;
    std::string certainty = "definite";
    std::string state_before;
    std::string object_id;
    Location primary;
    std::vector<TraceEvent> trace;
};

struct Unsupported {
    std::string kind;
    std::string symbol;
    Location primary;
};

class Collector {
public:
    // Findings are emitted during every worklist pass; the last emission for
    // a given site wins (states only move up the lattice, so "last" is the
    // most conservative and deterministic result for that program point).
    void addFinding(Finding finding) {
        const std::tuple<std::string, std::string, unsigned, unsigned, std::string>
            key{finding.id, finding.primary.file, finding.primary.line,
                finding.primary.column, finding.object_id};
        findings_[key] = std::move(finding);
    }

    void addUnsupported(Unsupported unsupported) {
        const std::tuple<std::string, std::string, unsigned, unsigned> key{
            unsupported.kind, unsupported.primary.file, unsupported.primary.line,
            unsupported.primary.column};
        if (!seen_unsupported_.insert(key).second) {
            return;
        }
        unsupported_.push_back(std::move(unsupported));
    }

    void noteFunction() { ++functions_analyzed_; }

    void noteTrackedHeapObjects(std::size_t count) {
        tracked_heap_objects_ += static_cast<unsigned>(count);
    }

    unsigned nextObjectId() { return next_object_id_++; }

    void finalize() {
        for (auto &entry : findings_) {
            findings_list_.push_back(std::move(entry.second));
        }
        findings_.clear();
        const auto by_location = [](const auto &a, const auto &b) {
            return std::tie(a.primary.file, a.primary.line, a.primary.column) <
                   std::tie(b.primary.file, b.primary.line, b.primary.column);
        };
        std::sort(findings_list_.begin(), findings_list_.end(), by_location);
        std::sort(unsupported_.begin(), unsupported_.end(), by_location);
    }

    bool hasFindings() const { return !findings_list_.empty(); }
    bool hasUnsupported() const { return !unsupported_.empty(); }

    int exitCode() const {
        if (hasFindings()) {
            return 1;
        }
        if (hasUnsupported()) {
            return 3;
        }
        return 0;
    }

    void printHuman() const {
        for (const auto &finding : findings_list_) {
            llvm::errs() << finding.primary.file << ':' << finding.primary.line << ':'
                         << finding.primary.column << ": error[" << finding.id << "] ("
                         << finding.certainty << "): " << finding.message << " ("
                         << finding.object_id << ")\n";
            for (const auto &event : finding.trace) {
                llvm::errs() << "  " << event.event << " -> " << event.state << " at "
                             << event.location.file << ':' << event.location.line << ':'
                             << event.location.column << '\n';
            }
        }
        for (const auto &unsupported : unsupported_) {
            llvm::errs() << unsupported.primary.file << ':' << unsupported.primary.line
                         << ':' << unsupported.primary.column
                         << ": note[CAND-U001]: P0 unsupported construct: "
                         << unsupported.kind << '\n';
        }
    }

    void printJson() const {
        llvm::json::Object root;
        root["schema"] = "cand.check/v1";
        root["cand_version"] = "0.1.0-dev";
        root["result"] =
            hasFindings() ? "fail" : (hasUnsupported() ? "incomplete" : "pass");
        root["safety_level"] = "p0-temporal-lifecycle";
        root["profile"] = "p0-semantic-core";

        llvm::json::Array findings;
        for (const auto &finding : findings_list_) {
            llvm::json::Object obj;
            obj["id"] = finding.id;
            obj["rule_id"] = finding.rule_id;
            obj["severity"] = "error";
            obj["safety_level"] = "cand1";
            obj["certainty"] = finding.certainty;
            obj["message_key"] = finding.id;
            obj["message"] = finding.message;
            obj["repair_class"] = finding.repair_class;
            obj["object_id"] = finding.object_id;
            if (!finding.state_before.empty()) {
                obj["state_before_access"] = finding.state_before;
            }
            obj["primary_location"] = locationJson(finding.primary);

            llvm::json::Array trace;
            for (const auto &event : finding.trace) {
                llvm::json::Object event_obj;
                event_obj["event"] = event.event;
                event_obj["state"] = event.state;
                event_obj["location"] = locationJson(event.location);
                trace.push_back(std::move(event_obj));
            }
            obj["state_trace"] = std::move(trace);
            findings.push_back(std::move(obj));
        }
        root["findings"] = std::move(findings);

        llvm::json::Array unsupported;
        for (const auto &item : unsupported_) {
            llvm::json::Object obj;
            obj["id"] = "CAND-U001";
            obj["kind"] = item.kind;
            if (!item.symbol.empty()) {
                obj["symbol"] = item.symbol;
            }
            obj["primary_location"] = locationJson(item.primary);
            unsupported.push_back(std::move(obj));
        }
        root["unsupported"] = std::move(unsupported);

        llvm::json::Object coverage;
        coverage["functions_analyzed"] = static_cast<std::int64_t>(functions_analyzed_);
        coverage["tracked_heap_objects"] =
            static_cast<std::int64_t>(tracked_heap_objects_);
        coverage["unsupported_ownership_operations"] =
            static_cast<std::int64_t>(unsupported_.size());
        root["coverage"] = std::move(coverage);

        llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
    }

private:
    static llvm::json::Object locationJson(const Location &location) {
        llvm::json::Object obj;
        obj["file"] = location.file;
        obj["line"] = static_cast<std::int64_t>(location.line);
        obj["column"] = static_cast<std::int64_t>(location.column);
        return obj;
    }

    std::map<std::tuple<std::string, std::string, unsigned, unsigned, std::string>,
             Finding>
        findings_;
    std::vector<Finding> findings_list_;
    std::vector<Unsupported> unsupported_;
    std::set<std::tuple<std::string, std::string, unsigned, unsigned>>
        seen_unsupported_;
    unsigned functions_analyzed_ = 0;
    unsigned tracked_heap_objects_ = 0;
    unsigned next_object_id_ = 1;
};

// ---------------------------------------------------------------------------
// Ownership lattice
// ---------------------------------------------------------------------------
//
//   Untracked  storage holds no tracked object on this path
//   Owned      object alive on every represented path
//   Dead       object destroyed on every represented path
//   MaybeDead  alive on some represented paths, destroyed on others
//   Unknown    C& cannot soundly model this storage's ownership state
//
// Order (bottom to top): Untracked/Owned/Dead < MaybeDead < Unknown.
// join is componentwise and deterministic.

enum class ObjectState { Untracked, Null, Owned, Dead, MaybeDead, Unknown };

const char *stateName(ObjectState state) {
    switch (state) {
    case ObjectState::Untracked:
        return "Untracked";
    case ObjectState::Null:
        return "Null";
    case ObjectState::Owned:
        return "Owned";
    case ObjectState::Dead:
        return "Dead";
    case ObjectState::MaybeDead:
        return "MaybeDead";
    case ObjectState::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

ObjectState joinState(ObjectState a, ObjectState b) {
    if (a == b) {
        return a;
    }
    if (a == ObjectState::Unknown || b == ObjectState::Unknown) {
        return ObjectState::Unknown;
    }
    if (a == ObjectState::Untracked || b == ObjectState::Untracked) {
        // Tracked on one path, never assigned (or released without a known
        // value) on another: ownership differs per path, so do not guess.
        return ObjectState::Unknown;
    }
    // Null joined with a live object is safe for destruction on both paths
    // (free(NULL) is a no-op), so the joined storage is treated as owned.
    if (a == ObjectState::Null && b == ObjectState::Owned) {
        return ObjectState::Owned;
    }
    if (b == ObjectState::Null && a == ObjectState::Owned) {
        return ObjectState::Owned;
    }
    // Null joined with a destroyed object: a later destruction is safe on
    // the null path and a violation on the destroyed path.
    if (a == ObjectState::Null || b == ObjectState::Null) {
        return ObjectState::MaybeDead;
    }
    // Any mixture of Owned / Dead / MaybeDead can be alive on one incoming
    // path and destroyed on another.
    return ObjectState::MaybeDead;
}

struct Binding {
    unsigned object_id = 0;
    ObjectState state = ObjectState::Untracked;
    Location allocation;
    Location destruction;
    bool destruction_known = false;

    bool operator==(const Binding &other) const {
        return object_id == other.object_id && state == other.state &&
               destruction_known == other.destruction_known &&
               sameLocation(allocation, other.allocation) &&
               sameLocation(destruction, other.destruction);
    }
};

Binding joinBinding(const Binding &a, const Binding &b) {
    Binding result;
    if (a.state == ObjectState::Untracked) {
        result.object_id = b.object_id;
    } else if (b.state == ObjectState::Untracked) {
        result.object_id = a.object_id;
    } else {
        result.object_id = std::min(a.object_id, b.object_id);
    }
    result.state = joinState(a.state, b.state);
    result.allocation = minLocation(a.allocation, b.allocation);
    result.destruction_known = a.destruction_known || b.destruction_known;
    if (a.destruction_known && b.destruction_known) {
        result.destruction = minLocation(a.destruction, b.destruction);
    } else if (a.destruction_known) {
        result.destruction = a.destruction;
    } else {
        result.destruction = b.destruction;
    }
    return result;
}

struct FlowState {
    // StorageId -> Binding. P0.2 fully supports local variable storage only;
    // member/array/pointee storage is reported as unsupported, never guessed.
    std::map<const VarDecl *, Binding> storages;

    bool operator==(const FlowState &other) const {
        return storages == other.storages;
    }
};

FlowState joinFlow(const FlowState &a, const FlowState &b) {
    FlowState result;
    for (const auto &entry : a.storages) {
        const auto it = b.storages.find(entry.first);
        result.storages[entry.first] =
            it == b.storages.end() ? joinBinding(entry.second, Binding{})
                                   : joinBinding(entry.second, it->second);
    }
    for (const auto &entry : b.storages) {
        if (a.storages.find(entry.first) == a.storages.end()) {
            result.storages[entry.first] = joinBinding(Binding{}, entry.second);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Per-function flow analysis
// ---------------------------------------------------------------------------

class FlowAnalyzer {
public:
    FlowAnalyzer(ASTContext &context, Collector &collector)
        : context_(context), source_manager_(context.getSourceManager()),
          collector_(collector) {}

    void analyze(const FunctionDecl &function) {
        const Stmt *body = function.getBody();
        if (body == nullptr) {
            return;
        }
        collector_.noteFunction();
        collectAllocationSites(body);

        std::unique_ptr<CFG> cfg =
            CFG::buildCFG(&function, const_cast<Stmt *>(body), &context_, CFG::BuildOptions());
        if (!cfg) {
            emitUnsupported(
                {"cfg-unavailable", "", location(function.getLocation())});
            return;
        }
        cfg_ = cfg.get();
        run();
        collector_.noteTrackedHeapObjects(bound_objects_.size());
    }

    // File-scope pointer initializers are reachable from every function but
    // owned by none; they can only be constant expressions in ISO C, but the
    // model gap is reported rather than silently ignored (ADR-0010).
    void analyzeGlobal(const VarDecl &var) {
        const Expr *init = var.getInit();
        if (init == nullptr || !var.getType()->isPointerType()) {
            return;
        }
        if (isAllocation(init)) {
            emitUnsupported({"allocation-to-untracked-storage:global", "",
                                       location(var.getLocation())});
        } else if (isNullConstant(init)) {
            /* KNOWN SAFE */
        } else {
            checkPointerValueSource(init);
        }
    }

private:
    using StorageId = const VarDecl *;

    // ---- locations and predicates -------------------------------------

    Location location(SourceLocation loc) const {
        loc = source_manager_.getExpansionLoc(loc);
        const auto presumed = source_manager_.getPresumedLoc(loc);
        if (!presumed.isValid()) {
            return {"<unknown>", 0, 0};
        }
        return {presumed.getFilename(), presumed.getLine(), presumed.getColumn()};
    }

    const VarDecl *resolveVar(const Expr *expr) const {
        if (expr == nullptr) {
            return nullptr;
        }
        expr = expr->IgnoreParenCasts();
        const auto *ref = dyn_cast<DeclRefExpr>(expr);
        if (ref == nullptr) {
            return nullptr;
        }
        return dyn_cast<VarDecl>(ref->getDecl());
    }

    const CallExpr *asCall(const Expr *expr) const {
        if (expr == nullptr) {
            return nullptr;
        }
        return dyn_cast<CallExpr>(expr->IgnoreParenCasts());
    }

    bool isNamedCall(const CallExpr &call, llvm::StringRef name) const {
        const FunctionDecl *callee = call.getDirectCallee();
        return callee != nullptr && callee->getNameAsString() == name;
    }

    bool isAllocatorCall(const CallExpr &call) const {
        return isNamedCall(call, "malloc") || isNamedCall(call, "calloc");
    }

    bool isAllocation(const Expr *expr) const {
        const CallExpr *call = asCall(expr);
        return call != nullptr && isAllocatorCall(*call);
    }

    bool isNullConstant(const Expr *expr) const {
        if (expr == nullptr) {
            return false;
        }
        return expr->IgnoreParenCasts()->isNullPointerConstant(
            context_, Expr::NPC_ValueDependentIsNotNull);
    }

    const CallExpr *asUnknownPointerCall(const Expr *expr) const {
        if (expr == nullptr) {
            return nullptr;
        }
        expr = expr->IgnoreParenCasts();
        const auto *call = dyn_cast<CallExpr>(expr);
        if (call == nullptr) {
            return nullptr;
        }
        if (!call->getType()->isPointerType()) {
            return nullptr;
        }
        if (isAllocatorCall(*call)) {
            return nullptr;
        }
        return call;
    }

    bool containsUnknownPointerCall(const Expr *expr) const {
        if (expr == nullptr) {
            return false;
        }
        if (asUnknownPointerCall(expr) != nullptr) {
            return true;
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsUnknownPointerCall(child_expr)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool containsAllocationCall(const Expr *expr) const {
        if (expr == nullptr) {
            return false;
        }
        if (const auto *call = asCall(expr)) {
            if (isAllocatorCall(*call)) {
                return true;
            }
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsAllocationCall(child_expr)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool isLocalStackOrigin(const Expr *expr) const {
        if (expr == nullptr) {
            return false;
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *addr = dyn_cast<UnaryOperator>(expr)) {
            if (addr->getOpcode() == clang::UO_AddrOf) {
                const Expr *target = addr->getSubExpr()->IgnoreParenCasts();
                const VarDecl *var = resolveVar(target);
                if (var == nullptr) {
                    if (const auto *member = dyn_cast<MemberExpr>(target)) {
                        var = resolveVar(member->getBase());
                    }
                }
                if (var != nullptr && var->hasLocalStorage() &&
                    !isa<ParmVarDecl>(var)) {
                    return true;
                }
            }
        }
        if (const VarDecl *var = resolveVar(expr)) {
            if (var->hasLocalStorage() && !isa<ParmVarDecl>(var) &&
                var->getType()->isArrayType()) {
                return true;
            }
        }
        if (const auto *literal = dyn_cast<CompoundLiteralExpr>(expr)) {
            if (!literal->isFileScope()) {
                return true;
            }
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (isLocalStackOrigin(child_expr)) {
                    return true;
                }
            }
        }
        return false;
    }

    const Binding *bindingFor(const VarDecl *var, const FlowState &state) const {
        if (var == nullptr) {
            return nullptr;
        }
        const auto it = state.storages.find(var);
        return it == state.storages.end() ? nullptr : &it->second;
    }

    bool containsTrackedStorage(const Expr *expr, const FlowState &state) const {
        if (expr == nullptr) {
            return false;
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            const auto *var = dyn_cast<VarDecl>(ref->getDecl());
            return bindingFor(var, state) != nullptr;
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsTrackedStorage(child_expr, state)) {
                    return true;
                }
            }
        }
        return false;
    }

    std::string unknownPointerSymbol(const CallExpr &call) const {
        if (const FunctionDecl *callee = call.getDirectCallee()) {
            return callee->getNameAsString();
        }
        return "indirect";
    }

    std::string untrackedStorageKind(const Expr *lhs) const {
        const Expr *expr = lhs->IgnoreParenCasts();
        if (isa<MemberExpr>(expr)) {
            return "struct-member";
        }
        if (isa<ArraySubscriptExpr>(expr)) {
            return "array-element";
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            if (unary->getOpcode() == clang::UO_Deref) {
                return "pointee";
            }
        }
        return "unknown";
    }

    // ---- emission ------------------------------------------------------

    // Diagnostics are emitted only in the post-convergence pass, so their
    // content reflects the final fixed point rather than an intermediate
    // worklist iteration.
    void emitUnsupported(Unsupported unsupported) {
        if (emitting_) {
            collector_.addUnsupported(std::move(unsupported));
        }
    }

    void emitFinding(Finding finding) {
        if (emitting_) {
            collector_.addFinding(std::move(finding));
        }
    }

    void noteUnknownPointerCall(const CallExpr &call) {
        Unsupported unsupported;
        unsupported.kind =
            "unknown-pointer-return-ownership:" + unknownPointerSymbol(call);
        unsupported.symbol = unknownPointerSymbol(call);
        unsupported.primary = location(call.getExprLoc());
        emitUnsupported(std::move(unsupported));
    }

    void noteUnknownPointerCallIn(const Expr *expr) {
        if (const CallExpr *call = asUnknownPointerCall(expr)) {
            noteUnknownPointerCall(*call);
        }
    }

    void checkPointerValueSource(const Expr *init) {
        if (const CallExpr *call = asUnknownPointerCall(init)) {
            noteUnknownPointerCall(*call);
            return;
        }
        if (containsUnknownPointerCall(init)) {
            for (const Stmt *child : init->children()) {
                if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                    checkPointerValueSource(child_expr);
                }
            }
        }
    }

    void markUnsupported(const Stmt &stmt, llvm::StringRef kind) {
        emitUnsupported({kind.str(), "", location(stmt.getBeginLoc())});
    }

    std::string objectName(unsigned id) const {
        return "obj:" + std::to_string(id);
    }

    void reportUseAfterDestroy(const Binding &binding, SourceLocation use_loc) {
        const bool definite = binding.state == ObjectState::Dead;
        Finding finding;
        finding.id = "CAND-T002";
        finding.rule_id = "cand1.no-use-after-death";
        finding.message = definite ? "use after object destruction"
                                   : "possible use after object destruction";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.certainty = definite ? "definite" : "possible";
        finding.state_before = stateName(binding.state);
        finding.object_id = objectName(binding.object_id);
        finding.primary = location(use_loc);
        finding.trace.push_back({"allocation", "Owned", binding.allocation});
        if (binding.destruction_known) {
            finding.trace.push_back({definite ? "destruction" : "conditional_destruction",
                                     definite ? "Dead" : "MaybeDead",
                                     binding.destruction});
        }
        finding.trace.push_back(
            {"access", stateName(binding.state), location(use_loc)});
        emitFinding(std::move(finding));
    }

    void reportDoubleDestroy(const Binding &binding, SourceLocation destroy_loc,
                             bool definite) {
        Finding finding;
        finding.id = "CAND-T003";
        finding.rule_id = "cand1.single-destruction";
        finding.message = definite ? "object destroyed more than once"
                                   : "possible double destruction on some path";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.certainty = definite ? "definite" : "possible";
        finding.state_before = stateName(binding.state);
        finding.object_id = objectName(binding.object_id);
        finding.primary = location(destroy_loc);
        finding.trace.push_back({"allocation", "Owned", binding.allocation});
        if (binding.destruction_known) {
            finding.trace.push_back({definite ? "first_destruction"
                                              : "conditional_destruction",
                                     definite ? "Dead" : "MaybeDead",
                                     binding.destruction});
        }
        finding.trace.push_back(
            {"repeated_destruction", stateName(binding.state), location(destroy_loc)});
        emitFinding(std::move(finding));
    }

    // ---- transfer functions -------------------------------------------

    void checkAccess(const Expr *pointer_expr, SourceLocation access_loc,
                     const FlowState &state) {
        if (asUnknownPointerCall(pointer_expr) != nullptr) {
            noteUnknownPointerCallIn(pointer_expr);
            return;
        }
        const Binding *binding = bindingFor(resolveVar(pointer_expr), state);
        if (binding == nullptr) {
            return; // untracked storage: not an additional obligation here
        }
        if (binding->state == ObjectState::Dead ||
            binding->state == ObjectState::MaybeDead) {
            reportUseAfterDestroy(*binding, access_loc);
        } else if (binding->state == ObjectState::Null) {
            // A null dereference is a spatial/null-safety issue, which P0
            // does not claim to model; it is neither a lifetime violation
            // nor an unresolved ownership obligation.
            return;
        } else if (binding->state == ObjectState::Unknown) {
            emitUnsupported(
                {"access-unknown-ownership-state", "",
                 location(access_loc)});
        }
    }

    void handleFree(const CallExpr &call, FlowState &state) {
        const Expr *arg = call.getArg(0);
        if (isNullConstant(arg)) {
            return; // KNOWN SAFE: free(NULL)
        }
        const VarDecl *var = resolveVar(arg);
        if (var == nullptr) {
            emitUnsupported(
                {"free-untracked-expression", "", location(call.getExprLoc())});
            return;
        }
        auto it = state.storages.find(var);
        if (it == state.storages.end()) {
            emitUnsupported(
                {"free-untracked-pointer", "", location(call.getExprLoc())});
            return;
        }
        Binding &binding = it->second;
        switch (binding.state) {
        case ObjectState::Null:
            // free(NULL) is defined as a no-op by ISO C.
            return;
        case ObjectState::Owned:
            binding.state = ObjectState::Dead;
            binding.destruction = location(call.getExprLoc());
            binding.destruction_known = true;
            return;
        case ObjectState::Dead:
            reportDoubleDestroy(binding, call.getExprLoc(), /*definite=*/true);
            return;
        case ObjectState::MaybeDead:
            reportDoubleDestroy(binding, call.getExprLoc(), /*definite=*/false);
            return;
        case ObjectState::Unknown:
        case ObjectState::Untracked:
            emitUnsupported(
                {"free-unknown-ownership-state", "", location(call.getExprLoc())});
            return;
        }
    }

    void handleCall(const CallExpr &call, const FlowState &state) {
        if (isNamedCall(call, "free") && call.getNumArgs() == 1) {
            return; // handled by the caller with mutable state
        }
        if (isAllocatorCall(call)) {
            return;
        }
        bool tracked_argument = false;
        for (const Expr *arg : call.arguments()) {
            tracked_argument = tracked_argument || containsTrackedStorage(arg, state);
            if (asUnknownPointerCall(arg) != nullptr) {
                noteUnknownPointerCallIn(arg);
            }
        }
        if (tracked_argument) {
            std::string kind = "unknown-call-with-tracked-pointer";
            if (const FunctionDecl *callee = call.getDirectCallee()) {
                kind += ":" + callee->getNameAsString();
            } else {
                kind += ":indirect";
            }
            markUnsupported(call, kind);
        }
    }

    void bindAllocation(StorageId var, const Expr *init, FlowState &state) {
        Binding binding;
        binding.object_id = objectIdForAllocation(init);
        binding.state = ObjectState::Owned;
        binding.allocation = location(init->getExprLoc());
        state.storages[var] = binding;
        bound_objects_.insert(binding.object_id);
    }

    unsigned objectIdForAllocation(const Expr *init) {
        const CallExpr *call = asCall(init);
        const auto it = allocation_sites_.find(call);
        if (it != allocation_sites_.end()) {
            return it->second;
        }
        // Defensive: an allocation site the pre-pass did not see gets a
        // stable id at the end of the range.
        const unsigned id = next_fallback_id_++;
        if (call != nullptr) {
            allocation_sites_[call] = id;
        }
        return id;
    }

    void handleDeclStmt(const DeclStmt &decl_stmt, FlowState &state) {
        for (const clang::Decl *decl : decl_stmt.decls()) {
            const auto *var = dyn_cast<VarDecl>(decl);
            if (var == nullptr || var->getInit() == nullptr) {
                continue;
            }
            const Expr *init = var->getInit();
            if (!var->getType()->isPointerType()) {
                if (containsAllocationCall(init)) {
                    emitUnsupported(
                        {"allocation-to-untracked-storage:initializer", "",
                         location(decl_stmt.getBeginLoc())});
                } else if (containsUnknownPointerCall(init)) {
                    checkPointerValueSource(init);
                }
                continue;
            }
            if (isAllocation(init)) {
                // A declaration introduces a fresh object on every execution.
                bindAllocation(var, init, state);
            } else if (containsTrackedStorage(init, state)) {
                markUnsupported(decl_stmt, "pointer-alias-initialization");
            } else if (isNullConstant(init)) {
                Binding null_binding;
                null_binding.state = ObjectState::Null;
                state.storages[var] = null_binding;
            } else {
                if (isLocalStackOrigin(init)) {
                    stack_pointers_.insert(var);
                }
                checkPointerValueSource(init);
            }
        }
    }

    void handleAssignment(const BinaryOperator &binary, FlowState &state) {
        const Expr *lhs = binary.getLHS();
        const Expr *rhs = binary.getRHS();
        const VarDecl *var = resolveVar(lhs);
        const bool lhs_is_pointer =
            lhs->getType()->isPointerType() ||
            (var != nullptr && var->getType()->isPointerType());

        if (var != nullptr && var->getType()->isPointerType()) {
            auto it = state.storages.find(var);
            if (isAllocation(rhs)) {
                if (it != state.storages.end() &&
                    (it->second.state == ObjectState::Owned ||
                     it->second.state == ObjectState::MaybeDead ||
                     it->second.state == ObjectState::Unknown)) {
                    markUnsupported(binary, "tracked-owner-overwrite");
                }
                bindAllocation(var, rhs, state);
            } else if (isNullConstant(rhs)) {
                // Releasing an owned pointer into NULL: the object may leak
                // (not modeled in P0.2) but no lifetime bug is introduced,
                // and a later free(NULL) is a defined no-op.
                Binding null_binding;
                null_binding.state = ObjectState::Null;
                state.storages[var] = null_binding;
            } else {
                if (it != state.storages.end() &&
                    it->second.state != ObjectState::Untracked) {
                    markUnsupported(binary, "tracked-pointer-reassignment");
                    state.storages.erase(var);
                }
                if (containsTrackedStorage(rhs, state)) {
                    markUnsupported(binary, "pointer-alias-assignment");
                } else {
                    if (isLocalStackOrigin(rhs)) {
                        stack_pointers_.insert(var);
                    }
                    checkPointerValueSource(rhs);
                }
            }
            return;
        }

        if (lhs_is_pointer) {
            // Assignment into storage C& cannot model yet.
            if (isAllocation(rhs)) {
                emitUnsupported({"allocation-to-untracked-storage:" +
                                               untrackedStorageKind(lhs),
                                           "", location(binary.getExprLoc())});
            } else if (containsTrackedStorage(rhs, state)) {
                markUnsupported(binary, "pointer-alias-assignment");
            } else {
                checkPointerValueSource(rhs);
            }
            return;
        }

        if (containsAllocationCall(rhs) && !containsTrackedStorage(rhs, state)) {
            emitUnsupported({"allocation-to-untracked-storage:initializer",
                                       "", location(binary.getExprLoc())});
        }
    }

    void handleReturn(const ReturnStmt &return_stmt, const FlowState &state) {
        const Expr *ret = return_stmt.getRetValue();
        if (ret == nullptr || !ret->getType()->isPointerType()) {
            return;
        }
        if (containsTrackedStorage(ret, state)) {
            markUnsupported(return_stmt, "tracked-pointer-return");
        }
        const Expr *stripped = ret->IgnoreParenCasts();
        bool stack_escape = false;
        if (const VarDecl *var = resolveVar(stripped)) {
            stack_escape =
                (var->hasLocalStorage() && !isa<ParmVarDecl>(var) &&
                 var->getType()->isArrayType()) ||
                stack_pointers_.count(var) != 0;
        } else {
            stack_escape = isLocalStackOrigin(stripped);
        }
        if (stack_escape) {
            markUnsupported(return_stmt, "stack-pointer-return");
        }
    }

    void processStmt(const Stmt *stmt, FlowState &state,
                     std::set<const Stmt *> &processed);

    void recurseChildren(const Stmt &stmt, FlowState &state,
                         std::set<const Stmt *> &processed) {
        for (const Stmt *child : stmt.children()) {
            if (child != nullptr) {
                processStmt(child, state, processed);
            }
        }
    }

    // ---- driver --------------------------------------------------------

    void collectAllocationSites(const Stmt *root) {
        std::vector<const CallExpr *> sites;
        collectAllocatorCalls(root, sites);
        std::sort(sites.begin(), sites.end(),
                  [this](const CallExpr *a, const CallExpr *b) {
                      return locationLess(location(a->getExprLoc()),
                                          location(b->getExprLoc()));
                  });
        unsigned id = 1;
        for (const CallExpr *call : sites) {
            allocation_sites_[call] = id++;
        }
        next_fallback_id_ = id;
    }

    void collectAllocatorCalls(const Stmt *stmt,
                               std::vector<const CallExpr *> &out) const {
        if (stmt == nullptr) {
            return;
        }
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            if (isAllocatorCall(*call)) {
                out.push_back(call);
            }
        }
        for (const Stmt *child : stmt->children()) {
            collectAllocatorCalls(child, out);
        }
    }

    static bool locationLess(const Location &a, const Location &b) {
        if (a.file != b.file) {
            return a.file < b.file;
        }
        if (a.line != b.line) {
            return a.line < b.line;
        }
        return a.column < b.column;
    }

    void run() {
        std::map<unsigned, FlowState> in_states;
        std::map<unsigned, FlowState> out_states;
        std::set<unsigned> worklist;
        const CFGBlock &entry = cfg_->getEntry();
        in_states[entry.getBlockID()] = FlowState{};
        worklist.insert(entry.getBlockID());

        emitting_ = false;
        while (!worklist.empty()) {
            const unsigned block_id = *worklist.begin();
            worklist.erase(worklist.begin());

            const CFGBlock *block = nullptr;
            for (CFG::const_iterator it = cfg_->begin(); it != cfg_->end(); ++it) {
                if ((*it)->getBlockID() == block_id) {
                    block = *it;
                    break;
                }
            }
            if (block == nullptr) {
                continue;
            }
            // Unreachable code is placed in blocks with no predecessors by
            // Clang's CFG builder; analyzing it would invent obligations for
            // code that can never run.
            if (block != &entry && block->pred_empty()) {
                continue;
            }

            FlowState out = transfer(*block, in_states[block_id]);
            const auto previous = out_states.find(block_id);
            if (previous != out_states.end() && previous->second == out) {
                continue;
            }
            out_states[block_id] = out;

            for (CFGBlock::const_succ_iterator si = block->succ_begin();
                 si != block->succ_end(); ++si) {
                const CFGBlock *successor = *si;
                if (successor == nullptr) {
                    continue;
                }
                const unsigned sid = successor->getBlockID();
                FlowState joined = out;
                const auto existing = in_states.find(sid);
                if (existing != in_states.end()) {
                    joined = joinFlow(existing->second, out);
                    if (joined == existing->second) {
                        continue;
                    }
                }
                in_states[sid] = joined;
                worklist.insert(sid);
            }
        }

        // Emit diagnostics from the converged state only.
        emitting_ = true;
        for (CFG::const_iterator it = cfg_->begin(); it != cfg_->end(); ++it) {
            const CFGBlock *block = *it;
            if (block == nullptr) {
                continue;
            }
            const auto state = in_states.find(block->getBlockID());
            if (state == in_states.end()) {
                continue;
            }
            if (block != &cfg_->getEntry() && block->pred_empty()) {
                continue;
            }
            FlowState ignored = transfer(*block, state->second);
            (void)ignored;
        }
    }

    FlowState transfer(const CFGBlock &block, FlowState state) {
        std::set<const Stmt *> processed;
        for (CFGBlock::const_iterator it = block.begin(); it != block.end(); ++it) {
            const CFGElement &element = *it;
            if (element.getKind() != CFGElement::Statement) {
                continue;
            }
            const std::optional<CFGStmt> cfg_stmt = element.getAs<CFGStmt>();
            if (!cfg_stmt) {
                continue;
            }
            processStmt(cfg_stmt->getStmt(), state, processed);
        }
        // Defensive: conditions normally appear as elements of the block, but
        // make sure a terminator condition is never silently skipped.
        if (const Stmt *terminator = block.getTerminatorStmt()) {
            if (const auto *if_stmt = dyn_cast<IfStmt>(terminator)) {
                processStmt(if_stmt->getCond(), state, processed);
            } else if (const auto *switch_stmt = dyn_cast<SwitchStmt>(terminator)) {
                processStmt(switch_stmt->getCond(), state, processed);
            } else if (const auto *while_stmt = dyn_cast<WhileStmt>(terminator)) {
                processStmt(while_stmt->getCond(), state, processed);
            } else if (const auto *for_stmt = dyn_cast<ForStmt>(terminator)) {
                if (for_stmt->getCond() != nullptr) {
                    processStmt(for_stmt->getCond(), state, processed);
                }
            } else if (const auto *do_stmt = dyn_cast<DoStmt>(terminator)) {
                processStmt(do_stmt->getCond(), state, processed);
            } else if (isa<IndirectGotoStmt>(terminator)) {
                markUnsupported(*terminator, "indirect-goto");
            }
        }
        return state;
    }

    ASTContext &context_;
    SourceManager &source_manager_;
    Collector &collector_;
    const CFG *cfg_ = nullptr;
    std::map<const CallExpr *, unsigned> allocation_sites_;
    unsigned next_fallback_id_ = 1;
    std::set<unsigned> bound_objects_;
    std::set<const VarDecl *> stack_pointers_;
    bool emitting_ = true;
};

// Process one statement/expression node exactly once per block transfer.
// Clang's CFG lists both the subexpressions and the enclosing statement, so
// the processed-set is what keeps ownership transitions single-shot.
void FlowAnalyzer::processStmt(const Stmt *stmt, FlowState &state,
                               std::set<const Stmt *> &processed) {
    if (stmt == nullptr || !processed.insert(stmt).second) {
        return;
    }

    if (const auto *decl_stmt = dyn_cast<DeclStmt>(stmt)) {
        handleDeclStmt(*decl_stmt, state);
        for (const clang::Decl *decl : decl_stmt->decls()) {
            if (const auto *var = dyn_cast<VarDecl>(decl)) {
                if (const Expr *init = var->getInit()) {
                    processStmt(init, state, processed);
                }
            }
        }
        return;
    }

    if (const auto *return_stmt = dyn_cast<ReturnStmt>(stmt)) {
        handleReturn(*return_stmt, state);
        if (const Expr *ret = return_stmt->getRetValue()) {
            processStmt(ret, state, processed);
        }
        return;
    }

    if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
        if (binary->isAssignmentOp()) {
            handleAssignment(*binary, state);
            processStmt(binary->getLHS(), state, processed);
            processStmt(binary->getRHS(), state, processed);
            return;
        }
        if (binary->getOpcode() == clang::BO_LAnd ||
            binary->getOpcode() == clang::BO_LOr) {
            // The RHS runs in its own CFG block; only the LHS is unconditional.
            processStmt(binary->getLHS(), state, processed);
            return;
        }
        recurseChildren(*binary, state, processed);
        return;
    }

    if (const auto *call = dyn_cast<CallExpr>(stmt)) {
        if (isNamedCall(*call, "free") && call->getNumArgs() == 1) {
            handleFree(*call, state);
        } else {
            handleCall(*call, state);
        }
        recurseChildren(*call, state, processed);
        return;
    }

    if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
        if (unary->getOpcode() == clang::UO_Deref) {
            checkAccess(unary->getSubExpr(), unary->getOperatorLoc(), state);
        }
        recurseChildren(*unary, state, processed);
        return;
    }

    if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(stmt)) {
        checkAccess(subscript->getBase(), subscript->getExprLoc(), state);
        recurseChildren(*subscript, state, processed);
        return;
    }

    if (const auto *member = dyn_cast<MemberExpr>(stmt)) {
        if (member->isArrow()) {
            checkAccess(member->getBase(), member->getExprLoc(), state);
        }
        recurseChildren(*member, state, processed);
        return;
    }

    if (const auto *conditional = dyn_cast<ConditionalOperator>(stmt)) {
        // Branch expressions live in their own CFG blocks; re-flattening them
        // here would double-apply ownership transitions.
        processStmt(conditional->getCond(), state, processed);
        return;
    }

    if (isa<AsmStmt>(stmt)) {
        markUnsupported(*stmt, "inline-asm");
        return;
    }

    if (isa<StmtExpr>(stmt)) {
        markUnsupported(*stmt, "statement-expression");
        return;
    }

    if (const auto *if_stmt = dyn_cast<IfStmt>(stmt)) {
        processStmt(if_stmt->getCond(), state, processed);
        return;
    }
    if (const auto *switch_stmt = dyn_cast<SwitchStmt>(stmt)) {
        processStmt(switch_stmt->getCond(), state, processed);
        return;
    }
    if (const auto *while_stmt = dyn_cast<WhileStmt>(stmt)) {
        processStmt(while_stmt->getCond(), state, processed);
        return;
    }
    if (const auto *for_stmt = dyn_cast<ForStmt>(stmt)) {
        if (for_stmt->getCond() != nullptr) {
            processStmt(for_stmt->getCond(), state, processed);
        }
        return;
    }
    if (const auto *do_stmt = dyn_cast<DoStmt>(stmt)) {
        processStmt(do_stmt->getCond(), state, processed);
        return;
    }
    if (isa<IndirectGotoStmt>(stmt)) {
        markUnsupported(*stmt, "indirect-goto");
        return;
    }
    if (isa<GotoStmt>(stmt)) {
        // Direct goto edges are represented in the CFG; nothing to model here.
        return;
    }

    recurseChildren(*stmt, state, processed);
}

class TranslationUnitVisitor : public RecursiveASTVisitor<TranslationUnitVisitor> {
public:
    TranslationUnitVisitor(ASTContext &context, Collector &collector)
        : context_(context), collector_(collector) {}

    bool VisitFunctionDecl(FunctionDecl *function) {
        if (function == nullptr || !function->hasBody()) {
            return true;
        }
        SourceLocation loc =
            context_.getSourceManager().getExpansionLoc(function->getLocation());
        if (!context_.getSourceManager().isWrittenInMainFile(loc)) {
            return true;
        }
        FlowAnalyzer analyzer(context_, collector_);
        analyzer.analyze(*function);
        return true;
    }

    bool VisitVarDecl(VarDecl *var) {
        if (var == nullptr || !var->hasInit()) {
            return true;
        }
        if (!isa<TranslationUnitDecl>(var->getDeclContext())) {
            return true;
        }
        SourceLocation loc =
            context_.getSourceManager().getExpansionLoc(var->getLocation());
        if (!context_.getSourceManager().isWrittenInMainFile(loc)) {
            return true;
        }
        FlowAnalyzer analyzer(context_, collector_);
        analyzer.analyzeGlobal(*var);
        return true;
    }

private:
    ASTContext &context_;
    Collector &collector_;
};

class CandConsumer : public ASTConsumer {
public:
    CandConsumer(ASTContext &context, Collector &collector)
        : visitor_(context, collector) {}

    void HandleTranslationUnit(ASTContext &context) override {
        visitor_.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    TranslationUnitVisitor visitor_;
};

class CandAction : public clang::ASTFrontendAction {
public:
    explicit CandAction(Collector &collector) : collector_(collector) {}

    std::unique_ptr<ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler,
                                                   llvm::StringRef) override {
        return std::make_unique<CandConsumer>(compiler.getASTContext(), collector_);
    }

private:
    Collector &collector_;
};

class CandActionFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit CandActionFactory(Collector &collector) : collector_(collector) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<CandAction>(collector_);
    }

private:
    Collector &collector_;
};

void printUsage(llvm::StringRef program) {
    llvm::errs() << "Usage: " << program
                 << " check [--format=human|json] <source...> [-- <clang-args...>]\n";
}

} // namespace

int main(int argc, const char **argv) {
    if (argc < 2 || llvm::StringRef(argv[1]) != "check") {
        printUsage(argv[0]);
        return 2;
    }

    std::vector<const char *> tool_args;
    tool_args.reserve(static_cast<std::size_t>(argc - 1));
    tool_args.push_back(argv[0]);
    for (int i = 2; i < argc; ++i) {
        tool_args.push_back(argv[i]);
    }

    int tool_argc = static_cast<int>(tool_args.size());
    auto parser_or_error = clang::tooling::CommonOptionsParser::create(
        tool_argc, tool_args.data(), CandCategory, llvm::cl::OneOrMore);
    if (!parser_or_error) {
        llvm::errs() << llvm::toString(parser_or_error.takeError()) << '\n';
        return 2;
    }

    if (OutputFormat != "human" && OutputFormat != "json") {
        llvm::errs() << "cand: --format must be 'human' or 'json'\n";
        return 2;
    }

    auto &options_parser = parser_or_error.get();
    clang::tooling::ClangTool tool(options_parser.getCompilations(),
                                   options_parser.getSourcePathList());

    Collector collector;
    CandActionFactory factory(collector);
    const int tool_result = tool.run(&factory);
    if (tool_result != 0) {
        // Tool/compilation failure is a distinct outcome from a C& FAIL:
        //   0 = PASS, 1 = FAIL (findings), 2 = tool/input error,
        //   3 = INCOMPLETE.
        llvm::errs() << "cand: analysis frontend failed (input or compiler error)\n";
        return 2;
    }

    collector.finalize();
    if (OutputFormat == "json") {
        collector.printJson();
    } else {
        collector.printHuman();
    }

    return collector.exitCode();
}
