// C& — P0.3 storage-identity and alias-aware temporal analysis.
//
// Design: ownership state is attached to program points (CFG basic blocks)
// rather than to source-order statements. A standard worklist computes the
// least fixed point over a small finite lattice. Every heap-relevant
// operation classifies as SUPPORTED, KNOWN SAFE, KNOWN VIOLATION or
// UNSUPPORTED/INCOMPLETE; there is no "unknown but PASS" (ADR-0010).

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <limits>
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
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
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
    std::string access_storage;
    std::string destroy_storage;
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
    // Findings are collected only during the post-convergence emission pass.
    // The map de-duplicates any repeated observation of the same program point.
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

    // A translation unit that produced compilation errors must never receive
    // a C& verdict: the analysis ran on a recovered (not real) AST.
    void noteFrontendError() { frontend_error_ = true; }
    bool hasFrontendError() const { return frontend_error_; }

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
            if (!finding.access_storage.empty()) {
                obj["access_storage"] = finding.access_storage;
            }
            if (!finding.destroy_storage.empty()) {
                obj["destroy_storage"] = finding.destroy_storage;
            }
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
    bool frontend_error_ = false;
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

enum class StorageKind { LocalVariable, StructMember, ArrayElement, DereferenceSlot,
                         GlobalVariable };

struct StorageId {
    StorageKind kind = StorageKind::LocalVariable;
    const VarDecl *root = nullptr;
    std::string path;
    int index = -1;

    bool operator<(const StorageId &other) const {
        if (kind != other.kind) return kind < other.kind;
        const unsigned this_loc = root ? root->getLocation().getRawEncoding() : 0;
        const unsigned other_loc = other.root ? other.root->getLocation().getRawEncoding() : 0;
        if (this_loc != other_loc) return this_loc < other_loc;
        const std::string this_name = root ? root->getNameAsString() : std::string{};
        const std::string other_name = other.root ? other.root->getNameAsString() : std::string{};
        if (this_name != other_name) return this_name < other_name;
        if (path != other.path) return path < other.path;
        if (index != other.index) return index < other.index;
        // Pathological declarations at the same source position still need a
        // strict in-process order. This fallback is not observable for normal
        // source declarations and keeps std::map semantics valid.
        return std::less<const VarDecl *>{}(root, other.root);
    }
    bool operator==(const StorageId &other) const {
        return kind == other.kind && root == other.root && path == other.path &&
               index == other.index;
    }
};

enum class PointerRelation { Owner, Alias, Null, MaybeNull, Unknown };

constexpr unsigned kNullObjectId = 0;
constexpr unsigned kUnknownObjectId = std::numeric_limits<unsigned>::max();

struct ObjectInfo {
    ObjectState state = ObjectState::Untracked;
    Location allocation;
    Location destruction;
    bool destruction_known = false;
    std::string destruction_storage;
    bool operator==(const ObjectInfo &other) const {
        return state == other.state && destruction_known == other.destruction_known &&
               sameLocation(allocation, other.allocation) &&
               sameLocation(destruction, other.destruction) &&
               destruction_storage == other.destruction_storage;
    }
};

struct StorageBinding {
    // 0 is an explicit, definitely-null storage value. UINT_MAX is an
    // unresolved/ambiguous target and must never be interpreted as NULL.
    unsigned object_id = kUnknownObjectId;
    PointerRelation relation = PointerRelation::Unknown;
    Location relation_location;
    bool operator==(const StorageBinding &other) const {
        return object_id == other.object_id && relation == other.relation &&
               sameLocation(relation_location, other.relation_location);
    }
};

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

std::string storageName(const StorageId &storage) {
    const char *prefix = storage.kind == StorageKind::StructMember ? "field:" :
                         storage.kind == StorageKind::ArrayElement ? "array:" :
                         storage.kind == StorageKind::DereferenceSlot ? "deref:" :
                         storage.kind == StorageKind::GlobalVariable ? "global:" : "local:";
    std::string name = prefix + (storage.root ? storage.root->getNameAsString() : "?");
    if (!storage.path.empty()) name += storage.path;
    if (storage.index >= 0) name += "[" + std::to_string(storage.index) + "]";
    return name;
}

