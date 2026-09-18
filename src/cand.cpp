// C& — P1 explicit unique-ownership and move-aware temporal analysis.
//
// Design: ownership state is attached to program points (CFG basic blocks)
// rather than to source-order statements. A standard worklist computes the
// least fixed point over a small finite lattice. Every heap-relevant
// operation classifies as SUPPORTED, KNOWN SAFE, KNOWN VIOLATION or
// UNSUPPORTED/INCOMPLETE; there is no "unknown but PASS" (ADR-0010).

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

#include "clang/Analysis/CFG.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/Lexer.h"
#include "clang/Basic/Version.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/raw_ostream.h"

#include "agent_policy.hpp"

#ifndef CAND_VERIFIER_SOURCE_COMMIT
#define CAND_VERIFIER_SOURCE_COMMIT "unknown"
#endif

namespace {

using clang::ASTConsumer;
using clang::ASTContext;
using clang::AnnotateAttr;
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
llvm::cl::opt<std::string> ContractFile(
    "contracts", llvm::cl::desc("trusted C& API contract YAML"),
    llvm::cl::init(""), llvm::cl::cat(CandCategory));
llvm::cl::opt<bool> AgentMode(
    "agent", llvm::cl::desc("strict generated-code verification mode"),
    llvm::cl::init(false), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> ProfileName(
    "profile", llvm::cl::desc("verification profile: semantic|generated"),
    llvm::cl::init("semantic"), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> SafetyLevel(
    "level", llvm::cl::desc("implemented safety level: p0-temporal-lifecycle"),
    llvm::cl::init("p0-temporal-lifecycle"), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> PolicyPath(
    "policy", llvm::cl::desc("effective proof policy JSON"),
    llvm::cl::init("cand-policy.json"), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> BaseRef(
    "base", llvm::cl::desc("trusted base git ref for policy comparison"),
    llvm::cl::init(""), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> BasePolicyPath(
    "base-policy", llvm::cl::desc("base proof-policy JSON file"),
    llvm::cl::init(""), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> EvidencePath(
    "emit-evidence", llvm::cl::desc("write deterministic evidence JSON"),
    llvm::cl::init(""), llvm::cl::cat(CandCategory));

int CandExecutableAnchor = 0;
std::string CandExecutableSha256;
std::string CandExecutablePath;
std::string TrustedBaseCommit;

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
    std::string owner_storage;
    std::string transition;
    std::string borrow_storage;
    std::string borrow_kind;
    std::string borrow_origin;
    Location borrow_created;
    Location parent_invalidated;
    Location move_location;
    Location primary;
    std::vector<TraceEvent> trace;
};

struct Unsupported {
    std::string kind;
    std::string symbol;
    Location primary;
};

enum class ReturnEffect { None, Owned, BorrowFromArg, Unknown };
enum class ParamEffect { None, Borrow, TakeOwnership, Destroy, Unknown };
enum class SummaryOrigin { BodyVerified, BuiltinTrusted, ExternalTrusted, CandidateUntrusted, Unknown };

struct FunctionSummary {
    const FunctionDecl *function = nullptr;
    ReturnEffect return_effect = ReturnEffect::None;
    std::optional<unsigned> return_borrow_arg;
    std::vector<ParamEffect> params;
    SummaryOrigin origin = SummaryOrigin::Unknown;
    bool conflict = false;
};

bool hasCandAnnotation(const clang::Decl *decl, llvm::StringRef name) {
    if (decl == nullptr) return false;
    for (const clang::Attr *attr : decl->attrs()) {
        const auto *annotate = dyn_cast<AnnotateAttr>(attr);
        if (annotate && annotate->getAnnotation() == name) return true;
    }
    return false;
}

std::optional<unsigned> borrowReturnParameter(const FunctionDecl *function) {
    if (!function) return std::nullopt;
    constexpr llvm::StringLiteral prefix = "cand:returns_borrow_from:";
    for (const FunctionDecl *decl = function; decl; decl = decl->getPreviousDecl()) {
        for (const clang::Attr *attr : decl->attrs()) {
            const auto *annotate = dyn_cast<AnnotateAttr>(attr);
            if (!annotate) continue;
            llvm::StringRef value = annotate->getAnnotation();
            if (!value.consume_front(prefix)) continue;
            unsigned index = 0;
            if (value.empty()) return std::nullopt;
            if (value.getAsInteger(10, index)) return std::nullopt;
            return index;
        }
    }
    return std::nullopt;
}

class SummaryStore {
public:
    void add(const FunctionDecl *function, FunctionSummary summary) {
        summaries_[function->getNameAsString()] = std::move(summary);
    }
    void set(llvm::StringRef name, FunctionSummary summary) { summaries_[name.str()] = std::move(summary); }
    const FunctionSummary *find(const FunctionDecl *function) const {
        if (!function) return nullptr;
        auto it = summaries_.find(function->getNameAsString());
        return it == summaries_.end() ? nullptr : &it->second;
    }
    const FunctionSummary *find(llvm::StringRef name) const {
        auto it = summaries_.find(name.str());
        return it == summaries_.end() ? nullptr : &it->second;
    }
private:
    std::map<std::string, FunctionSummary> summaries_;
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
    void noteDependency(std::string path) { dependencies_.insert(std::move(path)); }
    const std::set<std::string> &dependencies() const { return dependencies_; }

    // A translation unit that produced compilation errors must never receive
    // a C& verdict: the analysis ran on a recovered (not real) AST.
    void noteFrontendError() { frontend_error_ = true; }
    bool hasFrontendError() const { return frontend_error_; }
    void noteContractError() { contract_error_ = true; }
    bool hasContractError() const { return contract_error_; }

    void noteTrackedHeapObjects(std::size_t count) {
        tracked_heap_objects_ += static_cast<unsigned>(count);
    }

    void noteOwnershipTransition() { ++ownership_transitions_; }
    void noteUnsupportedOwnershipTransfer() { ++unsupported_ownership_transfers_; }
    void noteBorrow(std::string kind) {
        ++borrows_created_;
        if (kind == "mutable") ++mutable_borrows_;
        else ++shared_borrows_;
    }
    void noteBorrowInvalidated() { ++invalidated_borrows_; }
    void noteUnsupportedBorrow() { ++unsupported_borrows_; }

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

    llvm::json::Object jsonObject() const {
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
            obj["safety_level"] = "p0-temporal-lifecycle";
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
            if (!finding.owner_storage.empty()) {
                obj["owner_storage"] = finding.owner_storage;
            }
            if (!finding.transition.empty()) {
                obj["transition"] = finding.transition;
            }
            if (!finding.borrow_storage.empty()) {
                llvm::json::Object borrow;
                borrow["storage"] = finding.borrow_storage;
                borrow["kind"] = finding.borrow_kind;
                borrow["origin"] = finding.borrow_origin;
                borrow["lifetime_source"] = "object:" + finding.object_id;
                borrow["state"] = finding.parent_invalidated.file.empty() ? "Live" : "Invalid";
                borrow["created_at"] = locationJson(finding.borrow_created);
                obj["borrow"] = std::move(borrow);
                obj["invalid_access"] = locationJson(finding.primary);
            }
            if (!finding.parent_invalidated.file.empty()) {
                llvm::json::Object parent;
                parent["object_id"] = finding.object_id;
                parent["invalidated_at"] = locationJson(finding.parent_invalidated);
                obj["parent"] = std::move(parent);
            }
            if (!finding.move_location.file.empty()) {
                obj["move_location"] = locationJson(finding.move_location);
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
        coverage["ownership_transitions"] =
            static_cast<std::int64_t>(ownership_transitions_);
        coverage["unsupported_ownership_transfers"] =
            static_cast<std::int64_t>(unsupported_ownership_transfers_);
        coverage["ownership_rule_set"] = "p1-unique-ownership-v1";
        llvm::json::Object borrow_analysis;
        borrow_analysis["borrows_created"] = static_cast<std::int64_t>(borrows_created_);
        borrow_analysis["shared_borrows"] = static_cast<std::int64_t>(shared_borrows_);
        borrow_analysis["mutable_borrows"] = static_cast<std::int64_t>(mutable_borrows_);
        borrow_analysis["invalidated_borrows"] = static_cast<std::int64_t>(invalidated_borrows_);
        borrow_analysis["unsupported_borrow_operations"] = static_cast<std::int64_t>(unsupported_borrows_);
        borrow_analysis["rule_set"] = "p2-borrow-lifetime-v1";
        root["borrow_analysis"] = std::move(borrow_analysis);
        root["coverage"] = std::move(coverage);

        return root;
    }

    void printJson() const {
        llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(jsonObject()));
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
    std::set<std::string> dependencies_;
    bool frontend_error_ = false;
    bool contract_error_ = false;
    unsigned functions_analyzed_ = 0;
    unsigned tracked_heap_objects_ = 0;
    unsigned ownership_transitions_ = 0;
    unsigned unsupported_ownership_transfers_ = 0;
    unsigned borrows_created_ = 0;
    unsigned shared_borrows_ = 0;
    unsigned mutable_borrows_ = 0;
    unsigned invalidated_borrows_ = 0;
    unsigned unsupported_borrows_ = 0;
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
// Moved/MaybeMoved are storage capabilities in PointerRelation, not heap
// lifetime states: a moved-from storage can share a live object with its new
// authoritative owner without making that object dead.
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

enum class BorrowKind { Shared, Mutable };
enum class BorrowState { Live, Invalid, MaybeInvalid, Unknown };

struct BorrowInfo {
    unsigned parent_object_id = std::numeric_limits<unsigned>::max();
    BorrowKind kind = BorrowKind::Shared;
    BorrowState state = BorrowState::Unknown;
    std::string origin;
    std::string lifetime_source;
    Location created;
    Location invalidated;

    bool operator==(const BorrowInfo &other) const {
        return parent_object_id == other.parent_object_id && kind == other.kind &&
               state == other.state && origin == other.origin &&
               lifetime_source == other.lifetime_source && sameLocation(created, other.created) &&
               sameLocation(invalidated, other.invalidated);
    }
};

const char *borrowKindName(BorrowKind kind) {
    return kind == BorrowKind::Mutable ? "mutable" : "shared";
}

const char *borrowStateName(BorrowState state) {
    switch (state) {
    case BorrowState::Live: return "Live";
    case BorrowState::Invalid: return "Invalid";
    case BorrowState::MaybeInvalid: return "MaybeInvalid";
    case BorrowState::Unknown: return "Unknown";
    }
    return "Unknown";
}

// PointerRelation also carries the per-storage ownership capability. The
// heap object's lifetime remains in ObjectInfo; Moved means this storage is
// no longer the authoritative owner while another storage may still own the
// same live object.
enum class PointerRelation { Owner, Alias, Moved, MaybeMoved, Null, MaybeNull, Unknown };

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
        const auto moved = [](PointerRelation relation) {
            return relation == PointerRelation::Moved ||
                   relation == PointerRelation::MaybeMoved;
        };
        if (moved(a.relation) || moved(b.relation)) {
            return {a.object_id, PointerRelation::MaybeMoved, relation_location};
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
    std::map<StorageId, BorrowInfo> borrows;

    bool operator==(const FlowState &other) const {
        return storages == other.storages && objects == other.objects && borrows == other.borrows;
    }
};

BorrowState joinBorrowState(BorrowState a, BorrowState b) {
    if (a == b) return a;
    if (a == BorrowState::Unknown || b == BorrowState::Unknown) return BorrowState::Unknown;
    if (a == BorrowState::Live && b == BorrowState::Live) return BorrowState::Live;
    if (a == BorrowState::Invalid && b == BorrowState::Invalid) return BorrowState::Invalid;
    return BorrowState::MaybeInvalid;
}

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
    result.borrows = a.borrows;
    for (const auto &entry : b.borrows) {
        auto it = result.borrows.find(entry.first);
        if (it == result.borrows.end()) {
            result.borrows.emplace(entry.first, entry.second);
        } else {
            it->second.state = joinBorrowState(it->second.state, entry.second.state);
            it->second.created = minLocation(it->second.created, entry.second.created);
            if (!entry.second.invalidated.file.empty())
                it->second.invalidated = minLocation(it->second.invalidated, entry.second.invalidated);
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
    FlowAnalyzer(ASTContext &context, Collector &collector, const SummaryStore &summaries)
        : context_(context), source_manager_(context.getSourceManager()),
          collector_(collector), summaries_(summaries) {}

    void analyze(const FunctionDecl &function) {
        const Stmt *body = function.getBody();
        if (body == nullptr) {
            return;
        }
        collector_.noteFunction();
        current_summary_ = summaries_.find(&function);
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

    std::string sourceText(clang::SourceRange range) const {
        if (range.isInvalid()) return {};
        const SourceLocation begin = source_manager_.getExpansionLoc(range.getBegin());
        const SourceLocation end = source_manager_.getExpansionLoc(range.getEnd());
        if (begin.isInvalid() || end.isInvalid()) return {};
        return clang::Lexer::getSourceText(
                   clang::CharSourceRange::getTokenRange(begin, end), source_manager_,
                   context_.getLangOpts())
            .str();
    }

    bool sourceContainsMove(clang::SourceRange range) const {
        const std::string text = sourceText(range);
        bool line_comment = false, block_comment = false, string = false, character = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            const char next = i + 1 < text.size() ? text[i + 1] : '\0';
            if (line_comment) {
                if (c == '\n') line_comment = false;
                continue;
            }
            if (block_comment) {
                if (c == '*' && next == '/') { block_comment = false; ++i; }
                continue;
            }
            if (string) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') string = false;
                continue;
            }
            if (character) {
                if (c == '\\') { ++i; continue; }
                if (c == '\'') character = false;
                continue;
            }
            if (c == '/' && next == '/') { line_comment = true; ++i; continue; }
            if (c == '/' && next == '*') { block_comment = true; ++i; continue; }
            if (c == '"') { string = true; continue; }
            if (c == '\'') { character = true; continue; }
            constexpr llvm::StringLiteral marker = "CAND_MOVE";
            if (text.compare(i, marker.size(), marker.data()) != 0) continue;
            const bool left_boundary = i == 0 ||
                !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_');
            std::size_t j = i + marker.size();
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
            if (left_boundary && j < text.size() && text[j] == '(') return true;
        }
        return false;
    }

    bool isExplicitMove(const Expr *expr) const {
        if (expr == nullptr) return false;
        if (!expr->getBeginLoc().isMacroID() && !expr->getEndLoc().isMacroID()) return false;
        const auto hasMoveMacro = [this](SourceLocation loc) {
            while (loc.isMacroID()) {
                if (clang::Lexer::getImmediateMacroName(
                        loc, source_manager_, context_.getLangOpts()) == "CAND_MOVE" ||
                    clang::Lexer::getImmediateMacroNameForDiagnostics(
                        loc, source_manager_, context_.getLangOpts()) == "CAND_MOVE") {
                    return true;
                }
                loc = source_manager_.getImmediateMacroCallerLoc(loc);
            }
            return false;
        };
        // The end of an enclosing assignment/call may point into the macro
        // expansion even when the expression itself is not a move. Only the
        // beginning can identify a move operand without classifying its
        // parent statement as a standalone move.
        if (hasMoveMacro(expr->getBeginLoc())) return true;
        const std::string text = sourceText(expr->getSourceRange());
        if (text == "CAND_MOVE") return true;
        const std::size_t first = text.find_first_not_of(" \t\r\n");
        return first != std::string::npos &&
               sourceContainsMove(clang::SourceRange(expr->getBeginLoc(), expr->getEndLoc())) &&
               text.compare(first, std::strlen("CAND_MOVE"), "CAND_MOVE") == 0;
    }

    void noteOwnershipUnsupported(const Stmt &stmt, llvm::StringRef kind) {
        if (emitting_) collector_.noteUnsupportedOwnershipTransfer();
        markUnsupported(stmt, kind);
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

    const FunctionSummary *summaryFor(const CallExpr &call) const {
        return summaries_.find(call.getDirectCallee());
    }

    std::optional<unsigned> currentParameter(const Expr *expr) const {
        if (!current_summary_ || !expr) return std::nullopt;
        expr = expr->IgnoreParenCasts();
        const auto *ref = dyn_cast<DeclRefExpr>(expr);
        const auto *param = ref ? dyn_cast<ParmVarDecl>(ref->getDecl()) : nullptr;
        if (!param || !current_summary_->function) return std::nullopt;
        for (unsigned i = 0; i < current_summary_->function->param_size(); ++i)
            if (current_summary_->function->getParamDecl(i) == param) return i;
        return std::nullopt;
    }

    bool isModeledPointerCall(const CallExpr &call) const {
        return isAllocatorCall(call) || summaryFor(call) != nullptr;
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
        if (isModeledPointerCall(*call)) {
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

    bool containsOwnedPointerCall(const Expr *expr) const {
        if (!expr) return false;
        if (const auto *call = asCall(expr)) {
            const auto *summary = summaryFor(*call);
            if (summary && summary->return_effect == ReturnEffect::Owned) return true;
        }
        for (const Stmt *child : expr->children()) {
            const auto *e = llvm::dyn_cast_or_null<Expr>(child);
            if (e && containsOwnedPointerCall(e)) return true;
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

    bool containsParameterStorage(const Expr *expr) const {
        if (expr == nullptr) return false;
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (isa<ParmVarDecl>(ref->getDecl())) return true;
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsParameterStorage(child_expr)) return true;
            }
        }
        return false;
    }

    bool typeMayContainPointer(clang::QualType type) const {
        if (type.isNull()) return false;
        type = type.getCanonicalType();
        if (type->isPointerType()) return true;
        if (const auto *array = context_.getAsArrayType(type))
            return typeMayContainPointer(array->getElementType());
        if (const auto *record = type->getAs<clang::RecordType>()) {
            for (const clang::FieldDecl *field : record->getDecl()->fields())
                if (typeMayContainPointer(field->getType())) return true;
        }
        return false;
    }

    bool mayWritePointerStorage(const Expr *arg) const {
        if (arg == nullptr) return false;
        const Expr *stripped = arg->IgnoreParenCasts();
        if (const auto *unary = dyn_cast<UnaryOperator>(stripped)) {
            if (unary->getOpcode() == clang::UO_AddrOf)
                return typeMayContainPointer(unary->getSubExpr()->getType());
        }
        return arg->getType()->isPointerType() &&
               typeMayContainPointer(arg->getType()->getPointeeType());
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

    void markUnsupportedAt(SourceLocation loc, llvm::StringRef kind) {
        emitUnsupported({kind.str(), "", location(loc)});
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

    void reportUseAfterMove(const StorageBinding &binding, const ObjectInfo &object,
                            const StorageId &access, SourceLocation use_loc,
                            const FlowState &state, bool possible) {
        Finding finding;
        finding.id = "CAND-O001";
        finding.rule_id = "ownership.no-use-after-move";
        finding.message = possible ? "possible use after move" : "use after move";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.certainty = possible ? "possible" : "definite";
        finding.state_before = possible ? "MaybeMoved" : "Moved";
        finding.object_id = objectName(binding.object_id);
        finding.access_storage = storageName(access);
        finding.transition = "move";
        finding.move_location = binding.relation_location;
        for (const auto &entry : state.storages) {
            if (entry.second.object_id == binding.object_id &&
                entry.second.relation == PointerRelation::Owner) {
                finding.owner_storage = storageName(entry.first);
                break;
            }
        }
        finding.primary = location(use_loc);
        finding.trace.push_back({"allocation", "Owned", object.allocation});
        if (!binding.relation_location.file.empty())
            finding.trace.push_back({"move", possible ? "MaybeMoved" : "Moved",
                                     binding.relation_location});
        finding.trace.push_back({"invalid-access", finding.state_before, location(use_loc)});
        emitFinding(std::move(finding));
    }

    void reportOwnershipViolation(const char *id, const char *rule, const char *message,
                                  const StorageBinding &binding, const ObjectInfo &object,
                                  const StorageId &storage, SourceLocation loc,
                                  const FlowState &state, bool possible = false,
                                  std::optional<SourceLocation> operation_loc = std::nullopt) {
        Finding finding;
        finding.id = id;
        finding.rule_id = rule;
        finding.message = message;
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.certainty = possible ? "possible" : "definite";
        finding.state_before = binding.relation == PointerRelation::MaybeMoved
                                   ? "MaybeMoved"
                                   : binding.relation == PointerRelation::Moved
                                         ? "Moved"
                                         : stateName(object.state);
        finding.object_id = objectName(binding.object_id);
        finding.access_storage = storageName(storage);
        finding.destroy_storage = storageName(storage);
        finding.transition = "ownership";
        finding.move_location = operation_loc ? location(*operation_loc) : binding.relation_location;
        for (const auto &entry : state.storages) {
            if (entry.second.object_id == binding.object_id &&
                entry.second.relation == PointerRelation::Owner) {
                finding.owner_storage = storageName(entry.first);
                break;
            }
        }
        finding.primary = location(loc);
        finding.trace.push_back({"allocation", "Owned", object.allocation});
        if (!finding.move_location.file.empty())
            finding.trace.push_back({"move", "Moved", finding.move_location});
        finding.trace.push_back({"invalid-ownership-operation", finding.state_before,
                                 location(loc)});
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

    // ---- borrow helpers and transfer functions ------------------------

    const BorrowInfo *borrowFor(const Expr *expr, const FlowState &state) const {
        const auto storage = storageFor(expr);
        if (!storage) return nullptr;
        const auto it = state.borrows.find(*storage);
        return it == state.borrows.end() ? nullptr : &it->second;
    }

    bool isBorrowCast(const Expr *expr, const FlowState &state) const {
        const Expr *candidate = expr ? expr->IgnoreParens() : nullptr;
        const auto *cast = candidate ? dyn_cast<clang::ExplicitCastExpr>(candidate) : nullptr;
        return cast != nullptr && borrowFor(cast->getSubExpr(), state) != nullptr;
    }

    void reportBorrowFinding(const char *id, const char *rule, const char *message,
                             const StorageId &storage, const BorrowInfo &borrow,
                             SourceLocation primary, const FlowState &state) {
        Finding finding;
        finding.id = id;
        finding.rule_id = rule;
        finding.message = message;
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.object_id = objectName(borrow.parent_object_id);
        finding.access_storage = storageName(storage);
        finding.borrow_storage = storageName(storage);
        finding.borrow_kind = borrowKindName(borrow.kind);
        finding.borrow_origin = borrow.origin;
        finding.borrow_created = borrow.created;
        finding.parent_invalidated = borrow.invalidated;
        finding.primary = location(primary);
        finding.trace.push_back({"borrow-created", borrowStateName(borrow.state), borrow.created});
        const auto object = state.objects.find(borrow.parent_object_id);
        if (object != state.objects.end()) {
            finding.trace.push_back({"parent", stateName(object->second.state), object->second.allocation});
            if (object->second.destruction_known)
                finding.trace.push_back({"parent-destroyed", "Invalid", object->second.destruction});
        }
        finding.trace.push_back({"borrow-access", borrowStateName(borrow.state), location(primary)});
        emitFinding(std::move(finding));
    }

    bool createBorrow(const StorageId &storage, unsigned parent, BorrowKind kind,
                      llvm::StringRef origin, llvm::StringRef lifetime_source,
                      SourceLocation created_loc, FlowState &state) {
        const auto object = state.objects.find(parent);
        if (parent == kUnknownObjectId || object == state.objects.end() ||
            object->second.state == ObjectState::Dead || object->second.state == ObjectState::MaybeDead) {
            markUnsupportedAt(created_loc, "borrow-unknown-parent");
            if (emitting_) collector_.noteUnsupportedBorrow();
            return false;
        }
        for (const auto &entry : state.borrows) {
            if (entry.first == storage || entry.second.parent_object_id != parent ||
                entry.second.state != BorrowState::Live) continue;
            if (kind == BorrowKind::Mutable || entry.second.kind == BorrowKind::Mutable) {
                reportBorrowFinding("CAND-B004", "p2-borrow-lifetime-v1",
                                    "conflicting mutable borrow", entry.first, entry.second,
                                    created_loc, state);
                return false;
            }
        }
        BorrowInfo info;
        info.parent_object_id = parent;
        info.kind = kind;
        info.state = BorrowState::Live;
        info.origin = origin.str();
        info.lifetime_source = lifetime_source.str();
        info.created = location(created_loc);
        state.borrows[storage] = std::move(info);
        known_borrow_storages_.insert(storage);
        if (emitting_) collector_.noteBorrow(borrowKindName(kind));
        return true;
    }

    void invalidateBorrows(unsigned parent, SourceLocation loc, FlowState &state) {
        for (auto &entry : state.borrows) {
            if (entry.second.parent_object_id != parent || entry.second.state != BorrowState::Live) continue;
            entry.second.state = BorrowState::Invalid;
            entry.second.invalidated = location(loc);
            if (emitting_) collector_.noteBorrowInvalidated();
        }
    }

    bool borrowLiveAfter(const StorageId &storage, const Stmt *point) const {
        const auto it = borrow_live_after_stmt_.find(point);
        if (it == borrow_live_after_stmt_.end()) {
            // Missing a program point is an analysis implementation gap. Keep
            // the destruction check fail-closed instead of assuming the
            // borrow is dead.
            return true;
        }
        return it->second.count(storage) != 0;
    }

    void expireBorrowUses(const Stmt *stmt, FlowState &state) {
        if (stmt == nullptr) return;
        for (auto it = state.borrows.begin(); it != state.borrows.end();) {
            if (!borrowLiveAfter(it->first, stmt)) it = state.borrows.erase(it);
            else ++it;
        }
    }

    bool checkBorrowAccess(const Expr *pointer_expr, SourceLocation access_loc,
                           const FlowState &state) {
        const auto storage = storageFor(pointer_expr);
        if (!storage) return false;
        const auto it = state.borrows.find(*storage);
        if (it == state.borrows.end()) return false;
        const BorrowInfo &borrow = it->second;
        const auto object = state.objects.find(borrow.parent_object_id);
        if (borrow.state == BorrowState::Invalid || borrow.state == BorrowState::MaybeInvalid ||
            (object != state.objects.end() &&
             (object->second.state == ObjectState::Dead || object->second.state == ObjectState::MaybeDead))) {
            reportBorrowFinding("CAND-B002", "p2-borrow-lifetime-v1",
                                "borrow used after parent death", *storage, borrow, access_loc, state);
            return true;
        } else if (borrow.state == BorrowState::Unknown) {
            markUnsupportedAt(access_loc, "unknown-borrow-state");
            if (emitting_) collector_.noteUnsupportedBorrow();
            return true;
        }
        return false;
    }

    void checkAccess(const Expr *pointer_expr, SourceLocation access_loc,
                     const FlowState &state) {
        if (checkBorrowAccess(pointer_expr, access_loc, state)) return;
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
            if (auto index = currentParameter(pointer_expr); index &&
                *index < current_summary_->params.size() &&
                (current_summary_->params[*index] == ParamEffect::Borrow ||
                 current_summary_->params[*index] == ParamEffect::TakeOwnership)) return;
            if (containsParameterStorage(pointer_expr)) {
                emitUnsupported({"unmodelled-pointer-parameter", "",
                                 location(access_loc)});
                return;
            }
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
        if (binding->relation == PointerRelation::Moved ||
            binding->relation == PointerRelation::MaybeMoved) {
            if (access_storage) {
                reportUseAfterMove(*binding, *object, *access_storage, access_loc, state,
                                   binding->relation == PointerRelation::MaybeMoved);
            } else {
                emitUnsupported({"unresolved-access-storage", "", location(access_loc)});
            }
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
            if (auto index = currentParameter(arg); index && *index < current_summary_->params.size() &&
                (current_summary_->params[*index] == ParamEffect::Destroy ||
                 current_summary_->params[*index] == ParamEffect::TakeOwnership)) return;
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
            if (auto index = currentParameter(arg); index && *index < current_summary_->params.size() &&
                (current_summary_->params[*index] == ParamEffect::Destroy ||
                 current_summary_->params[*index] == ParamEffect::TakeOwnership)) return;
        }
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
        for (const auto &entry : state.borrows) {
            if (entry.second.parent_object_id == binding.object_id &&
                entry.second.state == BorrowState::Live &&
                borrowLiveAfter(entry.first, &call)) {
                BorrowInfo borrow = entry.second;
                borrow.invalidated = location(call.getExprLoc());
                reportBorrowFinding("CAND-B001", "p2-borrow-lifetime-v1",
                                    "owner destroyed with live borrow", entry.first, borrow,
                                    call.getExprLoc(), state);
            }
        }
        if (binding.relation == PointerRelation::Moved ||
            binding.relation == PointerRelation::MaybeMoved) {
            reportOwnershipViolation(
                "CAND-O003", "ownership.destroy-from-non-owner",
                "destruction attempted through a moved-from owner", binding, *object,
                *destroy_storage, call.getExprLoc(), state,
                binding.relation == PointerRelation::MaybeMoved);
            return;
        }
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
            invalidateBorrows(binding.object_id, call.getExprLoc(), state);
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

    void destroyBinding(const Expr *arg, const CallExpr &call, FlowState &state) {
        const auto storage = storageFor(arg);
        if (!storage) {
            markUnsupported(call, "destroy-untracked-pointer");
            return;
        }
        auto it = state.storages.find(*storage);
        if (it == state.storages.end()) {
            markUnsupported(call, "destroy-untracked-pointer");
            return;
        }
        StorageBinding &binding = it->second;
        if (binding.object_id == kNullObjectId && binding.relation == PointerRelation::Null) return;
        if (binding.object_id == kUnknownObjectId || binding.object_id == kNullObjectId) {
            markUnsupported(call, "ambiguous-alias-target");
            return;
        }
        auto object_it = state.objects.find(binding.object_id);
        if (object_it == state.objects.end()) {
            markUnsupported(call, "destroy-unknown-ownership-state");
            return;
        }
        ObjectInfo &object = object_it->second;
        if (binding.relation == PointerRelation::Moved ||
            binding.relation == PointerRelation::MaybeMoved) {
            reportOwnershipViolation(
                "CAND-O003", "ownership.destroy-from-non-owner",
                "destruction attempted through a moved-from owner", binding, object,
                *storage, call.getExprLoc(), state,
                binding.relation == PointerRelation::MaybeMoved);
            return;
        }
        for (const auto &entry : state.borrows) {
            if (entry.second.parent_object_id == binding.object_id &&
                entry.second.state == BorrowState::Live &&
                borrowLiveAfter(entry.first, &call)) {
                BorrowInfo borrow = entry.second;
                borrow.invalidated = location(call.getExprLoc());
                reportBorrowFinding("CAND-B001", "p2-borrow-lifetime-v1",
                                    "owner destroyed with live borrow", entry.first, borrow,
                                    call.getExprLoc(), state);
            }
        }
        if (object.state == ObjectState::Owned) {
            object.state = ObjectState::Dead;
            object.destruction = location(call.getExprLoc());
            object.destruction_known = true;
            object.destruction_storage = storageName(*storage);
            invalidateBorrows(binding.object_id, call.getExprLoc(), state);
        } else if (object.state == ObjectState::Dead || object.state == ObjectState::MaybeDead) {
            reportDoubleDestroy(binding, object, *storage, call.getExprLoc(), object.state == ObjectState::Dead);
        } else {
            markUnsupported(call, "destroy-unknown-ownership-state");
        }
    }

    void reportMissingMove(const StorageBinding &binding, const ObjectInfo &object,
                           const StorageId &storage, const CallExpr &call,
                           const FlowState &) {
        Finding finding;
        finding.id = "CAND-O005";
        finding.rule_id = "ownership.missing-explicit-transfer";
        finding.message = "consuming call requires CAND_MOVE";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.object_id = objectName(binding.object_id);
        finding.access_storage = storageName(storage);
        finding.primary = location(call.getExprLoc());
        finding.trace.push_back({"allocation", "Owned", object.allocation});
        finding.trace.push_back({"missing-move", "Owned", location(call.getExprLoc())});
        emitFinding(std::move(finding));
    }

    bool moveBinding(const Expr *arg, const StorageId *destination,
                     SourceLocation move_loc, const CallExpr *call,
                     FlowState &state) {
        const auto source = storageFor(arg);
        if (!source) {
            if (call) noteOwnershipUnsupported(*call, "move-untracked-pointer");
            return false;
        }
        auto source_it = state.storages.find(*source);
        if (source_it == state.storages.end() ||
            source_it->second.object_id == kUnknownObjectId) {
            if (call) noteOwnershipUnsupported(*call, "move-unknown-ownership-state");
            return false;
        }
        StorageBinding &binding = source_it->second;
        if (binding.object_id == kNullObjectId && binding.relation == PointerRelation::Null) {
            if (destination) state.storages[*destination] =
                {kNullObjectId, PointerRelation::Null, location(move_loc)};
            return true;
        }
        auto object_it = state.objects.find(binding.object_id);
        if (object_it == state.objects.end()) {
            if (call) noteOwnershipUnsupported(*call, "move-unknown-ownership-state");
            return false;
        }
        ObjectInfo &object = object_it->second;
        if (binding.relation == PointerRelation::Moved ||
            binding.relation == PointerRelation::MaybeMoved) {
            reportOwnershipViolation(
                "CAND-O002", "ownership.double-move", "object moved more than once",
                binding, object, *source, move_loc, state,
                binding.relation == PointerRelation::MaybeMoved, move_loc);
            return false;
        }
        if (object.state == ObjectState::Dead || object.state == ObjectState::MaybeDead) {
            reportOwnershipViolation(
                "CAND-O003", "ownership.move-from-dead", "move attempted from a dead object",
                binding, object, *source, move_loc, state,
                object.state == ObjectState::MaybeDead, move_loc);
            return false;
        }
        if (binding.relation != PointerRelation::Owner) {
            reportOwnershipViolation(
                "CAND-O004", "ownership.conflicting-owner",
                "ownership move requires the authoritative owner", binding, object,
                *source, move_loc, state, false, move_loc);
            return false;
        }
        if (destination) {
            if (*destination == *source) {
                reportOwnershipViolation(
                    "CAND-O004", "ownership.conflicting-owner",
                    "an owner cannot move into the same storage", binding, object,
                    *source, move_loc, state, false, move_loc);
                return false;
            }
            auto destination_it = state.storages.find(*destination);
            if (destination_it != state.storages.end() &&
                destination_it->second.object_id != kNullObjectId) {
                const ObjectInfo *old = objectFor(&destination_it->second, state);
                if (old && (old->state == ObjectState::Owned ||
                            old->state == ObjectState::MaybeDead ||
                            old->state == ObjectState::Unknown)) {
                    reportOwnershipViolation(
                        "CAND-O004", "ownership.conflicting-owner",
                        "owner storage overwritten while its object is live",
                        destination_it->second, *old, *destination, move_loc, state,
                        old->state == ObjectState::MaybeDead, move_loc);
                    return false;
                }
            }
        }
        binding.relation = PointerRelation::Moved;
        binding.relation_location = location(move_loc);
        if (destination) {
            state.storages[*destination] =
                {binding.object_id, PointerRelation::Owner, location(move_loc)};
        }
        if (emitting_) collector_.noteOwnershipTransition();
        return true;
    }

    bool strictOwnershipProfile() const {
        return AgentMode || ProfileName == "generated";
    }

    void transferBinding(const Expr *arg, const CallExpr &call, bool explicit_move,
                         FlowState &state, bool require_explicit_move) {
        const auto storage = storageFor(arg);
        if (!storage) {
            markUnsupported(call, "transfer-untracked-pointer");
            return;
        }
        const auto binding = state.storages.find(*storage);
        if (binding == state.storages.end() || binding->second.object_id == kUnknownObjectId) {
            markUnsupported(call, "transfer-unknown-ownership-state");
            return;
        }
        if (binding->second.object_id == kNullObjectId &&
            binding->second.relation == PointerRelation::Null) return;
        if (explicit_move) {
            moveBinding(arg, nullptr, call.getExprLoc(), &call, state);
            return;
        }
        if (strictOwnershipProfile()) {
            const auto object = state.objects.find(binding->second.object_id);
            if (object != state.objects.end())
                reportMissingMove(binding->second, object->second, *storage, call, state);
            else
                noteOwnershipUnsupported(call, "missing-explicit-transfer");
            return;
        }
        // CAND_TAKES is an explicit ownership boundary in every profile. The
        // legacy profile may preserve old unannotated-call behavior, but it
        // must not turn a missing CAND_MOVE into a verified PASS.
        if (require_explicit_move)
            markUnsupported(call, "missing-explicit-transfer");
        const auto object = state.objects.find(binding->second.object_id);
        if (object == state.objects.end() || object->second.state != ObjectState::Owned) {
            markUnsupported(call, "transfer-unknown-ownership-state");
            return;
        }
        // The callee now owns the object, but may retain or destroy it. Do not
        // conflate transfer with destruction: future caller accesses become
        // unknown rather than being optimistically treated as live or dead.
        object->second.state = ObjectState::Unknown;
        object->second.destruction_known = false;
        object->second.destruction = {};
        object->second.destruction_storage.clear();
    }

    void handleCall(const CallExpr &call, FlowState &state) {
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
        if (const FunctionSummary *summary = summaryFor(call)) {
            if (summary->conflict) {
                markUnsupported(call, "contract-body-conflict");
                return;
            }
            if (summary->return_effect == ReturnEffect::Unknown) {
                markUnsupported(call, "unknown-pointer-return-ownership");
                return;
            }
            for (unsigned i = 0; i < call.getNumArgs() && i < summary->params.size(); ++i) {
                if (summary->params[i] == ParamEffect::Destroy) {
                    const auto parameter = currentParameter(call.getArg(i));
                    if (!(parameter && current_summary_ && *parameter < current_summary_->params.size() &&
                          (current_summary_->params[*parameter] == ParamEffect::Destroy ||
                           current_summary_->params[*parameter] == ParamEffect::TakeOwnership)))
                        destroyBinding(call.getArg(i), call, state);
                }
                else if (summary->params[i] == ParamEffect::Borrow) {
                    if (isExplicitMove(call.getArg(i)))
                        markUnsupported(call, "move-to-non-consuming-parameter");
                    else
                        checkAccess(call.getArg(i), call.getExprLoc(), state);
                }
                else if (summary->params[i] == ParamEffect::TakeOwnership &&
                         (containsTrackedStorage(call.getArg(i), state) ||
                          isExplicitMove(call.getArg(i))))
                    transferBinding(call.getArg(i), call,
                                    isExplicitMove(call.getArg(i)), state,
                                    summary->origin == SummaryOrigin::BodyVerified);
                else if (isExplicitMove(call.getArg(i)))
                    markUnsupported(call, "move-to-non-consuming-parameter");
                else if (summary->params[i] == ParamEffect::Unknown && containsTrackedStorage(call.getArg(i), state))
                    markUnsupported(call, "unknown-call-with-tracked-pointer");
            }
            return;
        }
        if (call.getType()->isPointerType()) {
            noteUnknownPointerCall(call);
        }
        bool tracked_argument = false;
        bool borrowed_argument = false;
        bool global_argument = false;
        bool pointer_output_argument = false;
        for (const Expr *arg : call.arguments()) {
            tracked_argument = tracked_argument || containsTrackedStorage(arg, state);
            borrowed_argument = borrowed_argument || borrowFor(arg, state) != nullptr;
            global_argument = global_argument || containsGlobalStorage(arg);
            pointer_output_argument = pointer_output_argument || mayWritePointerStorage(arg);
            if (asUnknownPointerCall(arg) != nullptr) {
                noteUnknownPointerCallIn(arg);
            }
        }
        if (global_argument) {
            markUnsupported(call, "global-or-static-pointer-storage");
        }
        if (pointer_output_argument) {
            markUnsupported(call, "unknown-call-with-pointer-output");
        }
        if (!call.getType()->isPointerType() && typeMayContainPointer(call.getType())) {
            markUnsupported(call, "unknown-aggregate-return-ownership");
        }
        if (tracked_argument) {
            std::string kind = "unknown-call-with-tracked-pointer";
            if (const FunctionDecl *callee = call.getDirectCallee()) {
                kind += ":" + callee->getNameAsString();
            } else {
                kind += ":indirect";
            }
            markUnsupported(call, kind);
            if (borrowed_argument) {
                std::string retention = "unknown-call-borrow-retention";
                if (const FunctionDecl *callee = call.getDirectCallee())
                    retention += ":" + callee->getNameAsString();
                else
                    retention += ":indirect";
                markUnsupported(call, retention);
                if (emitting_) collector_.noteUnsupportedBorrow();
            }
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

    void bindSummaryReturn(const StorageId &storage, const CallExpr &call,
                           FlowState &state) {
        const FunctionSummary *summary = summaryFor(call);
        if (!summary || summary->conflict || summary->origin == SummaryOrigin::Unknown) {
            markUnsupported(call, "unknown-pointer-return-ownership");
            return;
        }
        if (summary->return_effect == ReturnEffect::Owned) {
            const unsigned id = objectIdForAllocation(&call);
            state.storages[storage] = {id, PointerRelation::Owner, location(call.getExprLoc())};
            auto &object = state.objects[id];
            object.state = ObjectState::Owned;
            object.allocation = location(call.getExprLoc());
            bound_objects_.insert(id);
            return;
        }
        if (summary->return_effect == ReturnEffect::BorrowFromArg &&
            summary->return_borrow_arg && *summary->return_borrow_arg < call.getNumArgs()) {
            const auto source = storageFor(call.getArg(*summary->return_borrow_arg));
            if (source) {
                auto it = state.storages.find(*source);
                if (it != state.storages.end() && it->second.object_id != kUnknownObjectId) {
                    state.storages[storage] = {it->second.object_id, PointerRelation::Alias,
                                               location(call.getExprLoc())};
                    const std::string origin = "verified-summary:" +
                        (call.getDirectCallee() ? call.getDirectCallee()->getNameAsString() : "unknown");
                    createBorrow(storage, it->second.object_id, BorrowKind::Shared,
                                 origin, "object:" + objectName(it->second.object_id),
                                 call.getExprLoc(), state);
                    return;
                }
            }
        }
        markUnsupported(call, "unknown-pointer-return-ownership");
    }

    void handleDeclStmt(const DeclStmt &decl_stmt, FlowState &state) {
        for (const clang::Decl *decl : decl_stmt.decls()) {
            const auto *var = dyn_cast<VarDecl>(decl);
            if (var == nullptr || var->getInit() == nullptr) {
                continue;
            }
            const Expr *init = var->getInit();
            if (var->getType()->isPointerType() && isBorrowCast(init, state)) {
                markUnsupported(decl_stmt, "borrow-cast-transport");
                if (emitting_) collector_.noteUnsupportedBorrow();
                continue;
            }
            if (var->hasGlobalStorage() &&
                (var->getType()->isPointerType() || var->getType()->isAtomicType() ||
                 containsAllocationCall(init) || containsTrackedStorage(init, state) ||
                 containsUnknownPointerCall(init))) {
                markUnsupported(decl_stmt, "global-or-static-pointer-storage");
                continue;
            }
            if (!var->getType()->isPointerType()) {
                if (containsAllocationCall(init) || containsOwnedPointerCall(init)) {
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
            const bool explicit_shared_borrow = hasCandAnnotation(var, "cand:borrow_shared") ||
                                                hasCandAnnotation(var, "cand:borrow");
            const bool explicit_mutable_borrow = hasCandAnnotation(var, "cand:borrow_mut");
            if (explicit_shared_borrow || explicit_mutable_borrow) {
                if (const auto *call = asCall(init); call && summaryFor(*call) &&
                    summaryFor(*call)->return_effect == ReturnEffect::BorrowFromArg) {
                    bindSummaryReturn(storage, *call, state);
                    if (explicit_mutable_borrow && state.borrows.count(storage)) {
                        state.borrows.erase(storage);
                        const auto parent = state.storages[storage].object_id;
                        createBorrow(storage, parent, BorrowKind::Mutable, "explicit-annotation",
                                     "object:" + objectName(parent), init->getExprLoc(), state);
                    }
                    continue;
                }
                if (const auto source = storageFor(init)) {
                    const auto it = state.storages.find(*source);
                    if (it != state.storages.end() && it->second.object_id != kUnknownObjectId &&
                        it->second.object_id != kNullObjectId) {
                        state.storages[storage] = {it->second.object_id, PointerRelation::Alias,
                                                   location(init->getExprLoc())};
                        createBorrow(storage, it->second.object_id,
                                     explicit_mutable_borrow ? BorrowKind::Mutable : BorrowKind::Shared,
                                     "explicit-annotation", "object:" + objectName(it->second.object_id),
                                     init->getExprLoc(), state);
                        continue;
                    }
                }
                markUnsupported(decl_stmt, "borrow-unknown-parent");
                if (emitting_) collector_.noteUnsupportedBorrow();
                continue;
            }
            if (isExplicitMove(init)) {
                if (!storageFor(init) ||
                    !moveBinding(init, &storage, init->getExprLoc(), nullptr, state)) {
                    if (!storageFor(init)) noteOwnershipUnsupported(decl_stmt, "move-untracked-pointer");
                }
            } else if (isNullConstant(init)) {
                state.storages[storage] =
                    {kNullObjectId, PointerRelation::Null, location(init->getExprLoc())};
            } else if (isAllocationOrNull(init)) {
                // A declaration introduces a fresh object on every execution.
                bindAllocation(storage, init, state);
            } else if (const auto *call = asCall(init); call != nullptr && summaryFor(*call) != nullptr) {
                bindSummaryReturn(storage, *call, state);
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
                    const PointerRelation relation =
                        it->second.relation == PointerRelation::Moved
                            ? PointerRelation::Moved
                            : it->second.relation == PointerRelation::MaybeMoved
                                  ? PointerRelation::MaybeMoved
                                  : PointerRelation::Alias;
                    if (hasCandAnnotation(var, "cand:own") &&
                        relation != PointerRelation::Owner) {
                        const auto object = state.objects.find(it->second.object_id);
                        if (object != state.objects.end())
                            reportOwnershipViolation(
                                "CAND-O004", "ownership.conflicting-owner",
                                "CAND_OWN requires an explicit ownership move",
                                it->second, object->second, *source, init->getExprLoc(), state);
                    }
                    state.storages[storage] =
                        {it->second.object_id, relation,
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

        if (lhs_is_pointer && isBorrowCast(rhs, state)) {
            markUnsupported(binary, "borrow-cast-transport");
            if (emitting_) collector_.noteUnsupportedBorrow();
            return;
        }

        // P1 models local pointer storage only. A move into an aggregate
        // field or array element must not silently disappear.
        if (isExplicitMove(rhs) && lhs_is_pointer &&
            (var == nullptr || !var->getType()->isPointerType())) {
            noteOwnershipUnsupported(binary, "move-unsupported-destination-storage");
            return;
        }

        if (containsGlobalStorage(lhs)) {
            if (const BorrowInfo *borrow = borrowFor(rhs, state)) {
                const auto storage = storageFor(rhs);
                if (storage) {
                    reportBorrowFinding("CAND-B003", "p2-borrow-lifetime-v1",
                                        "borrow escapes into global or static storage",
                                        *storage, *borrow, binary.getExprLoc(), state);
                    return;
                }
            }
        }

        if (containsGlobalStorage(lhs) &&
            (lhs_is_pointer || containsAllocationCall(rhs) ||
             containsTrackedStorage(rhs, state) || containsPointerToIntegerCast(rhs))) {
            markUnsupported(binary, "global-or-static-pointer-storage");
            return;
        }

        if (binary.isCompoundAssignmentOp() && lhs_is_pointer) {
            markUnsupported(binary, "pointer-arithmetic-reassignment");
            if (lhs_storage) {
                state.storages[*lhs_storage] =
                    {kUnknownObjectId, PointerRelation::Unknown,
                     location(binary.getExprLoc())};
            }
            return;
        }

        if (var != nullptr && var->getType()->isPointerType()) {
            auto it = lhs_storage ? state.storages.find(*lhs_storage) : state.storages.end();
            if (isExplicitMove(rhs)) {
                if (!lhs_storage || !moveBinding(rhs, &*lhs_storage, binary.getOperatorLoc(), nullptr, state)) {
                    if (!lhs_storage || !storageFor(rhs))
                        noteOwnershipUnsupported(binary, "move-untracked-pointer");
                }
            } else if (isNullConstant(rhs)) {
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
            } else if (const auto *call = asCall(rhs); call != nullptr && summaryFor(*call) != nullptr) {
                const ObjectInfo *old = it == state.storages.end() ? nullptr
                    : objectFor(&it->second, state);
                if (old && (old->state == ObjectState::Owned ||
                            old->state == ObjectState::MaybeDead ||
                            old->state == ObjectState::Unknown))
                    markUnsupported(binary, "tracked-owner-overwrite");
                if (lhs_storage) bindSummaryReturn(*lhs_storage, *call, state);
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
                        const PointerRelation relation =
                            source_it->second.relation == PointerRelation::Moved
                                ? PointerRelation::Moved
                                : source_it->second.relation == PointerRelation::MaybeMoved
                                      ? PointerRelation::MaybeMoved
                                      : PointerRelation::Alias;
                        state.storages[*lhs_storage] =
                            {source_it->second.object_id, relation,
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
            } else if (const auto *call = asCall(rhs); call != nullptr && summaryFor(*call) != nullptr) {
                if (lhs_storage) bindSummaryReturn(*lhs_storage, *call, state);
                else if (summaryFor(*call)->return_effect == ReturnEffect::Owned)
                    emitUnsupported({"allocation-to-untracked-storage:" + untrackedStorageKind(lhs), "", location(binary.getExprLoc())});
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
                    const PointerRelation relation =
                        source_it->second.relation == PointerRelation::Moved
                            ? PointerRelation::Moved
                            : source_it->second.relation == PointerRelation::MaybeMoved
                                  ? PointerRelation::MaybeMoved
                                  : PointerRelation::Alias;
                    state.storages[*lhs_storage] =
                        {source_it->second.object_id, relation,
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
        } else if ((containsAllocationCall(rhs) || containsOwnedPointerCall(rhs)) && !containsTrackedStorage(rhs, state)) {
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
        if (const BorrowInfo *borrow = borrowFor(ret, state)) {
            if (!(current_summary_ &&
                  current_summary_->return_effect == ReturnEffect::BorrowFromArg)) {
                const auto storage = storageFor(ret);
                if (storage) {
                    reportBorrowFinding("CAND-B003", "p2-borrow-lifetime-v1",
                                        "borrow escapes through an undeclared return",
                                        *storage, *borrow, return_stmt.getReturnLoc(), state);
                    return;
                }
            }
        }
        if (ret->getType()->isPointerType() && containsParameterStorage(ret) && !storageFor(ret) &&
            !(current_summary_ && current_summary_->return_effect == ReturnEffect::BorrowFromArg)) {
            markUnsupported(return_stmt, "unmodelled-pointer-parameter");
            return;
        }
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
            bool live_owned_return = false;
            if (current_summary_ && current_summary_->return_effect == ReturnEffect::Owned) {
                if (const auto storage = storageFor(ret)) {
                    const auto binding = state.storages.find(*storage);
                    if (binding != state.storages.end()) {
                        const auto object = state.objects.find(binding->second.object_id);
                        live_owned_return = object != state.objects.end() && object->second.state == ObjectState::Owned;
                    }
                }
            }
            if (!live_owned_return)
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

    using BorrowLiveSet = std::set<StorageId>;

    void collectBorrowUses(const Expr *expr, BorrowLiveSet &uses) const {
        if (expr == nullptr) return;
        if (const auto storage = storageFor(expr);
            storage && known_borrow_storages_.count(*storage)) {
            uses.insert(*storage);
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = dyn_cast<Expr>(child))
                collectBorrowUses(child_expr, uses);
        }
    }

    void collectBorrowUses(const Stmt *stmt, BorrowLiveSet &uses) const {
        if (stmt == nullptr) return;
        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            collectBorrowUses(expr, uses);
            return;
        }
        if (const auto *decl = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *item : decl->decls()) {
                if (const auto *var = dyn_cast<VarDecl>(item))
                    collectBorrowUses(var->getInit(), uses);
            }
            return;
        }
        if (const auto *ret = dyn_cast<ReturnStmt>(stmt)) {
            collectBorrowUses(ret->getRetValue(), uses);
            return;
        }
        if (const auto *if_stmt = dyn_cast<IfStmt>(stmt)) {
            collectBorrowUses(if_stmt->getCond(), uses);
            return;
        }
        if (const auto *switch_stmt = dyn_cast<SwitchStmt>(stmt)) {
            collectBorrowUses(switch_stmt->getCond(), uses);
            return;
        }
        if (const auto *while_stmt = dyn_cast<WhileStmt>(stmt)) {
            collectBorrowUses(while_stmt->getCond(), uses);
            return;
        }
        if (const auto *for_stmt = dyn_cast<ForStmt>(stmt)) {
            collectBorrowUses(for_stmt->getInit(), uses);
            collectBorrowUses(for_stmt->getCond(), uses);
            collectBorrowUses(for_stmt->getInc(), uses);
            return;
        }
        if (const auto *do_stmt = dyn_cast<DoStmt>(stmt)) {
            collectBorrowUses(do_stmt->getCond(), uses);
        }
    }

    void collectBorrowKills(const Stmt *stmt, BorrowLiveSet &kills) const {
        if (stmt == nullptr) return;
        if (const auto *decl = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *item : decl->decls()) {
                const auto *var = dyn_cast<VarDecl>(item);
                if (var == nullptr || !var->getType()->isPointerType()) continue;
                const StorageId storage{StorageKind::LocalVariable, var, {}, -1};
                if (known_borrow_storages_.count(storage)) kills.insert(storage);
            }
            return;
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (!binary->isAssignmentOp()) return;
            const auto storage = storageFor(binary->getLHS());
            if (storage && known_borrow_storages_.count(*storage)) kills.insert(*storage);
            return;
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->getOpcode() == clang::UO_PreInc ||
                unary->getOpcode() == clang::UO_PostInc ||
                unary->getOpcode() == clang::UO_PreDec ||
                unary->getOpcode() == clang::UO_PostDec) {
                const auto storage = storageFor(unary->getSubExpr());
                if (storage && known_borrow_storages_.count(*storage)) kills.insert(*storage);
            }
        }
    }

    void collectBorrowLivenessEvents(const Stmt *stmt,
                                     std::vector<const Stmt *> &events) const {
        if (stmt == nullptr) return;
        if (const auto *decl = dyn_cast<DeclStmt>(stmt)) {
            events.push_back(decl);
            return;
        }
        if (const auto *ret = dyn_cast<ReturnStmt>(stmt)) {
            events.push_back(ret);
            return;
        }
        if (const auto *if_stmt = dyn_cast<IfStmt>(stmt)) {
            events.push_back(if_stmt->getCond());
            return;
        }
        if (const auto *switch_stmt = dyn_cast<SwitchStmt>(stmt)) {
            events.push_back(switch_stmt->getCond());
            return;
        }
        if (const auto *while_stmt = dyn_cast<WhileStmt>(stmt)) {
            events.push_back(while_stmt->getCond());
            return;
        }
        if (const auto *for_stmt = dyn_cast<ForStmt>(stmt)) {
            events.push_back(for_stmt->getInit());
            events.push_back(for_stmt->getCond());
            events.push_back(for_stmt->getInc());
            return;
        }
        if (const auto *do_stmt = dyn_cast<DoStmt>(stmt)) {
            events.push_back(do_stmt->getCond());
            return;
        }
        if (isa<CompoundStmt>(stmt) || isa<GotoStmt>(stmt) ||
            isa<IndirectGotoStmt>(stmt)) return;
        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            // CFG wrappers contain the real expression. Keep call/member/
            // assignment nodes as individual program points so destruction
            // can query liveness immediately after the operation.
            if (isa<CallExpr>(expr) || isa<BinaryOperator>(expr) ||
                isa<MemberExpr>(expr) || isa<ArraySubscriptExpr>(expr) ||
                isa<UnaryOperator>(expr) || isa<ConditionalOperator>(expr)) {
                events.push_back(expr);
                return;
            }
            bool nested = false;
            for (const Stmt *child : expr->children()) {
                if (child != nullptr) {
                    nested = true;
                    collectBorrowLivenessEvents(child, events);
                }
            }
            if (!nested) events.push_back(expr);
            return;
        }
        events.push_back(stmt);
    }

    BorrowLiveSet borrowLivenessTransfer(const CFGBlock &block,
                                         BorrowLiveSet live,
                                         bool record) {
        std::vector<const Stmt *> events;
        for (CFGBlock::const_iterator it = block.begin(); it != block.end(); ++it) {
            const CFGElement &element = *it;
            if (element.getKind() != CFGElement::Statement) continue;
            const std::optional<CFGStmt> cfg_stmt = element.getAs<CFGStmt>();
            if (cfg_stmt) collectBorrowLivenessEvents(cfg_stmt->getStmt(), events);
        }
        if (const Stmt *terminator = block.getTerminatorStmt()) {
            collectBorrowLivenessEvents(terminator, events);
        }
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            const Stmt *event = *it;
            if (event == nullptr) continue;
            if (record) borrow_live_after_stmt_[event] = live;
            BorrowLiveSet uses;
            BorrowLiveSet kills;
            collectBorrowUses(event, uses);
            collectBorrowKills(event, kills);
            for (const StorageId &storage : kills) live.erase(storage);
            live.insert(uses.begin(), uses.end());
        }
        return live;
    }

    void computeBorrowLiveness() {
        borrow_live_in_.clear();
        borrow_live_out_.clear();
        borrow_live_after_stmt_.clear();
        bool changed = false;
        do {
            changed = false;
            for (CFG::const_iterator it = cfg_->begin(); it != cfg_->end(); ++it) {
                const CFGBlock *block = *it;
                if (block == nullptr) continue;
                BorrowLiveSet out;
                for (CFGBlock::const_succ_iterator si = block->succ_begin();
                     si != block->succ_end(); ++si) {
                    const CFGBlock *successor = *si;
                    if (successor == nullptr) continue;
                    const BorrowLiveSet &successor_in = borrow_live_in_[successor->getBlockID()];
                    out.insert(successor_in.begin(), successor_in.end());
                }
                const BorrowLiveSet in = borrowLivenessTransfer(*block, out, false);
                if (borrow_live_out_[block->getBlockID()] != out) {
                    borrow_live_out_[block->getBlockID()] = out;
                    changed = true;
                }
                if (borrow_live_in_[block->getBlockID()] != in) {
                    borrow_live_in_[block->getBlockID()] = in;
                    changed = true;
                }
            }
        } while (changed);

        for (CFG::const_iterator it = cfg_->begin(); it != cfg_->end(); ++it) {
            const CFGBlock *block = *it;
            if (block == nullptr) continue;
            borrowLivenessTransfer(*block, borrow_live_out_[block->getBlockID()], true);
        }
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

        computeBorrowLiveness();

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
    const SummaryStore &summaries_;
    const CFG *cfg_ = nullptr;
    std::map<const CallExpr *, unsigned> allocation_sites_;
    std::map<const Expr *, unsigned> synthetic_allocation_sites_;
    std::set<const CallExpr *> loop_allocation_sites_;
    std::set<StorageId> known_borrow_storages_;
    std::map<unsigned, BorrowLiveSet> borrow_live_in_;
    std::map<unsigned, BorrowLiveSet> borrow_live_out_;
    std::map<const Stmt *, BorrowLiveSet> borrow_live_after_stmt_;
    unsigned next_fallback_id_ = 1;
    std::set<unsigned> bound_objects_;
    std::set<const VarDecl *> stack_pointers_;
    std::set<const Stmt *> unevaluated_;
    bool emitting_ = true;
    const FunctionSummary *current_summary_ = nullptr;
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

    if (const auto *expr = dyn_cast<Expr>(stmt); expr && isExplicitMove(expr)) {
        noteOwnershipUnsupported(*stmt, "standalone-move");
        return;
    }

    if (const auto *decl_stmt = dyn_cast<DeclStmt>(stmt)) {
        handleDeclStmt(*decl_stmt, state);
        for (const clang::Decl *decl : decl_stmt->decls()) {
            if (const auto *var = dyn_cast<VarDecl>(decl)) {
                if (const Expr *init = var->getInit()) {
                    if (isExplicitMove(init)) processed.insert(init);
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
            if (isExplicitMove(binary->getRHS())) processed.insert(binary->getRHS());
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
        for (const Expr *arg : call->arguments()) {
            if (isExplicitMove(arg)) processed.insert(arg);
            }
            recurseChildren(*call, state, processed);
            expireBorrowUses(call, state);
            return;
        }

    if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
        if (unary->getOpcode() == clang::UO_Deref) {
            checkAccess(unary->getSubExpr(), unary->getOperatorLoc(), state);
        } else if ((unary->getOpcode() == clang::UO_PreInc ||
                    unary->getOpcode() == clang::UO_PostInc ||
                    unary->getOpcode() == clang::UO_PreDec ||
                    unary->getOpcode() == clang::UO_PostDec) &&
                   unary->getSubExpr()->getType()->isPointerType()) {
            markUnsupported(*unary, "pointer-arithmetic-reassignment");
            if (const auto storage = storageFor(unary->getSubExpr())) {
                state.storages[*storage] =
                    {kUnknownObjectId, PointerRelation::Unknown,
                     location(unary->getOperatorLoc())};
            }
        }
        recurseChildren(*unary, state, processed);
        expireBorrowUses(unary, state);
        return;
    }

    if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(stmt)) {
        checkAccess(subscript->getBase(), subscript->getExprLoc(), state);
        recurseChildren(*subscript, state, processed);
        expireBorrowUses(subscript, state);
        return;
    }

    if (const auto *member = dyn_cast<MemberExpr>(stmt)) {
        if (member->isArrow()) {
            checkAccess(member->getBase(), member->getExprLoc(), state);
        }
        recurseChildren(*member, state, processed);
        expireBorrowUses(member, state);
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

class SummaryBuilder {
public:
    SummaryBuilder(const SummaryStore &old, SummaryStore &out, ASTContext &context)
        : old_(old), out_(out), context_(context) {}

    void build(const FunctionDecl &function) {
        FunctionSummary summary;
        summary.function = &function;
        summary.origin = SummaryOrigin::BodyVerified;
        summary.params.assign(function.param_size(), ParamEffect::None);
        if (hasCandAnnotation(&function, "cand:returns_own"))
            summary.return_effect = ReturnEffect::Owned;
        if (const auto borrow = borrowReturnParameter(&function)) {
            if (*borrow < function.param_size()) {
                summary.return_effect = ReturnEffect::BorrowFromArg;
                summary.return_borrow_arg = *borrow;
            } else {
                summary.conflict = true;
            }
        }
        for (unsigned i = 0; i < function.param_size(); ++i) {
            const ParmVarDecl *param = function.getParamDecl(i);
            if (hasCandAnnotation(param, "cand:takes"))
                summary.params[i] = ParamEffect::TakeOwnership;
            else if (hasCandAnnotation(param, "cand:destroys"))
                summary.params[i] = ParamEffect::Destroy;
            else if (hasCandAnnotation(param, "cand:borrow") ||
                     hasCandAnnotation(param, "cand:borrow_shared") ||
                     hasCandAnnotation(param, "cand:borrow_mut"))
                summary.params[i] = ParamEffect::Borrow;
        }
        const Stmt *body = function.getBody();
        if (!body) return;
        scan(body, function, summary, false);
        if (function.getReturnType()->isPointerType() && summary.return_effect == ReturnEffect::None)
            summary.return_effect = ReturnEffect::Unknown;
        if (summary.conflict) {
            summary.return_effect = ReturnEffect::Unknown;
            summary.return_borrow_arg.reset();
            std::fill(summary.params.begin(), summary.params.end(), ParamEffect::Unknown);
        }
        out_.add(&function, std::move(summary));
    }

private:
    static std::optional<unsigned> parameterIndex(const Expr *expr, const FunctionDecl &f) {
        expr = expr ? expr->IgnoreParenCasts() : nullptr;
        const auto *ref = expr ? dyn_cast<DeclRefExpr>(expr) : nullptr;
        const auto *param = ref ? dyn_cast<ParmVarDecl>(ref->getDecl()) : nullptr;
        if (!param) return std::nullopt;
        for (unsigned i = 0; i < f.param_size(); ++i) if (f.getParamDecl(i) == param) return i;
        return std::nullopt;
    }

    static bool containsParameter(const Expr *expr, const FunctionDecl &f, unsigned index) {
        auto direct = parameterIndex(expr, f);
        if (direct && *direct == index) return true;
        if (!expr) return false;
        for (const Stmt *child : expr->children()) {
            const auto *e = llvm::dyn_cast_or_null<Expr>(child);
            if (e && containsParameter(e, f, index)) return true;
        }
        return false;
    }

    static std::optional<unsigned> borrowedParameterIndex(const Expr *expr,
                                                          const FunctionDecl &f) {
        expr = expr ? expr->IgnoreParenCasts() : nullptr;
        if (!expr) return std::nullopt;
        if (const auto direct = parameterIndex(expr, f)) return direct;
        if (const auto *unary = dyn_cast<UnaryOperator>(expr);
            unary && unary->getOpcode() == clang::UO_AddrOf) {
            for (unsigned i = 0; i < f.param_size(); ++i) {
                if (containsParameter(unary->getSubExpr(), f, i)) return i;
            }
        }
        if (expr->getType()->isPointerType()) {
            for (unsigned i = 0; i < f.param_size(); ++i) {
                if (containsParameter(expr, f, i)) return i;
            }
        }
        return std::nullopt;
    }

    static ReturnEffect returnEffect(const CallExpr &call, const SummaryStore &store,
                                     std::optional<unsigned> &borrow) {
        if (call.getDirectCallee() && (call.getDirectCallee()->getNameAsString() == "malloc" ||
                                       call.getDirectCallee()->getNameAsString() == "calloc")) return ReturnEffect::Owned;
        const FunctionSummary *s = store.find(call.getDirectCallee());
        if (!s) return ReturnEffect::Unknown;
        borrow = s->return_borrow_arg;
        return s->return_effect;
    }

    static bool ownedInitializer(const Expr *expr, const SummaryStore &store) {
        expr = expr ? expr->IgnoreParenCasts() : nullptr;
        if (const auto *call = expr ? dyn_cast<CallExpr>(expr) : nullptr) {
            if (call->getDirectCallee() && (call->getDirectCallee()->getNameAsString() == "malloc" ||
                                            call->getDirectCallee()->getNameAsString() == "calloc")) return true;
            const auto *summary = store.find(call->getDirectCallee());
            return summary && summary->return_effect == ReturnEffect::Owned;
        }
        return false;
    }

    static bool assignedLater(const Stmt *stmt, const VarDecl *var) {
        if (!stmt) return false;
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt); binary && binary->isAssignmentOp()) {
            const Expr *lhs = binary->getLHS()->IgnoreParenCasts();
            const auto *ref = dyn_cast<DeclRefExpr>(lhs);
            if (ref && ref->getDecl() == var) return true;
        }
        for (const Stmt *child : stmt->children()) if (assignedLater(child, var)) return true;
        return false;
    }

    bool nullPointerValue(const Expr *expr) const {
        return expr && expr->isNullPointerConstant(
            context_, Expr::NPC_ValueDependentIsNotNull);
    }

    static bool ownedLocal(const Stmt *stmt, const VarDecl *var, const SummaryStore &store) {
        if (!stmt) return false;
        if (const auto *decl = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *item : decl->decls()) {
                const auto *candidate = dyn_cast<VarDecl>(item);
                if (candidate == var && candidate->getInit() && ownedInitializer(candidate->getInit(), store)) return true;
            }
        }
        for (const Stmt *child : stmt->children()) if (ownedLocal(child, var, store)) return true;
        return false;
    }

    static void combineReturn(FunctionSummary &s, ReturnEffect effect,
                              std::optional<unsigned> borrow) {
        if (effect == ReturnEffect::None) return;
        if (s.return_effect == ReturnEffect::None) { s.return_effect = effect; s.return_borrow_arg = borrow; return; }
        if (s.return_effect != effect || (effect == ReturnEffect::BorrowFromArg && s.return_borrow_arg != borrow)) {
            s.conflict = true;
        }
    }

    void scan(const Stmt *stmt, const FunctionDecl &f, FunctionSummary &s, bool conditional) {
        if (!stmt) return;
        auto markParameterFlow = [&](const Expr *expr) {
            for (unsigned i = 0; i < f.param_size(); ++i) {
                if (!f.getParamDecl(i)->getType()->isPointerType() ||
                    !containsParameter(expr, f, i) || s.params[i] != ParamEffect::None) continue;
                s.params[i] = f.getParamDecl(i)->getType()->getPointeeType()->isPointerType()
                                  ? ParamEffect::Unknown : ParamEffect::Borrow;
            }
        };
        if (const auto *ret = dyn_cast<ReturnStmt>(stmt)) {
            const Expr *value = ret->getRetValue();
            if (value && value->getType()->isPointerType()) {
                value = value->IgnoreParenCasts();
                if (nullPointerValue(value)) return;
                std::optional<unsigned> borrow;
                ReturnEffect effect = ReturnEffect::Unknown;
                if ((borrow = borrowedParameterIndex(value, f))) {
                    effect = s.params[*borrow] == ParamEffect::TakeOwnership
                                 ? ReturnEffect::Owned
                                 : ReturnEffect::BorrowFromArg;
                }
                else if (const auto *call = dyn_cast<CallExpr>(value)) {
                    effect = returnEffect(*call, old_, borrow);
                    if (effect == ReturnEffect::BorrowFromArg && borrow && *borrow < call->getNumArgs()) {
                        const auto mapped = parameterIndex(call->getArg(*borrow), f);
                        if (mapped) borrow = mapped;
                        else effect = ReturnEffect::Unknown;
                    }
                }
                else if (const auto *ref = dyn_cast<DeclRefExpr>(value)) {
                    const auto *var = dyn_cast<VarDecl>(ref->getDecl());
                    if (var && ownedLocal(f.getBody(), var, old_) && !assignedLater(f.getBody(), var)) effect = ReturnEffect::Owned;
                }
                combineReturn(s, effect, borrow);
            }
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt);
            unary && unary->getOpcode() == clang::UO_Deref) {
            for (unsigned i = 0; i < f.param_size(); ++i) {
                if (!f.getParamDecl(i)->getType()->isPointerType() ||
                    !containsParameter(unary->getSubExpr(), f, i) || s.params[i] != ParamEffect::None) continue;
                s.params[i] = f.getParamDecl(i)->getType()->getPointeeType()->isPointerType()
                                  ? ParamEffect::Unknown : ParamEffect::Borrow;
            }
        }
        if (const auto *member = dyn_cast<MemberExpr>(stmt); member && member->isArrow()) {
            for (unsigned i = 0; i < f.param_size(); ++i)
                if (f.getParamDecl(i)->getType()->isPointerType() && containsParameter(member->getBase(), f, i) && s.params[i] == ParamEffect::None) s.params[i] = ParamEffect::Borrow;
        }
        if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(stmt)) {
            for (unsigned i = 0; i < f.param_size(); ++i)
                if (f.getParamDecl(i)->getType()->isPointerType() && containsParameter(subscript->getBase(), f, i) && s.params[i] == ParamEffect::None) s.params[i] = ParamEffect::Borrow;
        }
        if (const auto *decl = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *item : decl->decls()) {
                const auto *var = dyn_cast<VarDecl>(item);
                if (var && var->getType()->isPointerType() && var->getInit()) markParameterFlow(var->getInit());
            }
        }
        if (const auto *assignment = dyn_cast<BinaryOperator>(stmt);
            assignment && assignment->isAssignmentOp() && assignment->getLHS()->getType()->isPointerType())
            markParameterFlow(assignment->getRHS());
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            const std::string name = call->getDirectCallee() ? call->getDirectCallee()->getNameAsString() : "";
            const FunctionSummary *callee = old_.find(call->getDirectCallee());
            for (unsigned argument = 0; argument < call->getNumArgs(); ++argument) {
                std::vector<unsigned> currents;
                for (unsigned i = 0; i < f.param_size(); ++i)
                    if (f.getParamDecl(i)->getType()->isPointerType() && containsParameter(call->getArg(argument), f, i)) currents.push_back(i);
                if (currents.empty()) continue;
                if (currents.size() > 1) {
                    for (unsigned current : currents) s.params[current] = ParamEffect::Unknown;
                    continue;
                }
                const unsigned current = currents.front();
                ParamEffect effect = ParamEffect::Unknown;
                if (name == "free" && call->getNumArgs() == 1) effect = ParamEffect::Destroy;
                else if (callee && argument < callee->params.size()) effect = callee->params[argument];
                if (conditional) effect = ParamEffect::Unknown;
                if (s.params[current] == ParamEffect::TakeOwnership &&
                    (effect == ParamEffect::TakeOwnership || effect == ParamEffect::Destroy)) {
                    // A consuming parameter may be destroyed by the callee;
                    // that is still one ownership-transfer summary, not a
                    // contradictory second effect.
                } else if (s.params[current] == ParamEffect::None || s.params[current] == ParamEffect::Borrow) s.params[current] = effect;
                else if (effect != s.params[current]) s.params[current] = ParamEffect::Unknown;
            }
            if (name == "realloc") s.conflict = true;
        }
        const bool nested = conditional || isa<IfStmt>(stmt) || isa<SwitchStmt>(stmt) ||
                            isa<WhileStmt>(stmt) || isa<ForStmt>(stmt) || isa<DoStmt>(stmt) ||
                            isa<ConditionalOperator>(stmt);
        for (const Stmt *child : stmt->children()) scan(child, f, s, nested);
    }
    const SummaryStore &old_;
    SummaryStore &out_;
    ASTContext &context_;
};

class TranslationUnitVisitor : public RecursiveASTVisitor<TranslationUnitVisitor> {
public:
    TranslationUnitVisitor(ASTContext &context, Collector &collector)
        : context_(context), collector_(collector) {}

    void prepare() {
        const SourceManager &source_manager = context_.getSourceManager();
        for (auto it = source_manager.fileinfo_begin(); it != source_manager.fileinfo_end(); ++it)
            collector_.noteDependency(it->first.getName().str());
        std::vector<const FunctionDecl *> functions;
        for (const clang::Decl *decl : context_.getTranslationUnitDecl()->decls()) {
            if (const auto *function = dyn_cast<FunctionDecl>(decl); function && function->hasBody())
                functions.push_back(function);
        }
        for (const FunctionDecl *function : functions) {
            FunctionSummary empty;
            empty.function = function;
            empty.origin = SummaryOrigin::Unknown;
            empty.return_effect = ReturnEffect::Unknown;
            empty.params.assign(function->param_size(), ParamEffect::Unknown);
            summaries_.add(function, std::move(empty));
        }
        for (unsigned round = 0; round < functions.size() + 1; ++round) {
            SummaryStore next;
            for (const FunctionDecl *function : functions)
                SummaryBuilder(summaries_, next, context_).build(*function);
            summaries_ = std::move(next);
        }
        loadContracts();
        // Contracts can seed bodies that wrap external APIs. Re-run the same
        // bounded summary fixed point with trusted external facts available;
        // preserve explicit body/contract conflicts instead of overwriting them.
        for (unsigned round = 0; round < functions.size() + 1; ++round) {
            SummaryStore next = summaries_;
            for (const FunctionDecl *function : functions) {
                const FunctionSummary *existing = summaries_.find(function);
                if (existing && existing->conflict) continue;
                SummaryBuilder(summaries_, next, context_).build(*function);
            }
            summaries_ = std::move(next);
        }
    }

    void loadContracts() {
        if (ContractFile.empty()) return;
        std::ifstream input(ContractFile);
        if (!input) { collector_.noteContractError(); return; }
        std::string line, symbol;
        std::set<std::string> seen_symbols;
        std::set<unsigned> seen_param_indices, seen_param_effects;
        std::optional<unsigned> last_index;
        std::optional<ReturnEffect> return_seen;
        bool borrow_index_seen = false;
        FunctionSummary summary;
        bool in_symbol = false;
        bool schema_seen = false, name_seen = false, version_seen = false;
        bool symbols_seen = false, kind_seen = false;
        bool in_platform = false, in_notes_block = false;
        const auto parseUnsigned = [](const std::string &text, unsigned &value) {
            std::size_t end = 0;
            try {
                const unsigned long parsed = std::stoul(text, &end);
                if (end != text.size() || parsed > std::numeric_limits<unsigned>::max()) return false;
                value = static_cast<unsigned>(parsed);
                return true;
            } catch (...) { return false; }
        };
        auto finish = [&]() {
            if (!in_symbol || symbol.empty()) return true;
            if (!kind_seen) return false;
            if (seen_param_indices.size() != seen_param_effects.size()) return false;
            if (summary.return_effect == ReturnEffect::BorrowFromArg && !summary.return_borrow_arg) return false;
            if (summary.return_effect != ReturnEffect::BorrowFromArg && summary.return_borrow_arg) return false;
            const FunctionDecl *decl = nullptr;
            for (const clang::Decl *item : context_.getTranslationUnitDecl()->decls()) {
                const auto *candidate = dyn_cast<FunctionDecl>(item);
                if (candidate && candidate->getNameAsString() == symbol) { decl = candidate; break; }
            }
            if (decl && (summary.params.size() > decl->param_size() ||
                         (summary.return_borrow_arg && *summary.return_borrow_arg >= decl->param_size()))) return false;
            if (decl) summary.params.resize(decl->param_size(), ParamEffect::None);
            summary.origin = SummaryOrigin::ExternalTrusted;
            // realloc's success/failure and old-object lifetime are conditional
            // and cannot be represented by P0.4's simple effects.
            if (symbol == "realloc") {
                summary.return_effect = ReturnEffect::Unknown;
                summary.return_borrow_arg.reset();
                std::fill(summary.params.begin(), summary.params.end(), ParamEffect::Unknown);
            }
            const FunctionSummary *body = summaries_.find(symbol);
            if (body && body->origin == SummaryOrigin::BodyVerified &&
                (body->return_effect != summary.return_effect || body->return_borrow_arg != summary.return_borrow_arg || body->params != summary.params)) {
                FunctionSummary conflict = *body; conflict.conflict = true; summaries_.set(symbol, std::move(conflict));
            } else if (!body || body->origin == SummaryOrigin::Unknown) summaries_.set(symbol, summary);
            return true;
        };
        while (std::getline(input, line)) {
            const auto trim = [](std::string s) { const auto a = s.find_first_not_of(" \t"); const auto b = s.find_last_not_of(" \t\r"); return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1); };
            const std::size_t indent = line.find_first_not_of(" \t");
            if (indent == std::string::npos) continue;
            if (line.substr(0, indent).find('\t') != std::string::npos) { collector_.noteContractError(); return; }
            std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            if (in_notes_block && indent >= 6) continue;
            in_notes_block = false;
            if (!in_symbol) {
                if (t.rfind("schema:", 0) == 0) {
                    if (indent != 0 || schema_seen || t != "schema: cand.api-contract/v1") { collector_.noteContractError(); return; }
                    schema_seen = true;
                    continue;
                }
                if (t.rfind("name:", 0) == 0) {
                    const std::string value = trim(t.substr(5));
                    if (indent != 0 || name_seen || value.empty()) { collector_.noteContractError(); return; }
                    name_seen = true;
                    continue;
                }
                if (t.rfind("version:", 0) == 0) {
                    const std::string value = trim(t.substr(8));
                    if (indent != 0 || version_seen || value.empty()) { collector_.noteContractError(); return; }
                    version_seen = true;
                    continue;
                }
                if (t == "symbols:") {
                    if (indent != 0 || symbols_seen) { collector_.noteContractError(); return; }
                    symbols_seen = true;
                    in_platform = false;
                    continue;
                }
                if (t.rfind("- symbol:", 0) == 0) {
                    if (indent != 2 || !symbols_seen || !schema_seen || !name_seen || !version_seen) { collector_.noteContractError(); return; }
                    symbol = trim(t.substr(t.find(':') + 1));
                    in_symbol = !symbol.empty();
                    summary = FunctionSummary{};
                    last_index.reset(); return_seen.reset(); kind_seen = false;
                    seen_param_indices.clear(); seen_param_effects.clear(); borrow_index_seen = false;
                    if (!in_symbol || !seen_symbols.insert(symbol).second) { collector_.noteContractError(); return; }
                    continue;
                }
                if (t == "platform:") { if (indent != 0) { collector_.noteContractError(); return; } in_platform = true; continue; }
                if (in_platform && (t.rfind("os:", 0) == 0 || t.rfind("libc:", 0) == 0)) {
                    const std::string value = trim(t.substr(t.find(':') + 1));
                    if (indent != 2 || value.size() < 2 || value.front() != '[' || value.back() != ']') { collector_.noteContractError(); return; }
                    continue;
                }
                if (t.rfind("provenance:", 0) == 0) {
                    const std::string value = trim(t.substr(11));
                    if (indent != 0 || value != "{}") { collector_.noteContractError(); return; }
                    continue;
                }
                collector_.noteContractError(); return;
            }
            if (t.rfind("- symbol:", 0) == 0) {
                if (indent != 2) { collector_.noteContractError(); return; }
                if (in_symbol && !finish()) { collector_.noteContractError(); return; }
                symbol = trim(t.substr(t.find(':') + 1)); in_symbol = !symbol.empty(); summary = FunctionSummary{}; last_index.reset(); return_seen.reset();
                seen_param_indices.clear(); seen_param_effects.clear(); borrow_index_seen = false;
                kind_seen = false;
                if (!in_symbol) { collector_.noteContractError(); return; }
                if (!seen_symbols.insert(symbol).second) { collector_.noteContractError(); return; }
                continue;
            }
            if (t.rfind("ownership:", 0) == 0) {
                if (indent != 6) { collector_.noteContractError(); return; }
                std::string v = trim(t.substr(10));
                ReturnEffect effect;
                if (v == "owned") effect = ReturnEffect::Owned;
                else if (v == "borrowed") effect = ReturnEffect::BorrowFromArg;
                else if (v == "none") effect = ReturnEffect::None;
                else if (v == "unknown") effect = ReturnEffect::Unknown;
                else { collector_.noteContractError(); return; }
                if (return_seen) { collector_.noteContractError(); return; }
                return_seen = effect; summary.return_effect = effect;
            } else if (t.rfind("from_param:", 0) == 0) {
                if (indent != 8) { collector_.noteContractError(); return; }
                unsigned index;
                if (borrow_index_seen || !parseUnsigned(trim(t.substr(11)), index)) { collector_.noteContractError(); return; }
                borrow_index_seen = true;
                summary.return_borrow_arg = index;
            } else if (t.rfind("index:", 0) == 0 || t.rfind("- index:", 0) == 0) {
                if (indent != 6) { collector_.noteContractError(); return; }
                unsigned index;
                const std::size_t colon = t.find(':');
                if (!parseUnsigned(trim(t.substr(colon + 1)), index)) { collector_.noteContractError(); return; }
                if (!seen_param_indices.insert(index).second) { collector_.noteContractError(); return; }
                if (summary.params.size() <= index) summary.params.resize(index + 1, ParamEffect::None);
                last_index = index;
                summary.params[index] = ParamEffect::Unknown;
            } else if (t.rfind("effect:", 0) == 0) {
                if (indent != 8) { collector_.noteContractError(); return; }
                if (!last_index || *last_index >= summary.params.size() ||
                    !seen_param_effects.insert(*last_index).second) { collector_.noteContractError(); return; }
                std::string v = trim(t.substr(7));
                ParamEffect effect;
                if (v == "borrow" || v == "borrow_shared") effect = ParamEffect::Borrow;
                else if (v == "consumes") effect = ParamEffect::TakeOwnership;
                else if (v == "destroys") effect = ParamEffect::Destroy;
                else if (v == "no_ownership_effect") effect = ParamEffect::None;
                else if (v == "unknown") effect = ParamEffect::Unknown;
                else { collector_.noteContractError(); return; }
                summary.params[*last_index] = effect;
            } else if (t == "kind: function") {
                if (indent != 4) { collector_.noteContractError(); return; }
                if (kind_seen) { collector_.noteContractError(); return; }
                kind_seen = true;
            } else if (t == "returns:" || t == "params:") {
                if (indent != 4) { collector_.noteContractError(); return; }
            } else if (t == "lifetime:") {
                if (indent != 6) { collector_.noteContractError(); return; }
            } else if (t.rfind("borrow_kind:", 0) == 0) {
                if (indent != 6 || trim(t.substr(12)) != "shared") {
                    collector_.noteContractError();
                    return;
                }
            } else if (t == "conditional_effects:" || t == "callbacks:") {
                if (indent != 4) { collector_.noteContractError(); return; }
            } else if (t.rfind("notes:", 0) == 0) {
                if (indent != 4) { collector_.noteContractError(); return; }
                const std::string value = trim(t.substr(6));
                in_notes_block = value == ">-" || value == ">" || value == "|" || value == "|-" || value == "|+";
            } else if (t.rfind("allocation_family:", 0) == 0) {
                if (indent != 6 && indent != 8) { collector_.noteContractError(); return; }
            } else if (t.rfind("nullable:", 0) == 0) {
                if (indent != 6) { collector_.noteContractError(); return; }
                continue;
            } else if (t.find(':') != std::string::npos) {
                collector_.noteContractError(); return;
            } else {
                collector_.noteContractError(); return;
            }
        }
        if (!schema_seen || !name_seen || !version_seen || !symbols_seen || !finish())
            collector_.noteContractError();
    }

    bool VisitFunctionDecl(FunctionDecl *function) {
        if (function == nullptr || !function->hasBody() ||
            !function->isThisDeclarationADefinition()) {
            return true;
        }
        SourceLocation loc =
            context_.getSourceManager().getExpansionLoc(function->getLocation());
        if (!context_.getSourceManager().isWrittenInMainFile(loc)) {
            return true;
        }
        FlowAnalyzer analyzer(context_, collector_, summaries_);
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
        FlowAnalyzer analyzer(context_, collector_, summaries_);
        analyzer.analyzeGlobal(*var);
        return true;
    }

private:
    ASTContext &context_;
    Collector &collector_;
    SummaryStore summaries_;
};

class CandConsumer : public ASTConsumer {
public:
    CandConsumer(ASTContext &context, Collector &collector)
        : visitor_(context, collector), collector_(collector) {}

    void HandleTranslationUnit(ASTContext &context) override {
        if (context.getDiagnostics().hasErrorOccurred()) {
            collector_.noteFrontendError();
        }
        visitor_.prepare();
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

struct AgentPolicyState {
    cand::Policy policy;
    cand::PolicyDiff delta;
    bool policy_failed = false;
    bool review_required = false;
    std::vector<std::string> policy_errors;
    std::vector<std::tuple<std::string, std::string, std::string>> contract_inputs;
    unsigned unsafe_boundaries = 0;
    unsigned suppressions = 0;
};

std::string readFile(const std::string &path, std::string &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "cannot read file: " + path; return {}; }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) { error = "failed reading file: " + path; return {}; }
    return contents.str();
}

std::string gitHead() {
    FILE *pipe = popen("git rev-parse HEAD 2>/dev/null", "r");
    if (!pipe) return {};
    std::array<char, 256> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
    const int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    return output;
}

std::string gitOriginMain() {
    FILE *pipe = popen("git rev-parse --verify 'origin/main^{commit}' 2>/dev/null", "r");
    if (!pipe) return {};
    std::array<char, 256> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
    const int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    if ((output.size() != 40 && output.size() != 64) ||
        !std::all_of(output.begin(), output.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))
        return {};
    return output;
}

unsigned countToken(const std::string &text, llvm::StringRef token) {
    unsigned count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(token.str(), offset)) != std::string::npos) {
        ++count;
        offset += token.size();
    }
    return count;
}

bool collectSourceInputs(clang::tooling::CommonOptionsParser &parser,
                         const cand::Policy &policy,
                         llvm::json::Array &sources,
                         llvm::json::Array &frontend_args,
                         AgentPolicyState &state) {
    std::vector<std::string> input_paths;
    std::vector<std::string> contents;
    for (const std::string &source : parser.getSourcePathList()) {
        std::string normalized = cand::normalizedRelativePath(source);
        if (normalized.empty()) {
            state.policy_failed = true;
            state.policy_errors.push_back("source path must be relative and remain inside the workspace: " + source);
            continue;
        }
        std::string error;
        const std::string bytes = readFile(source, error);
        if (!error.empty()) {
            state.policy_failed = true;
            state.policy_errors.push_back(error);
            continue;
        }
        std::string digest = cand::sha256(bytes);
        llvm::json::Object file;
        file["path"] = normalized;
        file["sha256"] = digest;
        sources.push_back(std::move(file));
        input_paths.push_back(normalized);
        contents.push_back(bytes);
        state.unsafe_boundaries += countToken(bytes, "CAND_UNSAFE");
        state.suppressions += countToken(bytes, "CAND_SUPPRESS") +
                              countToken(bytes, "CAND_BASELINE") +
                              countToken(bytes, "cand: ignore");
    }
    std::sort(input_paths.begin(), input_paths.end());
    if (std::adjacent_find(input_paths.begin(), input_paths.end()) != input_paths.end()) {
        state.policy_failed = true;
        state.policy_errors.push_back("duplicate checked source path");
    }
    if (input_paths != policy.scope_files) {
        state.policy_failed = true;
        state.policy_errors.push_back("checked source inputs do not exactly match policy scope.files");
        llvm::json::Array before, after;
        for (const auto &path : policy.scope_files) before.push_back(path);
        for (const auto &path : input_paths) after.push_back(path);
        cand::PolicyChange change{"checked-scope-change", "policy scope", "invocation scope", "PROOF_WEAKENING"};
        state.delta.changes.push_back(std::move(change));
        state.delta.weakened = true;
        state.delta.classification = "PROOF_WEAKENING";
    }
    for (const auto &source : parser.getSourcePathList()) {
        const auto commands = parser.getCompilations().getCompileCommands(source);
        for (const auto &command : commands) {
            bool skip_output = false;
            for (std::size_t i = 1; i < command.CommandLine.size(); ++i) {
                const std::string &arg = command.CommandLine[i];
                if (skip_output) { skip_output = false; continue; }
                if (arg == "-o") { skip_output = true; continue; }
                if (arg == command.Filename || arg == source || arg == "-c") continue;
                frontend_args.push_back(arg);
            }
        }
    }
    return !state.policy_failed;
}

void addIncludedFiles(llvm::json::Array &sources, const Collector &collector,
                      AgentPolicyState &state) {
    std::map<std::string, std::string> hashes;
    for (const auto &entry : sources) {
        const auto *object = entry.getAsObject();
        if (!object) continue;
        auto path = object->getString("path");
        auto digest = object->getString("sha256");
        if (path && digest) hashes[path->str()] = digest->str();
    }
    const auto cwd = std::filesystem::current_path();
    for (const std::string &input : collector.dependencies()) {
        if (input.empty() || input.front() == '<') continue;
        std::filesystem::path path(input);
        std::string logical;
        if (path.is_absolute()) {
            const auto relative = path.lexically_relative(cwd);
            if (relative.empty() || *relative.begin() == "..") continue;
            logical = relative.generic_string();
        } else {
            logical = cand::normalizedRelativePath(input);
        }
        if (logical.empty() || hashes.count(logical)) continue;
        std::string content_error;
        const std::string content = readFile(input, content_error);
        if (!content_error.empty()) {
            state.policy_failed = true;
            state.policy_errors.push_back("cannot inspect frontend dependency " + logical);
            continue;
        }
        // The public annotation header defines the marker macros; counting
        // those definitions as candidate unsafe operations would make every
        // normal annotated translation unit fail its generated-code policy.
        // The header remains content-bound and verifier-surface protected.
        if (logical != "include/cand/cand.h") {
            state.unsafe_boundaries += countToken(content, "CAND_UNSAFE");
            state.suppressions += countToken(content, "CAND_SUPPRESS") +
                                  countToken(content, "CAND_BASELINE") +
                                  countToken(content, "cand: ignore");
        }
        std::string digest, error;
        if (!cand::sha256File(input, digest, error)) {
            state.policy_failed = true;
            state.policy_errors.push_back("cannot bind frontend dependency " + logical + ": " + error);
            continue;
        }
        hashes[logical] = std::move(digest);
    }
    sources.clear();
    for (const auto &entry : hashes) {
        llvm::json::Object file;
        file["path"] = entry.first;
        file["sha256"] = entry.second;
        sources.push_back(std::move(file));
    }
}

llvm::json::Object policyDeltaJson(const AgentPolicyState &state) {
    llvm::json::Object delta;
    delta["classification"] = state.delta.classification;
    delta["weakened"] = state.delta.weakened || state.policy_failed;
    delta["review_required"] = state.delta.review_required || state.review_required;
    llvm::json::Array changes;
    for (const auto &change : state.delta.changes) {
        llvm::json::Object item;
        item["kind"] = change.kind;
        item["before"] = change.before;
        item["after"] = change.after;
        item["classification"] = change.classification;
        changes.push_back(std::move(item));
    }
    for (const auto &error : state.policy_errors) {
        llvm::json::Object item;
        item["kind"] = "policy-violation";
        item["detail"] = error;
        item["classification"] = "PROOF_WEAKENING";
        changes.push_back(std::move(item));
    }
    delta["changes"] = std::move(changes);
    return delta;
}

std::string serializeJson(llvm::json::Object object) {
    return llvm::formatv("{0:2}", llvm::json::Value(std::move(object))).str();
}

llvm::json::Value canonicalizeJson(const llvm::json::Value &value) {
    if (const auto *object = value.getAsObject()) {
        std::map<std::string, const llvm::json::Value *> fields;
        for (const auto &entry : *object) fields.emplace(entry.first.str(), &entry.second);
        llvm::json::Object canonical;
        for (const auto &entry : fields) canonical[entry.first] = canonicalizeJson(*entry.second);
        return canonical;
    }
    if (const auto *array = value.getAsArray()) {
        llvm::json::Array canonical;
        for (const auto &entry : *array) canonical.push_back(canonicalizeJson(entry));
        return canonical;
    }
    if (auto string = value.getAsString()) return string->str();
    if (auto boolean = value.getAsBoolean()) return *boolean;
    if (auto integer = value.getAsInteger()) return *integer;
    if (auto number = value.getAsNumber()) return *number;
    return nullptr;
}

std::string canonicalJson(const llvm::json::Value &value) {
    return llvm::formatv("{0:2}", canonicalizeJson(value)).str();
}

std::string quoteShellArgument(const std::string &value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    return quoted + "'";
}

llvm::json::Object buildEvidence(const Collector &collector,
                                 const AgentPolicyState &state,
                                 llvm::json::Array sources,
                                 llvm::json::Array frontend_args,
                                 llvm::StringRef semantic_result,
                                 llvm::StringRef final_result) {
    llvm::json::Object evidence;
    evidence["schema"] = "cand.evidence/v1";
    evidence["result"] = final_result.str();
    llvm::json::Object cand_info;
    cand_info["version"] = "0.1.0-dev";
    cand_info["build_identity"] = llvm::formatv("cand-0.1.0-dev/llvm-{0}/clang-{1}", LLVM_VERSION_STRING, CLANG_VERSION_STRING).str();
    cand_info["verifier_source_commit"] = CAND_VERIFIER_SOURCE_COMMIT;
    cand_info["binary_sha256"] = CandExecutableSha256;
    evidence["cand"] = std::move(cand_info);
    llvm::json::Object source;
    source["commit"] = gitHead();
    source["files"] = std::move(sources);
    evidence["source"] = std::move(source);
    llvm::json::Object frontend;
    frontend["compiler"] = "clang";
    frontend["version"] = CLANG_VERSION_STRING;
    frontend["llvm_version"] = LLVM_VERSION_STRING;
    frontend["arguments"] = std::move(frontend_args);
    evidence["frontend"] = std::move(frontend);
    llvm::json::Object verification;
    verification["profile"] = state.policy.profile;
    verification["safety_level"] = state.policy.safety_level;
    verification["policy_path"] = PolicyPath.getValue();
    verification["base_ref"] = BaseRef.getValue();
    verification["trusted_base_sha"] = TrustedBaseCommit;
    verification["effective_policy_sha256"] = state.policy.sha256;
    verification["checked_scope"] = state.policy.scope_files;
    verification["policy_revision"] = gitHead();
    verification["ownership_rule_set"] = "p1-unique-ownership-v1";
    verification["borrow_rule_set"] = "p2-borrow-lifetime-v1";
    evidence["verification"] = std::move(verification);
    const llvm::json::Object analysis = collector.jsonObject();
    llvm::json::Object coverage;
    if (const auto *counts = analysis.getObject("coverage")) {
        if (auto value = counts->getInteger("functions_analyzed")) coverage["functions_analyzed"] = *value;
        if (auto value = counts->getInteger("tracked_heap_objects")) coverage["tracked_heap_objects"] = *value;
        if (auto value = counts->getInteger("unsupported_ownership_operations")) coverage["unsupported_ownership_operations"] = *value;
        if (auto value = counts->getInteger("ownership_transitions")) coverage["ownership_transitions"] = *value;
        if (auto value = counts->getInteger("unsupported_ownership_transfers")) coverage["unsupported_ownership_transfers"] = *value;
    }
    coverage["ownership_rule_set"] = "p1-unique-ownership-v1";
    if (const auto *borrows = analysis.getObject("borrow_analysis")) {
        llvm::json::Object borrow_copy;
        for (const auto &entry : *borrows) borrow_copy[entry.first] = entry.second;
        coverage["borrow_analysis"] = std::move(borrow_copy);
    }
    coverage["unsafe_boundaries"] = static_cast<std::int64_t>(state.unsafe_boundaries);
    coverage["suppressions"] = static_cast<std::int64_t>(state.suppressions);
    evidence["analysis"] = std::move(coverage);
    llvm::json::Array contracts;
    for (const auto &entry : state.contract_inputs) {
        llvm::json::Object contract;
        contract["path"] = std::get<0>(entry);
        contract["sha256"] = std::get<1>(entry);
        contract["trust_class"] = std::get<2>(entry);
        contracts.push_back(std::move(contract));
    }
    evidence["contracts"] = std::move(contracts);
    llvm::json::Object policy_delta;
    policy_delta["weakened"] = state.delta.weakened || state.policy_failed;
    policy_delta["review_required"] = state.delta.review_required || state.review_required;
    policy_delta["changes"] = llvm::json::Array();
    for (const auto &change : state.delta.changes) {
        llvm::json::Object item;
        item["kind"] = change.kind;
        item["before"] = change.before;
        item["after"] = change.after;
        item["classification"] = change.classification;
        policy_delta.getArray("changes")->push_back(std::move(item));
    }
    for (const auto &error : state.policy_errors) {
        llvm::json::Object item;
        item["kind"] = "policy-violation";
        item["detail"] = error;
        item["classification"] = "PROOF_WEAKENING";
        policy_delta.getArray("changes")->push_back(std::move(item));
    }
    evidence["proof_policy_delta"] = std::move(policy_delta);
    evidence["semantic_result"] = semantic_result.str();
    evidence["policy_result"] = state.policy_failed || state.delta.weakened ? "fail" :
        (state.review_required || state.delta.review_required ? "review_required" : "pass");
    const std::string payload = canonicalJson(llvm::json::Value(std::move(evidence)));
    auto reparsed = llvm::json::parse(payload);
    evidence = std::move(*reparsed->getAsObject());
    evidence["integrity_sha256"] = cand::sha256(payload);
    return evidence;
}

void printAgentJson(const Collector &collector, const AgentPolicyState &state,
                    llvm::json::Object evidence,
                    llvm::StringRef semantic_result,
                    llvm::StringRef final_result,
                    llvm::StringRef evidence_path) {
    llvm::json::Object root;
    root["schema"] = "cand.agent-check/v1";
    root["result"] = final_result.str();
    root["semantic_result"] = semantic_result.str();
    root["policy_result"] = state.policy_failed || state.delta.weakened ? "fail" :
        (state.review_required || state.delta.review_required ? "review_required" : "pass");
    root["profile"] = state.policy.profile;
    root["safety_level"] = state.policy.safety_level;
    root["policy_delta"] = policyDeltaJson(state);
    root["analysis"] = collector.jsonObject();
    root["effective_policy"] = cand::policyJson(state.policy);
    root["evidence"] = std::move(evidence);
    if (!evidence_path.empty()) root["evidence_path"] = evidence_path.str();
    llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
}

bool verifyEvidenceFile(const std::string &path, std::string &status,
                        std::string &detail) {
    std::string error;
    const std::string text = readFile(path, error);
    if (!error.empty()) { status = "error"; detail = error; return false; }
    auto parsed = llvm::json::parse(text);
    if (!parsed || !parsed->getAsObject()) { status = "tampered"; detail = "invalid evidence JSON"; return false; }
    llvm::json::Object evidence = std::move(*parsed->getAsObject());
    auto integrity = evidence.getString("integrity_sha256");
    if (!integrity) { status = "tampered"; detail = "missing integrity digest"; return false; }
    const std::string expected_integrity = integrity->str();
    evidence.erase("integrity_sha256");
    const std::string canonical_payload = canonicalJson(llvm::json::Value(std::move(evidence)));
    if (cand::sha256(canonical_payload) != expected_integrity) {
        status = "tampered"; detail = "evidence payload digest mismatch"; return false;
    }
    auto restored = llvm::json::parse(canonical_payload);
    if (!restored || !restored->getAsObject()) { status = "tampered"; detail = "invalid canonical payload"; return false; }
    evidence = std::move(*restored->getAsObject());
    const auto *cand_info = evidence.getObject("cand");
    const auto *source = evidence.getObject("source");
    auto verifier_source = cand_info ? cand_info->getString("verifier_source_commit") : std::nullopt;
    auto source_commit = source ? source->getString("commit") : std::nullopt;
    if (!verifier_source || verifier_source->str() != CAND_VERIFIER_SOURCE_COMMIT ||
        !source_commit || source_commit->str() != gitHead()) {
        status = "stale"; detail = "verifier/source commit provenance differs from evidence"; return false;
    }
    const auto result = evidence.getString("result");
    const auto semantic_result = evidence.getString("semantic_result");
    const auto policy_result = evidence.getString("policy_result");
    const auto *policy_delta = evidence.getObject("proof_policy_delta");
    const auto weakened = policy_delta ? policy_delta->getBoolean("weakened") : std::nullopt;
    if (!result || !semantic_result || !policy_result || !weakened ||
        (result->str() == "pass" &&
         (semantic_result->str() != "pass" || policy_result->str() != "pass" || *weakened))) {
        status = "tampered"; detail = "invalid semantic/policy result combination"; return false;
    }
    auto binary_digest = cand_info ? cand_info->getString("binary_sha256") : std::nullopt;
    if (!binary_digest || binary_digest->str() != CandExecutableSha256) {
        status = "stale"; detail = "C& verifier binary differs from evidence"; return false;
    }
    const auto *files = source ? source->getArray("files") : nullptr;
    if (!files) { status = "tampered"; detail = "missing source file manifest"; return false; }
    for (const auto &item : *files) {
        const auto *file = item.getAsObject();
        auto file_path = file ? file->getString("path") : std::nullopt;
        auto expected = file ? file->getString("sha256") : std::nullopt;
        const std::filesystem::path path(file_path ? file_path->str() : "");
        if (!file_path || path.is_absolute() || path.lexically_normal() != path ||
            std::find(path.begin(), path.end(), "..") != path.end()) {
            status = "tampered"; detail = "source manifest contains a non-relative path"; return false;
        }
        std::string actual, hash_error;
        if (!file_path || !expected || !cand::sha256File(file_path->str(), actual, hash_error) || actual != expected->str()) {
            status = "stale"; detail = "source content changed or unavailable"; return false;
        }
    }
    const auto *verification = evidence.getObject("verification");
    auto relative_manifest_path = [](llvm::StringRef value) {
        const std::filesystem::path path(value.str());
        return !path.empty() && !path.is_absolute() && path.lexically_normal() == path &&
               std::find(path.begin(), path.end(), "..") == path.end();
    };
    auto expected_base = verification ? verification->getString("trusted_base_sha") : std::nullopt;
    const char *trusted_base_env = std::getenv("CAND_TRUSTED_BASE_SHA");
    if (!expected_base || !trusted_base_env || expected_base->str() != trusted_base_env ||
        gitOriginMain() != expected_base->str()) {
        status = "stale"; detail = "trusted base commit differs from evidence"; return false;
    }
    auto policy_path = verification ? verification->getString("policy_path") : std::nullopt;
    auto policy_digest = verification ? verification->getString("effective_policy_sha256") : std::nullopt;
    std::string actual_policy;
    if (!policy_path || !relative_manifest_path(*policy_path) || !policy_digest ||
        !cand::sha256File(policy_path->str(), actual_policy, error) || actual_policy != policy_digest->str()) {
        status = "stale"; detail = "effective policy changed or unavailable"; return false;
    }
    const auto *contracts = evidence.getArray("contracts");
    if (contracts) for (const auto &item : *contracts) {
        const auto *contract = item.getAsObject();
        auto contract_path = contract ? contract->getString("path") : std::nullopt;
        auto expected = contract ? contract->getString("sha256") : std::nullopt;
        std::string actual;
        if (!contract_path || !relative_manifest_path(*contract_path) || !expected ||
            !cand::sha256File(contract_path->str(), actual, error) || actual != expected->str()) {
            status = "stale"; detail = "trusted contract changed or unavailable"; return false;
        }
    }
    const auto *frontend = evidence.getObject("frontend");
    const auto *arguments = frontend ? frontend->getArray("arguments") : nullptr;
    const auto *scope = verification ? verification->getArray("checked_scope") : nullptr;
    if (!arguments || !scope || scope->empty() || CandExecutablePath.empty()) {
        status = "tampered"; detail = "evidence cannot be replayed"; return false;
    }
    std::vector<std::string> source_paths;
    for (const auto &item : *scope) {
        auto path = item.getAsString();
        if (!path || cand::normalizedRelativePath(path->str()) != path->str()) {
            status = "tampered"; detail = "invalid checked-scope path"; return false;
        }
        source_paths.push_back(path->str());
    }
    std::string contract_path;
    if (contracts) {
        if (contracts->size() > 1) {
            status = "tampered"; detail = "unsupported contract bundle count"; return false;
        }
        if (!contracts->empty()) {
            const auto *contract = contracts->front().getAsObject();
            auto path = contract ? contract->getString("path") : std::nullopt;
            if (!path) { status = "tampered"; detail = "invalid contract identity"; return false; }
            contract_path = path->str();
        }
    }
    char replay_path[] = "/tmp/cand-evidence-XXXXXX";
    const int replay_fd = mkstemp(replay_path);
    if (replay_fd < 0) { status = "error"; detail = "cannot create evidence replay file"; return false; }
    close(replay_fd);
    std::string command = quoteShellArgument(CandExecutablePath) +
        " check --agent --base origin/main --policy " + quoteShellArgument(policy_path->str()) +
        " --emit-evidence " + quoteShellArgument(replay_path);
    if (!contract_path.empty()) command += " --contracts " + quoteShellArgument(contract_path);
    for (const auto &path : source_paths) command += " " + quoteShellArgument(path);
    command += " --";
    for (const auto &item : *arguments) {
        auto argument = item.getAsString();
        if (!argument) {
            std::filesystem::remove(replay_path);
            status = "tampered"; detail = "invalid frontend argument"; return false;
        }
        command += " " + quoteShellArgument(argument->str());
    }
    command += " >/dev/null 2>&1";
    const int replay_status = std::system(command.c_str());
    std::string replay_error;
    const std::string replay_text = readFile(replay_path, replay_error);
    std::filesystem::remove(replay_path);
    if (replay_status == -1 || !replay_error.empty()) {
        status = "stale"; detail = "evidence replay could not reproduce a verifier result"; return false;
    }
    auto replayed = llvm::json::parse(replay_text);
    if (!replayed || !replayed->getAsObject()) {
        status = "stale"; detail = "evidence replay failed"; return false;
    }
    replayed->getAsObject()->erase("integrity_sha256");
    if (canonicalJson(*replayed) != canonical_payload) {
        status = "stale"; detail = "evidence does not match replayed verification"; return false;
    }
    status = "valid"; detail = "all bound inputs match"; return true;
}

void printPolicyAddedDiff(llvm::StringRef path) {
    llvm::json::Object root;
    root["schema"] = "cand.policy-diff/v1";
    root["classification"] = "REVIEW_REQUIRED";
    root["result"] = "review_required";
    llvm::json::Array changes;
    llvm::json::Object change;
    change["kind"] = "initial-policy";
    change["before"] = "absent";
    change["after"] = path.str();
    change["classification"] = "REVIEW_REQUIRED";
    changes.push_back(std::move(change));
    root["changes"] = std::move(changes);
    llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
}

int runPolicyDiff(int argc, const char **argv) {
    std::string base_ref, base_file, head_file = "cand-policy.json";
    bool json = false;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--base" || arg == "--base-policy" || arg == "--head-policy" || arg == "--format") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--base") base_ref = value;
            else if (arg == "--base-policy") base_file = value;
            else if (arg == "--head-policy") head_file = value;
            else json = value == "json";
        } else {
            llvm::errs() << "cand policy diff: unknown or incomplete argument: " << arg << '\n';
            return 2;
        }
    }
    if (!json || (base_ref.empty() == base_file.empty())) {
        llvm::errs() << "Usage: cand policy diff (--base <git-ref>|--base-policy <file>) [--head-policy <file>] --format json\n";
        return 2;
    }
    cand::Policy before, after;
    std::string error;
    if (!cand::loadPolicy(head_file, after, error)) {
        llvm::errs() << "cand policy diff: " << error << '\n';
        return 2;
    }
    const bool base_ok = base_file.empty()
        ? cand::loadPolicyAtRef(base_ref, "cand-policy.json", before, error)
        : cand::loadPolicy(base_file, before, error);
    if (!base_ok && error.find("base policy not found") == 0) {
        printPolicyAddedDiff("cand-policy.json");
        return 4;
    }
    if (!base_ok) {
        llvm::errs() << "cand policy diff: " << error << '\n';
        return 2;
    }
    const cand::PolicyDiff diff = cand::comparePolicies(before, after);
    llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(diff.toJson()));
    return diff.weakened || diff.review_required ? 4 : 0;
}

int runEvidenceVerify(int argc, const char **argv) {
    if (argc != 4 || llvm::StringRef(argv[2]) != "verify") {
        llvm::errs() << "Usage: cand evidence verify <evidence.json>\n";
        return 2;
    }
    std::string status, detail;
    const bool valid = verifyEvidenceFile(argv[3], status, detail);
    llvm::json::Object result;
    result["schema"] = "cand.evidence-verify/v1";
    result["result"] = status;
    result["detail"] = detail;
    llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(result)));
    return valid ? 0 : (status == "stale" ? 1 : 2);
}

bool loadAgentPolicy(AgentPolicyState &state) {
    std::string error;
    if (!cand::loadPolicy(PolicyPath, state.policy, error)) {
        state.policy_failed = true;
        state.policy_errors.push_back(error);
        return false;
    }
    if (!cand::isStrictGeneratedPolicy(state.policy, error)) {
        state.policy_failed = true;
        state.policy_errors.push_back(error);
    }
    if (cand::normalizedRelativePath(PolicyPath) != "cand-policy.json") {
        state.policy_failed = true;
        state.policy_errors.push_back("--agent only accepts the repository-authoritative cand-policy.json path");
    }
    if (BaseRef != "origin/main" || !BasePolicyPath.empty()) {
        state.policy_failed = true;
        state.policy_errors.push_back("generated verification requires --base origin/main; --base-policy is not proof authority");
        return false;
    }
    const char *trusted_base_env = std::getenv("CAND_TRUSTED_BASE_SHA");
    if (!trusted_base_env || TrustedBaseCommit.empty() || TrustedBaseCommit != gitOriginMain()) {
        state.policy_failed = true;
        state.policy_errors.push_back("CAND_TRUSTED_BASE_SHA must be supplied by the trusted verifier runner and match origin/main");
        return false;
    }
    if (BaseRef != state.policy.base_ref) {
        state.policy_failed = true;
        state.policy_errors.push_back("--base differs from the base ref configured by the effective policy");
    }
    cand::Policy before;
    const bool loaded = cand::loadPolicyAtRef(BaseRef, PolicyPath, before, error);
    if (!loaded) {
        if (BasePolicyPath.empty() && error.find("base policy not found") == 0) {
            state.review_required = true;
            state.delta.classification = "REVIEW_REQUIRED";
            state.delta.review_required = true;
            state.delta.changes.push_back({"initial-policy", "absent", PolicyPath, "REVIEW_REQUIRED"});
        } else {
            state.policy_failed = true;
            state.policy_errors.push_back(error);
        }
    } else {
        state.delta = cand::comparePolicies(before, state.policy);
    }
    if (state.delta.weakened) state.policy_failed = true;
    if (state.delta.review_required) state.review_required = true;
    return !state.policy_failed;
}

bool validateAgentContract(AgentPolicyState &state) {
    if (ContractFile.empty()) return true;
    std::string digest, error, trust;
    const std::string normalized = cand::normalizedRelativePath(ContractFile);
    if (normalized.empty() || !cand::sha256File(ContractFile, digest, error) ||
        !cand::contractIsPinned(state.policy, normalized, digest, trust)) {
        state.policy_failed = true;
        state.review_required = true;
        state.policy_errors.push_back("contract path/content is not pinned by the effective policy");
        state.delta.review_required = true;
        state.delta.classification = state.delta.weakened ? "PROOF_WEAKENING" : "REVIEW_REQUIRED";
        state.delta.changes.push_back({"contract-set-substitution", "pinned trusted inputs", normalized,
                                       "REVIEW_REQUIRED"});
        ContractFile = ""; // candidate or substituted contracts never reach the analyzer
        return false;
    }
    state.contract_inputs.emplace_back(normalized, digest, trust);
    return true;
}

void addDetectedPolicyViolations(AgentPolicyState &state) {
    auto violation = [&](llvm::StringRef kind, unsigned count) {
        if (count == 0) return;
        state.policy_failed = true;
        state.policy_errors.push_back((kind + ": detected " + llvm::Twine(count)).str());
        state.delta.changes.push_back({kind.str(), "0", std::to_string(count), "PROOF_WEAKENING"});
        state.delta.weakened = true;
        state.delta.classification = "PROOF_WEAKENING";
    };
    violation("new-unsafe-boundaries", state.unsafe_boundaries);
    violation("new-suppressions", state.suppressions);
}

void validateAgentFrontendEnvironment(AgentPolicyState &state) {
    const char *variables[] = {
        "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
        "COMPILER_PATH", "GCC_EXEC_PREFIX", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET",
        "CFLAGS", "CPPFLAGS", "CXXFLAGS", "LDFLAGS", "CLANG_CONFIG_FILE"
    };
    for (const char *variable : variables) {
        if (const char *value = std::getenv(variable); value && *value) {
            state.policy_failed = true;
            state.policy_errors.push_back(std::string("frontend environment variable is not permitted: ") + variable);
        }
        unsetenv(variable);
    }
}

void validateAgentFrontendArguments(const llvm::json::Array &arguments,
                                    AgentPolicyState &state) {
    const auto cwd = std::filesystem::current_path();
    const auto reject = [&](const std::string &detail) {
        state.policy_failed = true;
        state.policy_errors.push_back(detail);
    };
    bool pending_path = false;
    for (const auto &entry : arguments) {
        const auto value = entry.getAsString();
        if (!value) { reject("frontend argument is not a string"); continue; }
        const std::string argument = value->str();
        if (pending_path) {
            const std::filesystem::path path(argument);
            const auto resolved = (cwd / path).lexically_normal();
            if (path.is_absolute() || (resolved != cwd && resolved.string().find(cwd.string() + "/") != 0))
                reject("frontend path escapes the candidate workspace: " + argument);
            pending_path = false;
            continue;
        }
        if (argument == "-I" || argument == "-iquote" || argument == "-isystem" ||
            argument == "-idirafter" || argument == "-include" || argument == "-imacros") {
            pending_path = true;
            continue;
        }
        if (argument == "-isysroot" || llvm::StringRef(argument).starts_with("-isysroot=") ||
            llvm::StringRef(argument).starts_with("--sysroot=") || llvm::StringRef(argument).starts_with("@") ||
            argument == "-Xclang" || argument == "-load" || llvm::StringRef(argument).starts_with("-fplugin") ||
            llvm::StringRef(argument).starts_with("-fmodule") ||
            llvm::StringRef(argument).starts_with("-fpass-plugin") ||
            llvm::StringRef(argument).starts_with("-load-pass-plugin") || argument == "-mllvm" ||
            argument == "-include-pch" || argument == "-fpch-preprocess" ||
            llvm::StringRef(argument).starts_with("-resource-dir") ||
            llvm::StringRef(argument).starts_with("-working-directory")) {
            reject("frontend argument is not permitted in generated verification: " + argument);
            continue;
        }
        for (const char *prefix : {"-I", "-iquote", "-isystem", "-idirafter", "-include", "-imacros"}) {
            if (llvm::StringRef(argument).starts_with(prefix) && argument.size() > std::strlen(prefix)) {
                const std::filesystem::path path(argument.substr(std::strlen(prefix)));
                const auto resolved = (cwd / path).lexically_normal();
                if (path.is_absolute() || resolved.string().find(cwd.string() + "/") != 0)
                    reject("frontend path escapes the candidate workspace: " + argument);
                break;
            }
        }
    }
    if (pending_path) reject("frontend path flag is missing its path");
}

int writeEvidenceFile(const std::string &path, const std::string &evidence) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) { llvm::errs() << "cand: cannot write evidence file: " << path << '\n'; return 2; }
    output << evidence << '\n';
    if (!output) { llvm::errs() << "cand: failed writing evidence file: " << path << '\n'; return 2; }
    return 0;
}

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
                 << " check [--agent] [--profile semantic|generated] [--policy file] [--base ref] [--emit-evidence file] <source...> [-- <clang-args...>]\n"
                 << "       " << program << " policy diff --base <git-ref> --format json\n"
                 << "       " << program << " evidence verify <evidence.json>\n";
}

} // namespace

int main(int argc, const char **argv) {
    const std::string executable = llvm::sys::fs::getMainExecutable(argv[0], &CandExecutableAnchor);
    CandExecutablePath = executable;
    std::string executable_error;
    if (!executable.empty()) (void)cand::sha256File(executable, CandExecutableSha256, executable_error);
    if (const char *trusted_base = std::getenv("CAND_TRUSTED_BASE_SHA")) TrustedBaseCommit = trusted_base;
    if (argc >= 2 && llvm::StringRef(argv[1]) == "policy")
        return runPolicyDiff(argc, argv);
    if (argc >= 2 && llvm::StringRef(argv[1]) == "evidence")
        return runEvidenceVerify(argc, argv);
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

    if (ProfileName != "semantic" && ProfileName != "generated") {
        llvm::errs() << "cand: unsupported profile; supported values are semantic and generated\n";
        return 2;
    }
    const std::string requested_profile = ProfileName;
    if (ProfileName == "generated") AgentMode = true;
    const bool weaker_profile_requested = AgentMode &&
        ProfileName.getNumOccurrences() != 0 && requested_profile != "generated";
    if (AgentMode && SafetyLevel != "p0-temporal-lifecycle") {
        llvm::errs() << "cand: unsupported safety level; only p0-temporal-lifecycle is implemented\n";
        return 2;
    }
    if (AgentMode) {
        ProfileName = "generated";
        OutputFormat = "json";
    }

    auto &options_parser = parser_or_error.get();

    AgentPolicyState agent_state;
    llvm::json::Array source_inputs;
    llvm::json::Array frontend_args;
    if (AgentMode) {
        if (CandExecutableSha256.empty()) {
            llvm::errs() << "cand: cannot identify the running verifier binary\n";
            return 2;
        }
        (void)loadAgentPolicy(agent_state);
        if (weaker_profile_requested) {
            agent_state.policy_failed = true;
            agent_state.policy_errors.push_back("--agent cannot be combined with a weaker/non-generated profile");
            agent_state.delta.weakened = true;
            agent_state.delta.classification = "PROOF_WEAKENING";
            agent_state.delta.changes.push_back({"profile-override", requested_profile, "generated", "PROOF_WEAKENING"});
        }
        if (agent_state.policy.schema.empty()) {
            llvm::json::Object error;
            error["schema"] = "cand.agent-check/v1";
            error["result"] = "policy-error";
            error["policy_errors"] = agent_state.policy_errors;
            llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(error)));
            return 2;
        }
        (void)collectSourceInputs(options_parser, agent_state.policy, source_inputs,
                                  frontend_args, agent_state);
        bool saw_required_standard = false;
        for (const auto &argument : frontend_args) {
            if (auto standard = argument.getAsString()) {
                if (standard->starts_with("-std=")) {
                    if (*standard == "-std=c11" && agent_state.policy.frontend_standard == "c11")
                        saw_required_standard = true;
                    else {
                        agent_state.policy_failed = true;
                        agent_state.policy_errors.push_back("frontend standard differs from the effective policy");
                    }
                }
            }
        }
        if (!saw_required_standard) {
            agent_state.policy_failed = true;
            agent_state.policy_errors.push_back("frontend arguments must explicitly select -std=c11");
        }
        validateAgentFrontendEnvironment(agent_state);
        validateAgentFrontendArguments(frontend_args, agent_state);
        std::vector<std::string> extra_frontend_args;
        for (const auto &argument : frontend_args) {
            const auto value = argument.getAsString();
            if (value && *value != "-std=c11") extra_frontend_args.push_back(value->str());
        }
        if (extra_frontend_args != agent_state.policy.frontend_arguments) {
            agent_state.policy_failed = true;
            agent_state.policy_errors.push_back("frontend arguments differ from the policy-pinned argument list");
        }
        (void)validateAgentContract(agent_state);
    }

    clang::tooling::ClangTool tool(options_parser.getCompilations(),
                                   options_parser.getSourcePathList());
    tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
        "-DCAND_ANALYSIS=1", clang::tooling::ArgumentInsertPosition::BEGIN));

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

    if (collector.hasContractError()) {
        llvm::errs() << "cand: invalid trusted contract; no verdict is reported\n";
        return 2;
    }

    if (AgentMode) {
        addIncludedFiles(source_inputs, collector, agent_state);
        addDetectedPolicyViolations(agent_state);
    }

    collector.finalize();
    if (AgentMode) {
        llvm::json::Object analysis = collector.jsonObject();
        const auto semantic = analysis.getString("result").value_or("internal-error");
        const bool policy_fail = agent_state.policy_failed || agent_state.delta.weakened;
        const bool review = agent_state.review_required || agent_state.delta.review_required;
        const char *final_result = policy_fail ? "fail-policy" :
                                   (review ? "review-required" : semantic.data());
        llvm::json::Object evidence = buildEvidence(collector, agent_state,
            std::move(source_inputs), std::move(frontend_args), semantic, final_result);
        const std::string evidence_text = serializeJson(std::move(evidence));
        auto parsed_evidence = llvm::json::parse(evidence_text);
        if (!parsed_evidence || !parsed_evidence->getAsObject()) {
            llvm::errs() << "cand: internal evidence serialization error\n";
            return 2;
        }
        if (!EvidencePath.empty() && writeEvidenceFile(EvidencePath, evidence_text) != 0)
            return 2;
        printAgentJson(collector, agent_state,
            std::move(*parsed_evidence->getAsObject()), semantic, final_result, EvidencePath);
        if (policy_fail || review) return 4;
        return collector.exitCode();
    }
    if (OutputFormat == "json") {
        collector.printJson();
    } else {
        collector.printHuman();
    }

    return collector.exitCode();
}