StorageBinding joinBinding(const StorageBinding &a, const StorageBinding &b) {
    const auto unknown = [](const StorageBinding &binding) {
        return binding.object_id == kUnknownObjectId ||
               (binding.object_id == kNullObjectId &&
                binding.relation == PointerRelation::Unknown);
    };
    const auto known_null = [](const StorageBinding &binding) {
        return binding.object_id == kNullObjectId &&
               binding.relation == PointerRelation::Null;
    };
    const Location relation_location =
        minLocation(a.relation_location, b.relation_location);

    if (a.object_id == b.object_id) {
        if (a.relation == b.relation) {
            return {a.object_id, a.relation, relation_location};
        }
        if (a.relation == PointerRelation::MaybeNull ||
            b.relation == PointerRelation::MaybeNull) {
            return {a.object_id, PointerRelation::MaybeNull, relation_location};
        }
        return {a.object_id, PointerRelation::Unknown, relation_location};
    }
    if (unknown(a) || unknown(b)) {
        return {kUnknownObjectId, PointerRelation::Unknown, relation_location};
    }
    // Null on one path and one known object on the other still has one heap
    // target for temporal purposes. Null-dereference safety is outside P0.
    if (known_null(a) && b.object_id != kNullObjectId) {
        return {b.object_id, PointerRelation::MaybeNull, relation_location};
    }
    if (known_null(b) && a.object_id != kNullObjectId) {
        return {a.object_id, PointerRelation::MaybeNull, relation_location};
    }
    // Two different non-null objects are an unresolved alias target, never NULL.
    return {kUnknownObjectId, PointerRelation::Unknown, relation_location};
}

struct FlowState {
    std::map<StorageId, StorageBinding> storages;
    std::map<unsigned, ObjectInfo> objects;

    bool operator==(const FlowState &other) const {
        return storages == other.storages && objects == other.objects;
    }
};

FlowState joinFlow(const FlowState &a, const FlowState &b) {
    FlowState result;
    result.objects = a.objects;
    for (const auto &entry : b.objects) {
        auto it = result.objects.find(entry.first);
        if (it == result.objects.end()) {
            result.objects[entry.first] = entry.second;
        } else {
            it->second.state = joinState(it->second.state, entry.second.state);
            it->second.allocation = minLocation(it->second.allocation, entry.second.allocation);
            it->second.destruction_known = it->second.destruction_known || entry.second.destruction_known;
            if (entry.second.destruction_known)
                it->second.destruction = minLocation(it->second.destruction, entry.second.destruction);
            if (entry.second.destruction_known &&
                (it->second.destruction_storage.empty() ||
                 entry.second.destruction_storage < it->second.destruction_storage))
                it->second.destruction_storage = entry.second.destruction_storage;
        }
    }
    for (const auto &entry : a.storages) {
        const auto it = b.storages.find(entry.first);
        result.storages[entry.first] =
            it == b.storages.end() ? joinBinding(entry.second, StorageBinding{})
                                   : joinBinding(entry.second, it->second);
    }
    for (const auto &entry : b.storages) {
        if (a.storages.find(entry.first) == a.storages.end()) {
            result.storages[entry.first] = joinBinding(StorageBinding{}, entry.second);
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
        collectLoopAllocations(body, false);
        collectUnevaluated(body);

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
        expr = expr->IgnoreParenCasts();
        if (expr->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull)) {
            return true;
        }
        // A conditional whose branches are both null constants is a null
        // constant (e.g. `c ? NULL : NULL`).
        if (const auto *conditional = dyn_cast<ConditionalOperator>(expr)) {
            return isNullConstant(conditional->getTrueExpr()) &&
                   isNullConstant(conditional->getFalseExpr());
        }
        return false;
    }

    // An initializer/RHS that yields a fresh owned allocation or NULL on
    // every path (e.g. `c ? malloc(4) : malloc(8)`), so binding it as an
    // owned storage is sound. Anything mixing an allocation with a pointer
    // of unknown ownership returns false and stays INCOMPLETE.
    bool isAllocationOrNull(const Expr *expr) const {
        if (expr == nullptr) {
            return false;
        }
        expr = expr->IgnoreParenCasts();
        if (isNullConstant(expr) || isAllocation(expr)) {
            return true;
        }
        if (const auto *conditional = dyn_cast<ConditionalOperator>(expr)) {
            return isAllocationOrNull(conditional->getTrueExpr()) &&
                   isAllocationOrNull(conditional->getFalseExpr());
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(expr)) {
            if (binary->getOpcode() == clang::BO_Comma) {
                return isAllocationOrNull(binary->getRHS());
            }
        }
        return false;
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

    bool containsPointerToIntegerCast(const Expr *expr) const {
        if (expr == nullptr) return false;
        if (const auto *cast = dyn_cast<clang::CastExpr>(expr)) {
            if (cast->getCastKind() == clang::CK_PointerToIntegral) return true;
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsPointerToIntegerCast(child_expr)) return true;
            }
        }
        return false;
    }

    bool containsGlobalStorage(const Expr *expr) const {
        if (expr == nullptr) return false;
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (const auto *var = dyn_cast<VarDecl>(ref->getDecl())) {
                if (var->hasGlobalStorage()) return true;
            }
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsGlobalStorage(child_expr)) return true;
            }
        }
        return false;
    }

    std::optional<StorageId> storageFor(const Expr *expr) const {
        if (expr == nullptr) return std::nullopt;
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (const auto *var = dyn_cast<VarDecl>(ref->getDecl())) {
                // File-scope and static-local storage outlives one invocation;
                // P0.3's per-function state cannot model it soundly.
                if (var->hasGlobalStorage()) return std::nullopt;
                return StorageId{StorageKind::LocalVariable, var, {}, -1};
            }
            return std::nullopt;
        }
        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (member->isArrow()) return std::nullopt;
            if (const auto *field = dyn_cast<clang::FieldDecl>(member->getMemberDecl())) {
                if (field->getParent() != nullptr && field->getParent()->isUnion()) {
                    return std::nullopt;
                }
            }
            auto base = storageFor(member->getBase());
            if (!base) return std::nullopt;
            StorageId result = *base;
            result.kind = StorageKind::StructMember;
            result.path += "." + member->getMemberDecl()->getNameAsString();
            return result;
        }
        if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr)) {
            auto base = storageFor(subscript->getBase());
            if (!base) return std::nullopt;
            const auto *literal = dyn_cast<clang::IntegerLiteral>(
                subscript->getIdx()->IgnoreParenCasts());
            if (!literal) return std::nullopt;
            StorageId result = *base;
            result.kind = StorageKind::ArrayElement;
            result.path += "[" + std::to_string(literal->getValue().getSExtValue()) + "]";
            result.index = -1;
            return result;
        }
        return std::nullopt;
    }

    const StorageBinding *bindingFor(const Expr *expr, const FlowState &state) const {
        const auto storage = storageFor(expr);
        if (!storage) return nullptr;
        const auto it = state.storages.find(*storage);
        return it == state.storages.end() ? nullptr : &it->second;
    }

    const ObjectInfo *objectFor(const StorageBinding *binding, const FlowState &state) const {
        if (binding == nullptr || binding->object_id == kNullObjectId ||
            binding->object_id == kUnknownObjectId) {
            return nullptr;
        }
        const auto it = state.objects.find(binding->object_id);
        return it == state.objects.end() ? nullptr : &it->second;
    }

    // Find the tracked storage referenced anywhere inside an expression, so
    // that a dereference whose base is not a bare variable (pointer
    // arithmetic such as `*(p + 1)`, `(p + i)[j]`, `(p + 1)->field`) is still
    // checked against the object it ultimately refers to. The lowest object
    // id wins so the choice is deterministic.
    void collectTrackedBindings(const Expr *expr, const FlowState &state,
                                const StorageBinding *&best) const {
        if (expr == nullptr) {
            return;
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            const StorageBinding *binding = bindingFor(expr, state);
            if (binding != nullptr &&
                (best == nullptr || binding->object_id < best->object_id)) {
                best = binding;
            }
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                collectTrackedBindings(child_expr, state, best);
            }
        }
    }

    const StorageBinding *findTrackedBinding(const Expr *expr,
                                      const FlowState &state) const {
        const StorageBinding *best = nullptr;
        collectTrackedBindings(expr, state, best);
        return best;
    }

    std::optional<StorageId> findTrackedStorage(const Expr *expr,
                                                const FlowState &state) const {
        if (expr == nullptr) return std::nullopt;
        expr = expr->IgnoreParenCasts();
        if (storageFor(expr) && state.storages.find(*storageFor(expr)) != state.storages.end())
            return storageFor(expr);
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (auto found = findTrackedStorage(child_expr, state)) return found;
            }
        }
        return std::nullopt;
    }

    bool containsTrackedStorage(const Expr *expr, const FlowState &state) const {
        if (expr == nullptr) {
            return false;
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (const StorageBinding *binding = bindingFor(expr, state)) {
                return !(binding->object_id == kNullObjectId &&
                         binding->relation == PointerRelation::Null);
            }
            // Aggregate expressions such as `s`, `&s`, or a by-value struct
            // argument may carry tracked pointer fields even though the root
            // aggregate itself has no pointer binding.
            if (const auto *var = dyn_cast<VarDecl>(ref->getDecl())) {
                for (const auto &entry : state.storages) {
                    if (entry.first.root == var &&
                        !(entry.second.object_id == kNullObjectId &&
                          entry.second.relation == PointerRelation::Null)) {
                        return true;
                    }
                }
            }
            return false;
        }
        if (storageFor(expr)) return state.storages.find(*storageFor(expr)) != state.storages.end();
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
        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (const auto *field = dyn_cast<clang::FieldDecl>(member->getMemberDecl())) {
                if (field->getParent() != nullptr && field->getParent()->isUnion()) {
                    return "union-member-storage";
                }
            }
            return "struct-member";
        }
        if (isa<ArraySubscriptExpr>(expr)) {
            const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr);
            return isa<clang::IntegerLiteral>(subscript->getIdx()->IgnoreParenCasts())
                       ? "array-element" : "dynamic-array-storage";
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            if (unary->getOpcode() == clang::UO_Deref) {
                return "unresolved-pointee-storage";
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

    void reportUseAfterDestroy(const StorageBinding &binding, const ObjectInfo &object,
                               const StorageId &access, SourceLocation use_loc,
                               const FlowState &state, bool nullable) {
        const bool definite = object.state == ObjectState::Dead && !nullable;
        Finding finding;
        finding.id = "CAND-T002";
        finding.rule_id = "cand1.no-use-after-death";
        finding.message = definite ? "use after object destruction"
                                   : "possible use after object destruction";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.certainty = definite ? "definite" : "possible";
        finding.state_before = stateName(object.state);
        finding.object_id = objectName(binding.object_id);
        finding.access_storage = storageName(access);
        if (object.destruction_known) finding.destroy_storage = object.destruction_storage;
        finding.primary = location(use_loc);
        finding.trace.push_back({"allocation", "Owned", object.allocation});
        for (const auto &entry : state.storages) {
            if (entry.second.object_id == binding.object_id &&
                entry.second.relation == PointerRelation::Alias) {
                const Location alias_location = entry.second.relation_location.file.empty()
                                                    ? object.allocation
                                                    : entry.second.relation_location;
                finding.trace.push_back({"alias_created", "Alias", alias_location});
            }
        }
        if (object.destruction_known) {
            finding.trace.push_back({definite ? "destruction" : "conditional_destruction",
                                     definite ? "Dead" : "MaybeDead",
                                     object.destruction});
        }
        finding.trace.push_back(
            {"access", stateName(object.state), location(use_loc)});
        emitFinding(std::move(finding));
    }

    void reportDoubleDestroy(const StorageBinding &binding, const ObjectInfo &object,
                             const StorageId &destroy, SourceLocation destroy_loc,
                             bool definite) {
        Finding finding;
        finding.id = "CAND-T003";
        finding.rule_id = "cand1.single-destruction";
        finding.message = definite ? "object destroyed more than once"
                                   : "possible double destruction on some path";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.certainty = definite ? "definite" : "possible";
        finding.state_before = stateName(object.state);
        finding.object_id = objectName(binding.object_id);
        finding.destroy_storage = storageName(destroy);
        finding.primary = location(destroy_loc);
        finding.trace.push_back({"allocation", "Owned", object.allocation});
        if (object.destruction_known) {
            finding.trace.push_back({definite ? "first_destruction"
                                              : "conditional_destruction",
                                     definite ? "Dead" : "MaybeDead",
                                     object.destruction});
        }
        finding.trace.push_back(
            {"repeated_destruction", stateName(object.state), location(destroy_loc)});
        emitFinding(std::move(finding));
    }

    // ---- transfer functions -------------------------------------------

    void checkAccess(const Expr *pointer_expr, SourceLocation access_loc,
                     const FlowState &state) {
        if (containsGlobalStorage(pointer_expr)) {
            emitUnsupported({"global-or-static-pointer-storage", "", location(access_loc)});
            return;
        }
        if (asUnknownPointerCall(pointer_expr) != nullptr) {
            noteUnknownPointerCallIn(pointer_expr);
            return;
        }
        auto access_storage = storageFor(pointer_expr);
        const StorageBinding *binding = bindingFor(pointer_expr, state);
        if (binding == nullptr) {
            // The base is not a bare variable: it may be pointer arithmetic
            // or another computed form that still refers to a tracked object.
            binding = findTrackedBinding(pointer_expr, state);
        }
        if (!access_storage) access_storage = findTrackedStorage(pointer_expr, state);
        if (binding == nullptr) {
            return; // genuinely untracked storage: outside the current P0 heap scope
        }
        if (binding->object_id == kUnknownObjectId ||
            (binding->object_id == kNullObjectId &&
             binding->relation == PointerRelation::Unknown)) {
            emitUnsupported({"ambiguous-alias-target", "", location(access_loc)});
            return;
        }
        if (binding->object_id == kNullObjectId &&
            binding->relation == PointerRelation::Null) {
            return; // null dereference is outside the P0 temporal claim
        }
        const ObjectInfo *object = objectFor(binding, state);
        if (object == nullptr) {
            emitUnsupported({"access-unknown-ownership-state", "", location(access_loc)});
            return;
        }
        if (object->state == ObjectState::Dead || object->state == ObjectState::MaybeDead) {
            if (access_storage) {
                reportUseAfterDestroy(*binding, *object, *access_storage, access_loc, state,
                                      binding->relation == PointerRelation::MaybeNull);
            } else {
                emitUnsupported({"unresolved-access-storage", "", location(access_loc)});
            }
        } else if (object->state == ObjectState::Null) {
            // A null dereference is a spatial/null-safety issue, which P0
            // does not claim to model; it is neither a lifetime violation
            // nor an unresolved ownership obligation.
            return;
        } else if (object->state == ObjectState::Unknown) {
            emitUnsupported(
                {"access-unknown-ownership-state", "",
                 location(access_loc)});
        }
    }

    void handleFree(const CallExpr &call, FlowState &state) {
        const Expr *arg = call.getArg(0);
        if (containsGlobalStorage(arg)) {
            emitUnsupported({"global-or-static-pointer-storage", "", location(call.getExprLoc())});
            return;
        }
        if (isNullConstant(arg)) {
            return; // KNOWN SAFE: free(NULL)
        }
        const auto destroy_storage = storageFor(arg);
        if (!destroy_storage) {
            const Expr *base = arg->IgnoreParenCasts();
            if (isa<ArraySubscriptExpr>(base)) {
                emitUnsupported({untrackedStorageKind(base), "", location(call.getExprLoc())});
                return;
            }
            if (const auto *unary = dyn_cast<UnaryOperator>(base)) {
                if (unary->getOpcode() == clang::UO_Deref) {
                    emitUnsupported({"unresolved-pointee-storage", "", location(call.getExprLoc())});
                    return;
                }
            }
            emitUnsupported(
                {"free-untracked-expression", "", location(call.getExprLoc())});
            return;
        }
        auto it = state.storages.find(*destroy_storage);
        if (it == state.storages.end()) {
            emitUnsupported(
                {"free-untracked-pointer", "", location(call.getExprLoc())});
            return;
        }
        StorageBinding &binding = it->second;
        if (binding.object_id == kNullObjectId &&
            binding.relation == PointerRelation::Null) {
            return; // definitely NULL storage
        }
        if (binding.object_id == kUnknownObjectId ||
            binding.object_id == kNullObjectId) {
            emitUnsupported({"ambiguous-alias-target", "", location(call.getExprLoc())});
            return;
        }
        const auto object_it = state.objects.find(binding.object_id);
        if (object_it == state.objects.end()) {
            emitUnsupported({"free-unknown-ownership-state", "", location(call.getExprLoc())});
            return;
        }
        ObjectInfo *object = &object_it->second;
        if (binding.relation == PointerRelation::MaybeNull) {
            if (object->state == ObjectState::Owned) {
                object->state = ObjectState::MaybeDead;
                object->destruction = location(call.getExprLoc());
                object->destruction_known = true;
                object->destruction_storage = storageName(*destroy_storage);
                return;
            }
            if (object->state == ObjectState::Dead ||
                object->state == ObjectState::MaybeDead) {
                reportDoubleDestroy(binding, *object, *destroy_storage, call.getExprLoc(), false);
                return;
            }
        }
        switch (object->state) {
        case ObjectState::Null:
            // free(NULL) is defined as a no-op by ISO C.
            return;
        case ObjectState::Owned:
            object->state = ObjectState::Dead;
            object->destruction = location(call.getExprLoc());
            object->destruction_known = true;
            object->destruction_storage = storageName(*destroy_storage);
            return;
        case ObjectState::Dead:
            reportDoubleDestroy(binding, *object, *destroy_storage, call.getExprLoc(), true);
            return;
        case ObjectState::MaybeDead:
            reportDoubleDestroy(binding, *object, *destroy_storage, call.getExprLoc(), false);
            return;
        case ObjectState::Unknown:
        case ObjectState::Untracked:
            emitUnsupported(
                {"free-unknown-ownership-state", "", location(call.getExprLoc())});
            return;
        }
    }

    void handleCall(const CallExpr &call, const FlowState &state) {
        const auto is_nonlocal_control = [this, &call]() {
            static const char *names[] = {"setjmp", "_setjmp", "sigsetjmp",
                                           "__sigsetjmp", "longjmp", "_longjmp",
                                           "siglongjmp"};
            for (const char *name : names) {
                if (isNamedCall(call, name)) return true;
            }
            return false;
        };
        if (is_nonlocal_control()) {
            markUnsupported(call, "nonlocal-control-flow");
            return;
        }
        if (isNamedCall(call, "free") && call.getNumArgs() == 1) {
            return; // handled by the caller with mutable state
        }
        if (isAllocatorCall(call)) {
            return;
        }
        bool tracked_argument = false;
        bool global_argument = false;
        for (const Expr *arg : call.arguments()) {
            tracked_argument = tracked_argument || containsTrackedStorage(arg, state);
            global_argument = global_argument || containsGlobalStorage(arg);
            if (asUnknownPointerCall(arg) != nullptr) {
                noteUnknownPointerCallIn(arg);
            }
        }
        if (global_argument) {
            markUnsupported(call, "global-or-static-pointer-storage");
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

    bool containsLoopAllocation(const Expr *expr) const {
        if (expr == nullptr) return false;
        if (const auto *call = asCall(expr)) {
            if (loop_allocation_sites_.count(call) != 0) return true;
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsLoopAllocation(child_expr)) return true;
            }
        }
        return false;
    }

    void bindAllocation(StorageId storage, const Expr *init, FlowState &state) {
        if (containsLoopAllocation(init)) {
            markUnsupported(*init, "loop-allocation-site");
        }
        const unsigned object_id = objectIdForAllocation(init);
        state.storages[storage] =
            {object_id, PointerRelation::Owner, location(init->getExprLoc())};
        auto &object = state.objects[object_id];
        object.state = ObjectState::Owned;
        object.allocation = location(init->getExprLoc());
        bound_objects_.insert(object_id);
    }

    unsigned objectIdForAllocation(const Expr *init) {
        const CallExpr *call = asCall(init);
        const auto it = allocation_sites_.find(call);
        if (it != allocation_sites_.end()) {
            return it->second;
        }
        // Composite allocation expressions (e.g. `cond ? malloc() : NULL`)
        // need a stable synthetic id across worklist iterations as well.
        const Expr *key = init ? init->IgnoreParenCasts() : init;
        const auto synthetic = synthetic_allocation_sites_.find(key);
        if (synthetic != synthetic_allocation_sites_.end()) return synthetic->second;
        const unsigned id = next_fallback_id_++;
        if (call != nullptr) allocation_sites_[call] = id;
        if (key != nullptr) synthetic_allocation_sites_[key] = id;
        return id;
    }

    void handleDeclStmt(const DeclStmt &decl_stmt, FlowState &state) {
        for (const clang::Decl *decl : decl_stmt.decls()) {
            const auto *var = dyn_cast<VarDecl>(decl);
            if (var == nullptr || var->getInit() == nullptr) {
                continue;
            }
            const Expr *init = var->getInit();
            if (var->hasGlobalStorage() &&
                (var->getType()->isPointerType() || var->getType()->isAtomicType() ||
                 containsAllocationCall(init) || containsTrackedStorage(init, state) ||
                 containsUnknownPointerCall(init))) {
                markUnsupported(decl_stmt, "global-or-static-pointer-storage");
                continue;
            }
            if (!var->getType()->isPointerType()) {
                if (containsAllocationCall(init)) {
                    emitUnsupported(
                        {"allocation-to-untracked-storage:initializer", "",
                         location(decl_stmt.getBeginLoc())});
                } else if (containsPointerToIntegerCast(init) &&
                           containsTrackedStorage(init, state)) {
                    markUnsupported(decl_stmt, "pointer-integer-provenance");
                } else if (var->getType()->isAtomicType() &&
                           containsTrackedStorage(init, state)) {
                    markUnsupported(decl_stmt, "atomic-pointer-storage");
                } else if (var->getType()->isRecordType() &&
                           containsTrackedStorage(init, state)) {
                    markUnsupported(decl_stmt, "aggregate-copy-with-tracked-pointer");
                } else if (containsUnknownPointerCall(init)) {
                    checkPointerValueSource(init);
                }
                continue;
            }
            const StorageId storage{StorageKind::LocalVariable, var, {}, -1};
            if (isNullConstant(init)) {
                state.storages[storage] =
                    {kNullObjectId, PointerRelation::Null, location(init->getExprLoc())};
            } else if (isAllocationOrNull(init)) {
                // A declaration introduces a fresh object on every execution.
                bindAllocation(storage, init, state);
            } else if (const auto source = storageFor(init)) {
                const auto it = state.storages.find(*source);
                if (it == state.storages.end() ||
                    it->second.object_id == kUnknownObjectId ||
                    (it->second.object_id == kNullObjectId &&
                     it->second.relation == PointerRelation::Unknown)) {
                    markUnsupported(decl_stmt, "ambiguous-alias-target");
                } else if (it->second.object_id == kNullObjectId) {
                    state.storages[storage] =
                        {kNullObjectId, PointerRelation::Null, location(init->getExprLoc())};
                } else {
                    state.storages[storage] =
                        {it->second.object_id, PointerRelation::Alias,
                         location(init->getExprLoc())};
                }
            } else if (containsTrackedStorage(init, state)) {
                markUnsupported(decl_stmt, "ambiguous-alias-target");
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
        const auto lhs_storage = storageFor(lhs);
        const VarDecl *var = resolveVar(lhs);
        const bool lhs_is_pointer =
            lhs->getType()->isPointerType() ||
            (var != nullptr && var->getType()->isPointerType());

        if (containsGlobalStorage(lhs) &&
            (lhs_is_pointer || containsAllocationCall(rhs) ||
             containsTrackedStorage(rhs, state) || containsPointerToIntegerCast(rhs))) {
            markUnsupported(binary, "global-or-static-pointer-storage");
            return;
        }

        if (var != nullptr && var->getType()->isPointerType()) {
            auto it = lhs_storage ? state.storages.find(*lhs_storage) : state.storages.end();
            if (isNullConstant(rhs)) {
                // Releasing an owned pointer into NULL: the object may leak
                // (not modeled in P0.2) but no lifetime bug is introduced,
                // and a later free(NULL) is a defined no-op.
                if (lhs_storage) {
                    state.storages[*lhs_storage] =
                        {kNullObjectId, PointerRelation::Null, location(binary.getExprLoc())};
                }
            } else if (isAllocationOrNull(rhs)) {
                const ObjectInfo *old = it == state.storages.end() ? nullptr :
                    objectFor(&it->second, state);
                if (old != nullptr && (old->state == ObjectState::Owned ||
                     old->state == ObjectState::MaybeDead || old->state == ObjectState::Unknown)) {
                    markUnsupported(binary, "tracked-owner-overwrite");
                }
                if (lhs_storage) bindAllocation(*lhs_storage, rhs, state);
            } else {
                if (const auto source = storageFor(rhs)) {
                    const auto source_it = state.storages.find(*source);
                    if (source_it == state.storages.end() ||
                        source_it->second.object_id == kUnknownObjectId ||
                        (source_it->second.object_id == kNullObjectId &&
                         source_it->second.relation == PointerRelation::Unknown)) {
                        markUnsupported(binary, "ambiguous-alias-target");
                    } else if (lhs_storage &&
                               source_it->second.object_id == kNullObjectId) {
                        state.storages[*lhs_storage] =
                            {kNullObjectId, PointerRelation::Null,
                             location(binary.getExprLoc())};
                    } else if (lhs_storage) {
                        state.storages[*lhs_storage] =
                            {source_it->second.object_id, PointerRelation::Alias,
                             location(binary.getExprLoc())};
                    }
                } else if (containsTrackedStorage(rhs, state)) {
                    markUnsupported(binary, "ambiguous-alias-target");
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
            if (lhs_storage && isAllocationOrNull(rhs)) {
                if (isNullConstant(rhs)) {
                    state.storages[*lhs_storage] =
                        {kNullObjectId, PointerRelation::Null,
                         location(binary.getExprLoc())};
                } else {
                    bindAllocation(*lhs_storage, rhs, state);
                }
            } else if (lhs_storage && storageFor(rhs)) {
                const auto source = storageFor(rhs);
                const auto source_it = state.storages.find(*source);
                if (source_it == state.storages.end() ||
                    source_it->second.object_id == kUnknownObjectId ||
                    (source_it->second.object_id == kNullObjectId &&
                     source_it->second.relation == PointerRelation::Unknown)) {
                    markUnsupported(binary, "ambiguous-alias-target");
                } else if (source_it->second.object_id == kNullObjectId) {
                    state.storages[*lhs_storage] =
                        {kNullObjectId, PointerRelation::Null,
                         location(binary.getExprLoc())};
                } else {
                    state.storages[*lhs_storage] =
                        {source_it->second.object_id, PointerRelation::Alias,
                         location(binary.getExprLoc())};
                }
            } else if (isAllocation(rhs)) {
                emitUnsupported({"allocation-to-untracked-storage:" +
                                               untrackedStorageKind(lhs),
                                           "", location(binary.getExprLoc())});
            } else if (containsTrackedStorage(rhs, state)) {
                markUnsupported(binary, "unresolved-pointee-storage");
            } else {
                checkPointerValueSource(rhs);
            }
            return;
        }

        if (lhs->getType()->isAtomicType() && containsTrackedStorage(rhs, state)) {
            markUnsupported(binary, "atomic-pointer-storage");
        } else if (containsPointerToIntegerCast(rhs) &&
            containsTrackedStorage(rhs, state)) {
            markUnsupported(binary, "pointer-integer-provenance");
        } else if (lhs->getType()->isRecordType() &&
                   containsTrackedStorage(rhs, state)) {
            markUnsupported(binary, "aggregate-copy-with-tracked-pointer");
        } else if (containsAllocationCall(rhs) && !containsTrackedStorage(rhs, state)) {
            emitUnsupported({"allocation-to-untracked-storage:initializer",
                                       "", location(binary.getExprLoc())});
        }
    }

    bool isKnownStaticPointerOrigin(const Expr *expr) const {
        if (expr == nullptr) return false;
        expr = expr->IgnoreParenCasts();
        if (isa<clang::StringLiteral>(expr)) return true;
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (const auto *var = dyn_cast<VarDecl>(ref->getDecl())) {
                return var->hasGlobalStorage() && var->getType()->isArrayType();
            }
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            if (unary->getOpcode() == clang::UO_AddrOf)
                return isKnownStaticPointerOrigin(unary->getSubExpr());
        }
        return false;
    }

    void handleReturn(const ReturnStmt &return_stmt, const FlowState &state) {
        const Expr *ret = return_stmt.getRetValue();
        if (ret == nullptr) return;
        if (containsGlobalStorage(ret) && !isKnownStaticPointerOrigin(ret)) {
            markUnsupported(return_stmt, "global-or-static-pointer-storage");
            return;
        }
        if (!ret->getType()->isPointerType()) {
            if (containsPointerToIntegerCast(ret) && containsTrackedStorage(ret, state)) {
                markUnsupported(return_stmt, "pointer-integer-provenance");
            } else if (ret->getType()->isRecordType() &&
                       containsTrackedStorage(ret, state)) {
                markUnsupported(return_stmt, "aggregate-return-with-tracked-pointer");
            }
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

    // Collect the operands of unevaluated contexts (sizeof / alignof /
    // typeof). Their children are listed by the CFG but never dereference
    // memory, so they must not be treated as accesses.
    void collectUnevaluated(const Stmt *stmt) {
        if (stmt == nullptr) {
            return;
        }
        if (isa<UnaryExprOrTypeTraitExpr>(stmt)) {
            markUnevaluatedChildren(stmt);
            return;
        }
        for (const Stmt *child : stmt->children()) {
            collectUnevaluated(child);
        }
    }

    void markUnevaluatedChildren(const Stmt *stmt) {
        for (const Stmt *child : stmt->children()) {
            if (child == nullptr) {
                continue;
            }
            unevaluated_.insert(child);
            markUnevaluatedChildren(child);
        }
    }

    void recurseChildren(const Stmt &stmt, FlowState &state,
                         std::set<const Stmt *> &processed) {
        for (const Stmt *child : stmt.children()) {
            if (child != nullptr) {
                processStmt(child, state, processed);
            }
        }
    }

    // ---- driver --------------------------------------------------------

    void collectLoopAllocations(const Stmt *stmt, bool inside_loop) {
        if (stmt == nullptr) return;
        const bool children_in_loop = inside_loop || isa<ForStmt>(stmt) ||
                                      isa<WhileStmt>(stmt) || isa<DoStmt>(stmt);
        if (inside_loop) {
            if (const auto *call = dyn_cast<CallExpr>(stmt)) {
                if (isAllocatorCall(*call)) loop_allocation_sites_.insert(call);
            }
        }
        for (const Stmt *child : stmt->children()) {
            collectLoopAllocations(child, children_in_loop);
        }
    }

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
    std::map<const Expr *, unsigned> synthetic_allocation_sites_;
    std::set<const CallExpr *> loop_allocation_sites_;
    unsigned next_fallback_id_ = 1;
    std::set<unsigned> bound_objects_;
    std::set<const VarDecl *> stack_pointers_;
    std::set<const Stmt *> unevaluated_;
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
    if (unevaluated_.count(stmt) != 0) {
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

    if (const auto *atomic = dyn_cast<clang::AtomicExpr>(stmt)) {
        if (atomic->getType()->isPointerType() ||
            containsTrackedStorage(atomic, state) || containsGlobalStorage(atomic)) {
            markUnsupported(*stmt, "atomic-pointer-storage");
        }
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
        : visitor_(context, collector), collector_(collector) {}

    void HandleTranslationUnit(ASTContext &context) override {
        if (context.getDiagnostics().hasErrorOccurred()) {
            collector_.noteFrontendError();
        }
        visitor_.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    TranslationUnitVisitor visitor_;
    Collector &collector_;
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

// Any error-level diagnostic (including driver-level option errors that
// still let Clang build a recovered AST) means the translation unit did not
// compile: C& must report a tool error, never a verdict. The flag lives in
// shared storage because the tool may take ownership of the consumer.
struct FrontendErrors {
    bool saw_error = false;
};

class FrontendErrorTracker : public clang::DiagnosticConsumer {
public:
    FrontendErrorTracker(llvm::raw_ostream &os, clang::DiagnosticOptions *options,
                         std::shared_ptr<FrontendErrors> errors)
        : printer_(os, options), errors_(std::move(errors)) {}

    void BeginSourceFile(const clang::LangOptions &options,
                         const clang::Preprocessor *pp) override {
        printer_.BeginSourceFile(options, pp);
    }

    void EndSourceFile() override { printer_.EndSourceFile(); }

    void finish() override { printer_.finish(); }

    void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                          const clang::Diagnostic &info) override {
        if (level >= clang::DiagnosticsEngine::Error) {
            errors_->saw_error = true;
        }
        printer_.HandleDiagnostic(level, info);
    }

private:
    clang::TextDiagnosticPrinter printer_;
    std::shared_ptr<FrontendErrors> errors_;
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

    auto diagnostic_options = new clang::DiagnosticOptions();
    auto frontend_errors = std::make_shared<FrontendErrors>();
    tool.setDiagnosticConsumer(
        new FrontendErrorTracker(llvm::errs(), diagnostic_options, frontend_errors));

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

    if (tool_result != 0 || frontend_errors->saw_error ||
        collector.hasFrontendError()) {
        llvm::errs() << "cand: translation unit did not compile; no verdict is "
                        "reported (input or compiler error)\n";
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
