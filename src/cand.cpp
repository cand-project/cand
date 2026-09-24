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
#include "llvm/TargetParser/Host.h"
#include "llvm/Support/raw_ostream.h"

#include "agent_policy.hpp"

#ifndef CAND_VERIFIER_SOURCE_COMMIT
#define CAND_VERIFIER_SOURCE_COMMIT "unknown"
#endif
#ifndef CAND_TOOLCHAIN_CLANG_VERSION
#define CAND_TOOLCHAIN_CLANG_VERSION "unknown"
#define CAND_TOOLCHAIN_LLVM_VERSION "unknown"
#define CAND_TOOLCHAIN_TARGET "unknown"
#define CAND_TOOLCHAIN_STANDARD "unknown"
#define CAND_TOOLCHAIN_SYSROOT "unknown"
#define CAND_TOOLCHAIN_HOST "unknown"
#define CAND_TOOLCHAIN_ARCH "unknown"
#define CAND_TOOLCHAIN_CONTAINER "unknown"
#define CAND_TOOLCHAIN_ENVIRONMENT_DIGEST "unknown"
#define CAND_BUILD_COMPILER_ID "unknown"
#define CAND_BUILD_COMPILER_VERSION "unknown"
#define CAND_BUILD_COMPILER_PATH "unknown"
#define CAND_TOOLCHAIN_CMAKE_VERSION "unknown"
#define CAND_TOOLCHAIN_NINJA_VERSION "unknown"
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
llvm::cl::opt<std::string> AnnotationReviewPath(
    "annotation-review", llvm::cl::desc("reviewed C& declaration annotation YAML"),
    llvm::cl::init(""), llvm::cl::cat(CandCategory));
llvm::cl::opt<bool> AgentMode(
    "agent", llvm::cl::desc("strict generated-code verification mode"),
    llvm::cl::init(false), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> ProfileName(
    "profile", llvm::cl::desc("verification profile: semantic|generated"),
    llvm::cl::init("semantic"), llvm::cl::cat(CandCategory));
llvm::cl::opt<std::string> SafetyLevel(
    "level", llvm::cl::desc("implemented safety level: p0-temporal-lifecycle|cand1"),
    llvm::cl::init("p0-temporal-lifecycle"), llvm::cl::cat(CandCategory));
llvm::cl::opt<bool> PointerOutputContracts(
    "pointer-output-contracts",
    llvm::cl::desc("enable bounded produces_out_owner output contracts "
                   "(requires --level=cand1)"),
    llvm::cl::init(false), llvm::cl::cat(CandCategory));
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
    Unsupported() = default;
    Unsupported(std::string kind_value, std::string symbol_value, Location location_value)
        : kind(std::move(kind_value)), symbol(std::move(symbol_value)),
          primary(std::move(location_value)) {}
    std::string kind;
    std::string symbol;
    Location primary;
    std::string mechanism;
    std::string source_storage;
    std::string destination_storage;
    std::string tracked_state;
    std::string conflict_reason;
    std::string contract_fact;
    std::string body_fact;
    std::optional<unsigned> parameter_index;
    bool transport = false;
};

enum class ReturnEffect { None, Owned, BorrowFromArg, Unknown };
enum class ParamEffect { None, Borrow, TakeOwnership, Destroy, Unknown };
enum class SummaryOrigin { BodyVerified, BuiltinTrusted, ExternalTrusted, AnnotationTrusted, CandidateUntrusted, Unknown };

struct ContractConflict {
    std::string reason;
    std::optional<unsigned> parameter;
    std::string contract_fact;
    std::string body_fact;

    bool operator==(const ContractConflict &other) const {
        return reason == other.reason && parameter == other.parameter &&
               contract_fact == other.contract_fact && body_fact == other.body_fact;
    }
};

// #41: the reviewed out-owner output block of a produces_out_owner
// parameter. write_on_success selects the conditional form (the write
// happens only on the success path, guarded by the call result);
// success_nonzero is the success polarity of the return value; nullable
// records whether the produced object may still be NULL on the success
// path (fail-closed default: true).
struct OutOwnerContract {
    unsigned param = 0;
    bool write_on_success = false;
    bool success_nonzero = false;
    bool nullable = true;
    bool operator==(const OutOwnerContract &other) const {
        return param == other.param && write_on_success == other.write_on_success &&
               success_nonzero == other.success_nonzero && nullable == other.nullable;
    }
};

struct FunctionSummary {
    const FunctionDecl *function = nullptr;
    ReturnEffect return_effect = ReturnEffect::None;
    std::optional<unsigned> return_borrow_arg;
    std::vector<ParamEffect> params;
    SummaryOrigin origin = SummaryOrigin::Unknown;
    bool conflict = false;
    std::vector<ContractConflict> conflicts;
    // #41: reviewed produces_out_owner output contract (trusted external
    // summaries only; body-derived and annotation-derived summaries never
    // carry one).
    std::optional<OutOwnerContract> out_owner;

    bool operator==(const FunctionSummary &other) const {
        return return_effect == other.return_effect &&
               return_borrow_arg == other.return_borrow_arg && params == other.params &&
               origin == other.origin && conflict == other.conflict &&
               conflicts == other.conflicts && out_owner == other.out_owner;
    }
};

struct ContractSummary {
    std::optional<ReturnEffect> return_effect;
    std::optional<unsigned> return_borrow_arg;
    std::vector<std::optional<ParamEffect>> params;
    std::optional<OutOwnerContract> out_owner;
};

const char *returnEffectName(ReturnEffect effect) {
    switch (effect) {
    case ReturnEffect::None: return "none";
    case ReturnEffect::Owned: return "owned";
    case ReturnEffect::BorrowFromArg: return "borrow_from_arg";
    case ReturnEffect::Unknown: return "unknown";
    }
    return "unknown";
}

const char *paramEffectName(ParamEffect effect) {
    switch (effect) {
    case ParamEffect::None: return "no_ownership_effect";
    case ParamEffect::Borrow: return "borrow";
    case ParamEffect::TakeOwnership: return "consumes";
    case ParamEffect::Destroy: return "destroys";
    case ParamEffect::Unknown: return "unknown";
    }
    return "unknown";
}

// Compact fact strings for conflict reporting (milestone #39).
std::string contractFactsString(const ContractSummary &contract) {
    std::string out = returnEffectName(contract.return_effect.value_or(ReturnEffect::None));
    if (contract.return_borrow_arg) out += ":" + std::to_string(*contract.return_borrow_arg);
    out += "(";
    for (unsigned i = 0; i < contract.params.size(); ++i) {
        if (i) out += ",";
        out += contract.params[i] ? paramEffectName(*contract.params[i]) : "unspecified";
    }
    out += ")";
    return out;
}

std::string annotationSummaryFactsString(const FunctionSummary &summary) {
    std::string out = returnEffectName(summary.return_effect);
    if (summary.return_borrow_arg) out += ":" + std::to_string(*summary.return_borrow_arg);
    out += "(";
    for (unsigned i = 0; i < summary.params.size(); ++i) {
        if (i) out += ",";
        out += paramEffectName(summary.params[i]);
    }
    out += ")";
    return out;
}

// True when a pointer (or decayed pointer-array) type has a shape that can
// write or transport pointer storage through one more level of indirection.
// Such shapes stay outside the reviewed declaration-annotation vocabulary
// (issue #41 scope guard).
bool isPointerToPointerShape(clang::QualType type) {
    const clang::Type *desugared = type.getTypePtrOrNull();
    if (desugared == nullptr) return false;
    if (const auto *pointer = dyn_cast<clang::PointerType>(desugared)) {
        const clang::Type *inner = pointer->getPointeeType().getTypePtrOrNull();
        if (inner == nullptr) return false;
        if (isa<clang::PointerType>(inner)) return true;
        if (const auto *array = dyn_cast<clang::ArrayType>(inner))
            return isa<clang::PointerType>(array->getElementType().getTypePtrOrNull());
    }
    return false;
}

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
    bool operator==(const SummaryStore &other) const { return summaries_ == other.summaries_; }
private:
    std::map<std::string, FunctionSummary> summaries_;
};

class Collector {
public:
    void setProfile(llvm::StringRef level, bool pointer_output_contracts = false) {
        safety_level_ = level.str();
        // #41: the pointer-output rule set is a cand1 profile modifier; its
        // rule-set identity (profile and transport matrix version) changes
        // only when the feature is enabled.
        pointer_output_contracts_ = level == "cand1" && pointer_output_contracts;
        profile_ = level == "cand1"
                       ? (pointer_output_contracts_ ? "cand1/v1.1-draft" : "cand1/v1")
                       : "p0-semantic-core";
    }
    bool pointerOutputContracts() const { return pointer_output_contracts_; }
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
        if (unsupported.transport) {
            ++unsupported_transport_operations_;
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
    unsigned unsupportedBorrowCount() const { return unsupported_borrows_; }
    void noteHeapWidening() { ++heap_widenings_; }
    unsigned unsupportedTransportCount() const { return unsupported_transport_operations_; }

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
    unsigned unsupportedCount() const { return static_cast<unsigned>(unsupported_.size()); }

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
        root["safety_level"] = safety_level_;
        root["profile"] = profile_;
        // #41 [F13]: the feature marker appears only when the
        // pointer-output contracts profile is enabled; v1 runs are
        // byte-identical to before.
        if (pointer_output_contracts_) root["pointer_output_contracts"] = true;

        llvm::json::Array findings;
        for (const auto &finding : findings_list_) {
            llvm::json::Object obj;
            obj["id"] = finding.id;
            obj["rule_id"] = finding.rule_id;
            obj["severity"] = "error";
            obj["safety_level"] = safety_level_;
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
            if (!item.mechanism.empty()) obj["mechanism"] = item.mechanism;
            if (!item.source_storage.empty()) obj["source_storage"] = item.source_storage;
            if (!item.destination_storage.empty()) obj["destination_storage"] = item.destination_storage;
            if (!item.tracked_state.empty()) obj["tracked_state"] = item.tracked_state;
            if (!item.conflict_reason.empty()) obj["conflict_reason"] = item.conflict_reason;
            if (!item.contract_fact.empty()) obj["contract_fact"] = item.contract_fact;
            if (!item.body_fact.empty()) obj["body_fact"] = item.body_fact;
            if (item.parameter_index) {
                obj["parameter_index"] = static_cast<std::int64_t>(*item.parameter_index);
            }
            if (item.transport) obj["transport"] = true;
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
        coverage["heap_generation_widenings"] = static_cast<std::int64_t>(heap_widenings_);
        coverage["unsupported_transport_operations"] =
            static_cast<std::int64_t>(unsupported_transport_operations_);
        coverage["transport_rule_set"] =
            pointer_output_contracts_ ? "cand1-pointer-transport-v2"
                                      : "cand1-pointer-transport-v1";
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
    unsigned heap_widenings_ = 0;
    unsigned unsupported_transport_operations_ = 0;
    unsigned next_object_id_ = 1;
    std::string safety_level_ = "p0-temporal-lifecycle";
    std::string profile_ = "p0-semantic-core";
    bool pointer_output_contracts_ = false;
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
enum class ObjectOrigin { Allocation, Parameter, Unknown };
enum class ParameterCapability { None, Borrow, TakeOwnership, Destroy, Unknown };

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
//
// Interior (ADR-0031, issue #73 Area C) means the storage still points into
// its object_id's allocation but no longer at the exact base address: it
// holds the result of advancing a base pointer by a pure integer delta
// (`p += e`, `p++`, `q = p ± e`). The object id (lifetime link to the
// parent) is preserved, so uses after the parent's death stay detected,
// while every destruction/consumption path requires the exact base and
// stays fail-closed (see relationIsNonBase). The soundness boundary is
// scoped to well-defined executions: an integer delta applied to a pointer
// into object B yields either a pointer still derived from B or an
// out-of-bounds pointer, and the latter is UB in C; for lifetime purposes
// cand treats it as still borrowing B. The only well-defined way
// arithmetic can rebind to a DIFFERENT object is pointer-difference
// arithmetic (`p + (q - p)` == q), which requires q to point into the same
// array as p -- the same object -- or is itself UB; deltas mentioning a
// pointer value therefore stay poisoned. There is no Interior -> Base
// path: joins only degrade Interior to Unknown (keeping the object id),
// never upgrade it back.
enum class PointerRelation { Owner, Alias, Interior, Moved, MaybeMoved, Null, MaybeNull, Unknown };

constexpr unsigned kNullObjectId = 0;
constexpr unsigned kUnknownObjectId = std::numeric_limits<unsigned>::max();
// Allocation IDs occupy the low namespace; parameter-entry objects occupy a
// disjoint high namespace. The boundary is enforced in objectIdForAllocation.
constexpr unsigned kParameterObjectIdBase = 0x80000000u;

std::optional<unsigned> parameterObjectIdForIndex(unsigned index) {
    if (index >= kUnknownObjectId - kParameterObjectIdBase - 1)
        return std::nullopt;
    return kParameterObjectIdBase + index + 1;
}

struct ObjectInfo {
    ObjectState state = ObjectState::Untracked;
    ObjectOrigin origin = ObjectOrigin::Allocation;
    ParameterCapability capability = ParameterCapability::None;
    Location allocation;
    Location destruction;
    bool destruction_known = false;
    std::string destruction_storage;
    bool operator==(const ObjectInfo &other) const {
        return state == other.state && origin == other.origin && capability == other.capability &&
               destruction_known == other.destruction_known &&
               sameLocation(allocation, other.allocation) &&
               sameLocation(destruction, other.destruction) &&
               destruction_storage == other.destruction_storage;
    }
};

const char *objectEntryEvent(const ObjectInfo &object) {
    return object.origin == ObjectOrigin::Parameter ? "parameter-entry" : "allocation";
}

struct StorageBinding {
    // 0 is an explicit, definitely-null storage value. UINT_MAX is an
    // unresolved/ambiguous target and must never be interpreted as NULL.
    unsigned object_id = kUnknownObjectId;
    PointerRelation relation = PointerRelation::Unknown;
    Location relation_location;
    // #41: this storage holds a maybe-produced out-owner object whose
    // producing guard has not been refined yet. Orthogonal to PointerRelation:
    // it survives joins conservatively and any use of a marked binding is the
    // fail-closed unrefined-out-owner-use obligation. Nothing sets it unless
    // the pointer-output contract profile is enabled.
    bool produced_maybe = false;
    bool operator==(const StorageBinding &other) const {
        return object_id == other.object_id && relation == other.relation &&
               sameLocation(relation_location, other.relation_location) &&
               produced_maybe == other.produced_maybe;
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
    // #41: a maybe-produced binding stays maybe-produced when either incoming
    // path still carries the unrefined mark (conservative OR).
    const bool produced_maybe = a.produced_maybe || b.produced_maybe;

    if (a.object_id == b.object_id) {
        if (a.relation == b.relation) {
            return {a.object_id, a.relation, relation_location, produced_maybe};
        }
        // ADR-0031 Area C: Interior joined with any different relation
        // degrades to Unknown while KEEPING the object id. The joined
        // pointer may be interior on one path, so the exact-base
        // destruction predicate must stay fail-closed (Unknown with a
        // live object id is non-base too), and the lifetime link to the
        // parent object must survive so uses after its death stay
        // detected.
        if (a.relation == PointerRelation::Interior ||
            b.relation == PointerRelation::Interior) {
            return {a.object_id, PointerRelation::Unknown, relation_location, produced_maybe};
        }
        if (a.relation == PointerRelation::MaybeNull ||
            b.relation == PointerRelation::MaybeNull) {
            return {a.object_id, PointerRelation::MaybeNull, relation_location, produced_maybe};
        }
        const auto moved = [](PointerRelation relation) {
            return relation == PointerRelation::Moved ||
                   relation == PointerRelation::MaybeMoved;
        };
        if (moved(a.relation) || moved(b.relation)) {
            return {a.object_id, PointerRelation::MaybeMoved, relation_location, produced_maybe};
        }
        return {a.object_id, PointerRelation::Unknown, relation_location, produced_maybe};
    }
    if (unknown(a) || unknown(b)) {
        return {kUnknownObjectId, PointerRelation::Unknown, relation_location, produced_maybe};
    }
    // Null on one path and one known object on the other still has one heap
    // target for temporal purposes. Null-dereference safety is outside P0.
    if (known_null(a) && b.object_id != kNullObjectId) {
        return {b.object_id, PointerRelation::MaybeNull, relation_location, produced_maybe};
    }
    if (known_null(b) && a.object_id != kNullObjectId) {
        return {a.object_id, PointerRelation::MaybeNull, relation_location, produced_maybe};
    }
    // Two different non-null objects are an unresolved alias target, never NULL.
    return {kUnknownObjectId, PointerRelation::Unknown, relation_location, produced_maybe};
}

// ADR-0031 (issue #73, Area C): the delta expression of a pointer advance
// is a PURE INTEGER delta when no subexpression in its tree has pointer
// type. Integer literals and integer-typed variables qualify; any
// pointer-typed subexpression poisons the advance as today. This is the
// rebind boundary of the Interior relation: pointer-difference arithmetic
// (`p + (q - p)` == q) is the only well-defined way an arithmetic delta can
// move a pointer to a different object, and a well-defined pointer
// difference requires both operands to point into the same array -- the
// same object -- so deltas that mention a pointer value stay fail-closed.
// Note the operands of a pointer difference themselves have pointer type,
// so `p += (q - p)` is rejected by this walk even though the difference's
// own type is an integer.
bool isPureIntegerDelta(const Expr *expr) {
    if (expr == nullptr) return false;
    if (expr->getType()->isPointerType()) return false;
    for (const Stmt *child : expr->children()) {
        const auto *child_expr = dyn_cast<Expr>(child);
        if (child_expr != nullptr && !isPureIntegerDelta(child_expr)) return false;
    }
    return true;
}

// Relations from which an integer-delta advance may preserve the parent
// object: any live-object binding except moved-from storages (using a
// moved-from pointer must keep today's poisoning) and #41 unrefined
// maybe-produced bindings (their uses must keep the
// unrefined-out-owner-use obligation). Unknown with a live object id is
// what an Interior join degrades to, so a loop-carried cursor (entry
// Alias, back-edge Interior) keeps advancing instead of poisoning; the
// exact-base-required destruction predicate keeps such cursors
// fail-closed at every destroy.
bool relationSupportsIntegerAdvance(const StorageBinding &binding) {
    return !binding.produced_maybe &&
           binding.object_id != kUnknownObjectId &&
           binding.object_id != kNullObjectId &&
           binding.relation != PointerRelation::Moved &&
           binding.relation != PointerRelation::MaybeMoved;
}

// ADR-0031 (issue #73, Area C): exact-base-required destruction
// predicate. ISO C requires free and every ownership
// destruction/consumption to receive the exact base pointer of the
// allocation. A storage whose relation is Interior -- or Unknown while it
// still holds a live object id, which is what an Interior join degrades
// to -- may point inside the object, so every destruction path stays
// fail-closed until a base relation is re-established (the lattice has no
// Interior -> Base path). Bindings without an object id (today's poisoned
// storages) keep their existing unresolved/untracked obligations and are
// deliberately not covered here.
bool relationIsNonBase(const StorageBinding &binding) {
    return binding.object_id != kUnknownObjectId &&
           binding.object_id != kNullObjectId &&
           (binding.relation == PointerRelation::Interior ||
            binding.relation == PointerRelation::Unknown);
}

// #41: a pending out-owner guard records that a local variable currently
// holds the result of an accepted produces_out_owner call, so a later
// recognized single-form condition on that variable can refine the
// destination binding by the contract's success polarity.
struct PendingOutGuard {
    StorageId dest;
    bool success_nonzero = false;
    // The producing call this entry testifies about. An embedded-assignment
    // guard (C2) is materialized by the CFG as a call element followed by
    // the assignment element; the assignment must not kill the entry its
    // own RHS call just recorded (#41 kill rule (a) exemption).
    const CallExpr *call = nullptr;
    bool operator==(const PendingOutGuard &other) const {
        return dest == other.dest && success_nonzero == other.success_nonzero &&
               call == other.call;
    }
};

struct FlowState {
    std::map<StorageId, StorageBinding> storages;
    std::map<unsigned, ObjectInfo> objects;
    std::map<StorageId, BorrowInfo> borrows;
    std::map<unsigned, unsigned> allocation_generations;
    std::set<unsigned> widened_allocation_sites;
    // #41: result-variable -> producing-call guard fact. Conservative
    // intersection at joins: an entry survives only when both incoming
    // paths carry the identical fact. Nothing populates it unless the
    // pointer-output contract profile is enabled.
    std::map<StorageId, PendingOutGuard> pending_out_guards;

    bool operator==(const FlowState &other) const {
        return storages == other.storages && objects == other.objects && borrows == other.borrows &&
               allocation_generations == other.allocation_generations &&
               widened_allocation_sites == other.widened_allocation_sites &&
               pending_out_guards == other.pending_out_guards;
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
    result.allocation_generations = a.allocation_generations;
    for (const auto &entry : b.allocation_generations) {
        auto &generation = result.allocation_generations[entry.first];
        generation = std::max(generation, entry.second);
    }
    result.widened_allocation_sites = a.widened_allocation_sites;
    result.widened_allocation_sites.insert(b.widened_allocation_sites.begin(),
                                           b.widened_allocation_sites.end());
    result.objects = a.objects;
    for (const auto &entry : b.objects) {
        auto it = result.objects.find(entry.first);
        if (it == result.objects.end()) {
            result.objects[entry.first] = entry.second;
        } else {
            it->second.state = joinState(it->second.state, entry.second.state);
            if (it->second.origin != entry.second.origin)
                it->second.origin = ObjectOrigin::Unknown;
            if (it->second.capability != entry.second.capability)
                it->second.capability = ParameterCapability::Unknown;
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
    // #41: conservative intersection of pending out-owner guards -- an entry
    // survives a join only when every incoming path carries the same fact.
    for (const auto &entry : a.pending_out_guards) {
        const auto it = b.pending_out_guards.find(entry.first);
        if (it != b.pending_out_guards.end() && it->second == entry.second) {
            result.pending_out_guards[entry.first] = entry.second;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Per-function flow analysis
// ---------------------------------------------------------------------------

// Declaration-site annotations that were found in the TU but did not
// resolve to a reviewed summary (milestone #39). These sets only ever
// change the *kind* of the fail-closed obligation emitted for the
// symbol's calls; they never seed summaries or grant PASS authority.
struct AnnotationReviewFacts {
    std::set<std::string> unreviewed;    // no manifest entry / fact mismatch
    std::set<std::string> conflicting;   // contradictory annotation facts
};

class FlowAnalyzer {
public:
    FlowAnalyzer(ASTContext &context, Collector &collector, const SummaryStore &summaries,
                 bool cand1_profile, const AnnotationReviewFacts &annotation_review,
                 bool pointer_output_contracts)
        : context_(context), source_manager_(context.getSourceManager()),
          collector_(collector), summaries_(summaries), cand1_profile_(cand1_profile),
          annotation_review_(annotation_review),
          pointer_output_contracts_(cand1_profile && pointer_output_contracts) {}

    // The fail-closed obligation kind for a call to a symbol whose
    // declaration annotations were not accepted by a review manifest.
    llvm::StringRef annotationOverrideKind(const CallExpr &call) const {
        const FunctionDecl *callee = call.getDirectCallee();
        if (callee == nullptr) return {};
        const std::string name = callee->getNameAsString();
        if (annotation_review_.conflicting.count(name) != 0)
            return "conflicting-declaration-annotation";
        if (annotation_review_.unreviewed.count(name) != 0)
            return "unreviewed-declaration-annotation";
        return {};
    }

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
        collectPointerOutputFacts(body);

        std::unique_ptr<CFG> cfg =
            CFG::buildCFG(&function, const_cast<Stmt *>(body), &context_, CFG::BuildOptions());
        if (!cfg) {
            emitUnsupported(
                {"cfg-unavailable", "", location(function.getLocation())});
            return;
        }
        cfg_ = cfg.get();
        // #41: record every CallExpr materialized as (or within) a CFG
        // block element. The defensive terminator pass uses this to avoid
        // processing a later block's element call with this block's state.
        if (pointer_output_contracts_) {
            cfg_element_calls_.clear();
            for (CFG::const_iterator it = cfg->begin(); it != cfg->end(); ++it) {
                for (CFGBlock::const_iterator ei = (*it)->begin(); ei != (*it)->end(); ++ei) {
                    if (ei->getKind() != CFGElement::Statement) continue;
                    const std::optional<CFGStmt> element_stmt = ei->getAs<CFGStmt>();
                    if (element_stmt) collectElementCalls(element_stmt->getStmt());
                }
            }
        }
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
    static std::string transportMechanism(llvm::StringRef kind) {
        if (kind.contains("realloc")) return "realloc";
        if (kind.contains("memcpy")) return "memcpy";
        if (kind.contains("memmove")) return "memmove";
        if (kind.starts_with("aggregate") || kind.starts_with("struct") ||
            kind.starts_with("array")) return "aggregate";
        if (kind.starts_with("union")) return "union";
        if (kind.starts_with("cast") || kind.starts_with("borrow-cast")) return "cast";
        if (kind.starts_with("pointer-integer")) return "pointer-integer";
        if (kind.starts_with("pointer-arithmetic")) return "pointer-arithmetic";
        if (kind.starts_with("global-or-static")) return "global-static";
        if (kind.starts_with("unknown-call") || kind.starts_with("indirect-call")) return "unknown-call";
        if (kind.starts_with("out-parameter")) return "out-parameter";
        if (kind.starts_with("vararg")) return "varargs";
        if (kind.starts_with("atomic")) return "atomic";
        if (kind.starts_with("nonlocal")) return "nonlocal-control-flow";
        if (kind.starts_with("inline-asm")) return "inline-asm";
        if (kind.starts_with("realloc")) return "realloc";
        if (kind.starts_with("unresolved-pointee") || kind.starts_with("unmodelled-pointer") ||
            kind.starts_with("unresolved-access") || kind.starts_with("ambiguous-alias")) return "pointer-storage";
        if (kind.starts_with("unknown-pointer-return")) return "unknown-return";
        return {};
    }

    void emitUnsupported(Unsupported unsupported) {
        if (emitting_) {
            if (cand1_profile_) {
                unsupported.mechanism = transportMechanism(unsupported.kind);
                unsupported.transport = !unsupported.mechanism.empty();
                if (unsupported.transport) unsupported.tracked_state = "tracked-pointer";
            }
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
        const llvm::StringRef override_kind = annotationOverrideKind(call);
        unsupported.kind = override_kind.empty()
            ? "unknown-pointer-return-ownership:" + unknownPointerSymbol(call)
            : (override_kind + ":" + unknownPointerSymbol(call)).str();
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

    void markContractConflict(const CallExpr &call, const FunctionSummary &summary) {
        Unsupported unsupported{"contract-body-conflict", "", location(call.getExprLoc())};
        if (const FunctionDecl *callee = call.getDirectCallee()) {
            unsupported.symbol = callee->getNameAsString();
        }
        if (!summary.conflicts.empty()) {
            const ContractConflict &detail = summary.conflicts.front();
            unsupported.conflict_reason = detail.reason;
            unsupported.contract_fact = detail.contract_fact;
            unsupported.body_fact = detail.body_fact;
            unsupported.parameter_index = detail.parameter;
        }
        emitUnsupported(std::move(unsupported));
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
        finding.trace.push_back({objectEntryEvent(object), "Owned", object.allocation});
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
        finding.trace.push_back({objectEntryEvent(object), "Owned", object.allocation});
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
        finding.trace.push_back({objectEntryEvent(object), "Owned", object.allocation});
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
        finding.trace.push_back({objectEntryEvent(object), "Owned", object.allocation});
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

    void checkMutableOwnerAccess(const Expr *pointer_expr,
                                 SourceLocation access_loc,
                                 const FlowState &state) {
        const auto storage = storageFor(pointer_expr);
        if (!storage || state.borrows.count(*storage)) return;
        const StorageBinding *binding = bindingFor(pointer_expr, state);
        if (binding == nullptr || binding->relation != PointerRelation::Owner) return;
        for (const auto &entry : state.borrows) {
            if (entry.second.parent_object_id != binding->object_id ||
                entry.second.kind != BorrowKind::Mutable ||
                entry.second.state != BorrowState::Live ||
                !borrowLiveAfter(entry.first, pointer_expr)) continue;
            reportBorrowFinding("CAND-B004", "p2-borrow-lifetime-v1",
                                "owner access conflicts with live mutable borrow",
                                entry.first, entry.second, access_loc, state);
        }
    }

    void checkAccess(const Expr *pointer_expr, SourceLocation access_loc,
                     const FlowState &state) {
        if (checkBorrowAccess(pointer_expr, access_loc, state)) return;
        checkMutableOwnerAccess(pointer_expr, access_loc, state);
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
            // ADR-0031 (issue #73, Area A): an address-of whose pointee is
            // pointer-free storage reached this block only because nothing
            // inside it is tracked (tracked `&p[0]`/`&s->f` forms are held
            // by the findTrackedBinding above). The callee receives the
            // callee-local copy of a pointer-free stack slot; a read (or a
            // read-or-write borrow claim) of pointer-free scalar storage is
            // ownership-neutral: a scalar write cannot fabricate,
            // duplicate, or clobber a tracked pointer. The type filter is
            // conservative scoping, not the soundness load-bearer.
            if (pointer_expr != nullptr) {
                const auto *addr_of = dyn_cast<UnaryOperator>(
                    pointer_expr->IgnoreParenCasts());
                if (addr_of != nullptr &&
                    addr_of->getOpcode() == clang::UO_AddrOf &&
                    !typeMayContainPointer(addr_of->getSubExpr()->getType())) {
                    return; // &pointer-free storage: ownership-neutral
                }
            }
            if (containsParameterStorage(pointer_expr)) {
                emitUnsupported({"unmodelled-pointer-parameter", "",
                                 location(access_loc)});
                return;
            }
            return; // genuinely untracked storage: outside the current P0 heap scope
        }
        // #41 [F8]: the produced-maybe mark is tested BEFORE the
        // relation-silence paths -- a use of an unrefined maybe-produced
        // binding is the fail-closed unrefined-out-owner-use obligation,
        // never the silent null-deref path.
        if (pointer_output_contracts_ && binding->produced_maybe) {
            emitUnsupported({"unrefined-out-owner-use", "", location(access_loc)});
            return;
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
        // #41 [F8]: the mark is tested BEFORE the free(NULL) no-op path --
        // destroying an unrefined maybe-produced binding is the
        // unrefined-out-owner-use obligation.
        if (pointer_output_contracts_ && binding.produced_maybe) {
            emitUnsupported({"unrefined-out-owner-use", "", location(call.getExprLoc())});
            return;
        }
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
        // ADR-0031 (issue #73, Area C): exact-base-required predicate.
        // free() must receive the allocation's base pointer; an Interior
        // (or Unknown-with-live-object-id) cursor may point inside it.
        // Checked before the parameter-capability finding: an interior
        // cursor is not destroyable regardless of whose parameter it
        // derived from, and the obligation is the fail-closed verdict.
        if (relationIsNonBase(binding)) {
            emitUnsupported({"destroy-of-non-base", "", location(call.getExprLoc())});
            return;
        }
        if (object->origin == ObjectOrigin::Parameter &&
            object->capability == ParameterCapability::Borrow) {
            reportOwnershipViolation(
                "CAND-O006", "ownership.destroy-borrowed-parameter",
                "borrowed parameter cannot be destroyed", binding, *object,
                *destroy_storage, call.getExprLoc(), state);
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
        // ADR-0031 (issue #73, Area C): exact-base-required predicate
        // (see relationIsNonBase); a destroy effect on an interior cursor
        // must stay fail-closed. Checked before the parameter-capability
        // finding, mirroring handleFree.
        if (relationIsNonBase(binding)) {
            markUnsupported(call, "destroy-of-non-base");
            return;
        }
        if (object.origin == ObjectOrigin::Parameter &&
            object.capability == ParameterCapability::Borrow) {
            reportOwnershipViolation(
                "CAND-O006", "ownership.destroy-borrowed-parameter",
                "borrowed parameter cannot be destroyed", binding, object,
                *storage, call.getExprLoc(), state);
            return;
        }
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
        finding.trace.push_back({objectEntryEvent(object), "Owned", object.allocation});
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
        if (state.borrows.count(*source)) {
            markUnsupportedAt(move_loc, "move-borrowed-storage");
            if (emitting_) collector_.noteUnsupportedBorrow();
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
        if (object.origin == ObjectOrigin::Parameter &&
            object.capability != ParameterCapability::TakeOwnership) {
            if (call) markUnsupportedAt(move_loc, "parameter-capability-transfer");
            return false;
        }
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
        // ADR-0031 (issue #73, Area C): exact-base-required predicate
        // (see relationIsNonBase); moving an interior cursor hands the
        // callee a non-base pointer it may destroy.
        if (relationIsNonBase(binding)) {
            markUnsupportedAt(move_loc, "destroy-of-non-base");
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
        return AgentMode || ProfileName == "generated" || SafetyLevel == "cand1";
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
        // ADR-0031 (issue #73, Area C): exact-base-required predicate
        // (see relationIsNonBase). A TakeOwnership callee may destroy or
        // re-base the received pointer, so consuming an interior cursor
        // stays fail-closed.
        if (relationIsNonBase(binding->second)) {
            markUnsupported(call, "destroy-of-non-base");
            return;
        }
        const auto object_it = state.objects.find(binding->second.object_id);
        if (object_it != state.objects.end() &&
            object_it->second.origin == ObjectOrigin::Parameter &&
            object_it->second.capability != ParameterCapability::TakeOwnership) {
            markUnsupported(call, "parameter-capability-transfer");
            return;
        }
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

    // ------------------------------------------------------------------
    // #41: produces_out_owner flow machinery
    // ------------------------------------------------------------------

    // Per-function pre-pass (plan sections 2.2/2.6). Walks the body in
    // preorder so a produces call is recorded before the escape walk can
    // reach its out-slot AddrOf child, and collects:
    //  - locals whose address is taken anywhere other than as the out-slot
    //    argument of a produces call (address-escape refusal),
    //  - which local storage receives each call's result (pending-guard
    //    recording; deterministic, no AST-parent queries),
    //  - produces calls lexically inside a loop (loop refusal; the
    //    back-edge-joined destination pre-state leaves the allowed set).
    void collectPointerOutputFacts(const Stmt *body) {
        escaped_addr_locals_.clear();
        out_slot_locals_.clear();
        call_result_storages_.clear();
        produce_slot_addrs_.clear();
        loop_scoped_calls_.clear();
        if (!pointer_output_contracts_) return;
        collectPointerOutputFactsWalk(body, 0);
    }

    // #41: collects every CallExpr in an element statement's subtree into
    // cfg_element_calls_ (see analyze()).
    void collectElementCalls(const Stmt *stmt) {
        if (stmt == nullptr) return;
        if (const auto *call = dyn_cast<CallExpr>(stmt)) cfg_element_calls_.insert(call);
        for (const Stmt *child : stmt->children()) collectElementCalls(child);
    }

    // #41: pre-marks condition-subtree calls that are CFG block elements
    // as processed, so the defensive terminator pass cannot evaluate a
    // later block's element call with this block's state. The element's
    // own block processes the whole subtree with the correct in-state.
    void skipElementCallsOfOtherBlocks(const Stmt *stmt,
                                       std::set<const Stmt *> &processed) const {
        if (stmt == nullptr) return;
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            if (cfg_element_calls_.count(call) != 0) {
                processed.insert(call);
                return;
            }
        }
        for (const Stmt *child : stmt->children()) {
            skipElementCallsOfOtherBlocks(child, processed);
        }
    }

    void collectPointerOutputFactsWalk(const Stmt *stmt, unsigned loop_depth) {
        if (stmt == nullptr) return;
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (binary->getOpcode() == clang::BO_Assign) {
                if (const CallExpr *call = asCall(binary->getRHS())) {
                    if (const auto storage = storageFor(binary->getLHS()))
                        call_result_storages_[call] = *storage;
                }
            }
        } else if (const auto *decl_stmt = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *decl : decl_stmt->decls()) {
                const auto *var = dyn_cast<VarDecl>(decl);
                if (var != nullptr && var->hasInit()) {
                    if (const CallExpr *call = asCall(var->getInit())) {
                        if (var->hasLocalStorage() && !isa<ParmVarDecl>(var))
                            call_result_storages_[call] =
                                StorageId{StorageKind::LocalVariable, var, {}, -1};
                    }
                }
            }
        } else if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            if (loop_depth > 0) loop_scoped_calls_.insert(call);
            const FunctionSummary *summary = summaryFor(*call);
            if (summary != nullptr && summary->out_owner &&
                summary->out_owner->param < call->getNumArgs()) {
                const Expr *arg = call->getArg(summary->out_owner->param)->IgnoreParens();
                const auto *addr = dyn_cast<UnaryOperator>(arg);
                if (addr != nullptr && addr->getOpcode() == clang::UO_AddrOf) {
                    const auto *ref = dyn_cast<DeclRefExpr>(addr->getSubExpr()->IgnoreParens());
                    const auto *var = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
                    if (var != nullptr && var->hasLocalStorage() && !isa<ParmVarDecl>(var)) {
                        produce_slot_addrs_.insert(addr);
                        out_slot_locals_.insert(var);
                    }
                }
            }
        } else if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->getOpcode() == clang::UO_AddrOf &&
                produce_slot_addrs_.count(unary) == 0) {
                const auto *ref = dyn_cast<DeclRefExpr>(unary->getSubExpr()->IgnoreParens());
                const auto *var = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
                if (var != nullptr && var->hasLocalStorage() && !isa<ParmVarDecl>(var))
                    escaped_addr_locals_.insert(var);
            }
        }
        const unsigned child_depth =
            loop_depth + ((isa<WhileStmt>(stmt) || isa<ForStmt>(stmt) || isa<DoStmt>(stmt))
                              ? 1u
                              : 0u);
        for (const Stmt *child : stmt->children())
            collectPointerOutputFactsWalk(child, child_depth);
    }

    // Syntactic destination storage of a produces call's out-slot
    // argument (AddrOf of a function-local scalar variable), independent
    // of flow state. Parens are transparent; casts are not.
    std::optional<StorageId> destStorageForProduceCall(const CallExpr &call,
                                                       const OutOwnerContract &effect) const {
        if (effect.param >= call.getNumArgs()) return std::nullopt;
        const Expr *arg = call.getArg(effect.param)->IgnoreParens();
        const auto *addr = dyn_cast<UnaryOperator>(arg);
        if (addr == nullptr || addr->getOpcode() != clang::UO_AddrOf) return std::nullopt;
        const auto *ref = dyn_cast<DeclRefExpr>(addr->getSubExpr()->IgnoreParens());
        const auto *var = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
        if (var == nullptr || !var->hasLocalStorage() || isa<ParmVarDecl>(var))
            return std::nullopt;
        return StorageId{StorageKind::LocalVariable, var, {}, -1};
    }

    // True when the expression mentions the variable anywhere in its
    // subexpression tree (as a value or through its address).
    bool exprMentionsVar(const Expr *expr, const VarDecl *var) const {
        if (expr == nullptr) return false;
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (ref->getDecl() == var) return true;
        }
        for (const Stmt *child : expr->children()) {
            const auto *child_expr = dyn_cast<Expr>(child);
            if (child_expr != nullptr && exprMentionsVar(child_expr, var)) return true;
        }
        return false;
    }

    // The produces_out_owner call-site acceptance predicate (plan section
    // 2.2). Returns the destination storage when the call is accepted;
    // nullopt keeps today's fail-closed obligation (the refusal falls
    // through to the ordinary unknown-call path, byte-identical to v1).
    std::optional<StorageId> acceptsOutOwnerProduce(const CallExpr &call,
                                                    const OutOwnerContract &effect,
                                                    const FlowState &state) const {
        const FunctionDecl *callee = call.getDirectCallee();
        if (callee == nullptr) return std::nullopt; // indirect callee
        if (callee->isVariadic()) return std::nullopt; // variadic callee
        if (callee->getNameAsString().find("realloc") != std::string::npos)
            return std::nullopt; // in-place production is a different transport
        if (loop_scoped_calls_.count(&call) != 0) return std::nullopt; // 2.2.3
        const auto dest = destStorageForProduceCall(call, effect);
        if (!dest) return std::nullopt;
        // The destination address must not be taken anywhere else in the
        // function (plan 2.2.4, caller-side alias-join hazard).
        if (dest->root != nullptr && escaped_addr_locals_.count(dest->root) != 0)
            return std::nullopt;
        // `dest = f(&dest)` / `T *dest = f(&dest)`: the slot is both
        // destination and result; refuse.
        const auto result = call_result_storages_.find(&call);
        if (result != call_result_storages_.end() && result->second == *dest)
            return std::nullopt;
        // Destination pre-state: absent, Null, Moved, or MaybeMoved for
        // write: always; exactly Null for write: on_success (the failure
        // edge must never read an unrepresentable slot value).
        const auto binding = state.storages.find(*dest);
        if (binding != state.storages.end()) {
            const PointerRelation relation = binding->second.relation;
            const bool allowed = relation == PointerRelation::Null ||
                                 relation == PointerRelation::Moved ||
                                 relation == PointerRelation::MaybeMoved;
            if (!allowed) return std::nullopt;
            if (effect.write_on_success && relation != PointerRelation::Null)
                return std::nullopt;
        } else if (effect.write_on_success) {
            return std::nullopt; // uninitialized destination
        }
        return dest;
    }

    // Applies an accepted produce: binds the destination to a fresh
    // produced object through the existing allocation-site machinery
    // (loop produces never reach here, so generation widening is moot,
    // but the same site-id identity rules apply). write:always +
    // nullable:false is immediately usable; every other combination
    // carries the produced-maybe mark, so any use before a recognized
    // refinement is the fail-closed unrefined-out-owner-use obligation.
    void applyOutOwnerProduce(const CallExpr &call, const OutOwnerContract &effect,
                              const StorageId &dest, FlowState &state) {
        const unsigned object_id = objectIdForAllocation(&call);
        if (object_id == kUnknownObjectId) {
            state.storages[dest] = {kUnknownObjectId, PointerRelation::Unknown,
                                    location(call.getExprLoc()), true};
            markUnsupported(call, "allocation-object-id-exhausted");
            return;
        }
        const bool immediately_usable = !effect.write_on_success && !effect.nullable;
        state.storages[dest] = {object_id,
                                immediately_usable ? PointerRelation::Owner
                                                   : PointerRelation::MaybeNull,
                                location(call.getExprLoc()), !immediately_usable};
        auto &object = state.objects[object_id];
        object.state = ObjectState::Owned;
        object.allocation = location(call.getExprLoc());
        bound_objects_.insert(object_id);
        // Stale guards that referenced this destination die here: a second
        // produce into the same slot invalidates earlier result guards.
        for (auto it = state.pending_out_guards.begin(); it != state.pending_out_guards.end();) {
            if (it->second.dest == dest) it = state.pending_out_guards.erase(it);
            else ++it;
        }
    }

    // Strips parentheses, implicit casts, and leading `!` operators,
    // tracking negation parity (`!!x` is `x` in C). Explicit casts are not
    // stripped: a cast subject is an unrecognized form.
    static const Expr *normalizeGuardCondition(const Expr *expr, bool &negated) {
        while (expr != nullptr) {
            expr = expr->IgnoreParenImpCasts();
            if (expr == nullptr) break;
            if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
                if (unary->getOpcode() == clang::UO_LNot) {
                    negated = !negated;
                    expr = unary->getSubExpr();
                    continue;
                }
            }
            break;
        }
        return expr;
    }

    // Success/failure refinement by the guard's result polarity (C1/C3,
    // plan section 2.4). `this_eq` is true when `subject == K` holds on
    // this branch edge (truthiness is the K = 0, `!=` degenerate form).
    void refineByGuardPolarity(FlowState &state, const StorageId &dest,
                               bool success_nonzero, bool this_eq, bool K_zero) const {
        const auto binding = state.storages.find(dest);
        if (binding == state.storages.end() || !binding->second.produced_maybe) return;
        const bool zero = !success_nonzero;
        bool is_success, is_failure;
        if (this_eq) {
            is_success = zero == K_zero;
            is_failure = !is_success;
        } else {
            is_success = !zero && K_zero;
            is_failure = zero && K_zero;
        }
        if (is_success && binding->second.object_id != kNullObjectId &&
            binding->second.object_id != kUnknownObjectId) {
            state.storages[dest] = {binding->second.object_id, PointerRelation::Owner,
                                    binding->second.relation_location, false};
        } else if (is_failure) {
            // The failure edge keeps the pre-call Null binding and carries
            // the produced-maybe mark (F8): a use here is unrefined, never
            // the silent null-deref / free(NULL) path.
            state.storages[dest] = {kNullObjectId, PointerRelation::Null,
                                    binding->second.relation_location, true};
        }
        // Neither: the edge asserts nothing about the polarity; leave the
        // binding untouched (fail closed).
    }

    // C4 null-check refinement of a maybe-produced destination: the
    // non-null edge promotes to Owner and clears the mark; the null edge
    // pins Null and keeps the mark.
    void refineByDestNullCheck(FlowState &state, const StorageId &dest,
                               bool non_null_edge) const {
        const auto binding = state.storages.find(dest);
        if (binding == state.storages.end() || !binding->second.produced_maybe) return;
        if (non_null_edge) {
            if (binding->second.object_id != kNullObjectId &&
                binding->second.object_id != kUnknownObjectId) {
                state.storages[dest] = {binding->second.object_id, PointerRelation::Owner,
                                        binding->second.relation_location, false};
            }
        } else {
            state.storages[dest] = {kNullObjectId, PointerRelation::Null,
                                    binding->second.relation_location, true};
        }
    }

    // Plan section 2.4: refines one branch edge of a recognized
    // single-form out-owner guard. The whole-condition rule [F3]:
    // compounds and unrecognized forms refine nothing -- neither
    // partially nor compositionally.
    void applyOutOwnerRefinement(const Expr *cond, bool true_edge, FlowState &state) const {
        if (!pointer_output_contracts_) return;
        bool negated = false;
        const Expr *subject = normalizeGuardCondition(cond, negated);
        if (subject == nullptr) return;
        const bool subject_true = true_edge != negated;

        // Comparison against a constant: `subject == K` / `!= K`. A NULL
        // comparand is the K = 0 form. Truthiness is `subject != 0`.
        bool eq_form = false;
        bool has_constant = false;
        bool K_zero = true;
        const Expr *guard_subject = subject;
        if (const auto *binary = dyn_cast<BinaryOperator>(subject)) {
            const clang::BinaryOperatorKind opcode = binary->getOpcode();
            if (opcode == clang::BO_EQ || opcode == clang::BO_NE) {
                eq_form = opcode == clang::BO_EQ;
                if (isNullConstant(binary->getRHS())) {
                    has_constant = true;
                    K_zero = true;
                } else if (auto constant = binary->getRHS()->getIntegerConstantExpr(context_)) {
                    has_constant = true;
                    K_zero = constant->isZero();
                } else {
                    return; // non-constant comparand: unrecognized
                }
                guard_subject = binary->getLHS()->IgnoreParenImpCasts();
            } else if (opcode == clang::BO_Assign) {
                // C2 truthiness (`if ((r = call))`): the assignment reduces
                // to its left-hand side; the call element was processed by
                // the block transfer, so the pending entry exists.
                guard_subject = binary->getLHS()->IgnoreParenImpCasts();
            } else {
                return; // relational and other operators never refine [R18]
            }
        }
        // On this branch edge, does `subject == K` hold? Truthiness is the
        // K = 0, `!=` degenerate form: the subject being truthy means
        // `subject != 0`, so the negation parity folds into this_eq.
        const bool this_eq = has_constant ? (subject_true ? eq_form : !eq_form)
                                          : !subject_true;

        // C2: an embedded assignment in the condition reduces to its
        // left-hand side (the assignment was already processed by the
        // block transfer, so the pending entry exists).
        if (const auto *assign = dyn_cast<BinaryOperator>(guard_subject)) {
            if (assign->getOpcode() != clang::BO_Assign) return;
            guard_subject = assign->getLHS();
        }
        guard_subject = guard_subject->IgnoreParenImpCasts();
        if (const auto *inner = dyn_cast<UnaryOperator>(guard_subject)) {
            if (inner->getOpcode() == clang::UO_LNot) return; // only !! was normalized
        }

        if (const auto *call = dyn_cast<CallExpr>(guard_subject)) {
            // C1: the produces call directly in the condition. Routed by
            // RETURN polarity only -- a call expression is never a C4
            // destination null-check [F1].
            const FunctionSummary *summary = summaryFor(*call);
            if (summary == nullptr || !summary->out_owner) return;
            const auto dest = destStorageForProduceCall(*call, *summary->out_owner);
            if (!dest) return;
            refineByGuardPolarity(state, *dest, summary->out_owner->success_nonzero,
                                  this_eq, K_zero);
            return;
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(guard_subject)) {
            const auto storage = storageFor(ref);
            if (!storage) return;
            // C3 first: a pending result guard (a variable holding the
            // call result is a result subject, not a destination subject).
            const auto pending = state.pending_out_guards.find(*storage);
            if (pending != state.pending_out_guards.end()) {
                refineByGuardPolarity(state, pending->second.dest,
                                      pending->second.success_nonzero, this_eq, K_zero);
                return;
            }
            // C4: null-check of a maybe-produced destination. Only a bare
            // DeclRefExpr subject, and only against NULL/0.
            if (!K_zero) return;
            refineByDestNullCheck(state, *storage, subject_true != eq_form);
            return;
        }
    }

    // The condition of a branch terminator, when the terminator is one of
    // the recognized loop/branch statements.
    static const Expr *terminatorCondition(const Stmt *terminator) {
        if (const auto *if_stmt = dyn_cast<IfStmt>(terminator)) return if_stmt->getCond();
        if (const auto *while_stmt = dyn_cast<WhileStmt>(terminator)) return while_stmt->getCond();
        if (const auto *for_stmt = dyn_cast<ForStmt>(terminator)) return for_stmt->getCond();
        if (const auto *do_stmt = dyn_cast<DoStmt>(terminator)) return do_stmt->getCond();
        return nullptr;
    }

    // #41: any use of a still-marked maybe-produced binding as a call
    // argument is the fail-closed unrefined-out-owner-use obligation
    // (plan 2.3, transfer/pass-to-unknown-call uses). The accepted
    // produce's own out-slot argument is the defining write, not a use.
    void noteUnrefinedOutOwnerUsesInArgs(const CallExpr &call, const FlowState &state,
                                         const OutOwnerContract *skip_effect) {
        if (!pointer_output_contracts_) return;
        for (const auto &entry : state.storages) {
            if (!entry.second.produced_maybe || entry.first.root == nullptr) continue;
            for (unsigned i = 0; i < call.getNumArgs(); ++i) {
                if (skip_effect != nullptr && i == skip_effect->param) continue;
                if (exprMentionsVar(call.getArg(i), entry.first.root)) {
                    emitUnsupported({"unrefined-out-owner-use", "",
                                     location(call.getExprLoc())});
                    break;
                }
            }
        }
    }

    // #41: kill rule (b) -- pending guards whose destination (or its
    // address) appears in this call's arguments die here. The accepted
    // produce's own out-slot argument is exempt.
    void killPendingGuardsInCall(const CallExpr &call, FlowState &state,
                                 const OutOwnerContract *skip_effect,
                                 const std::optional<StorageId> &accepted_dest) const {
        if (!pointer_output_contracts_ || state.pending_out_guards.empty()) return;
        for (auto it = state.pending_out_guards.begin(); it != state.pending_out_guards.end();) {
            const VarDecl *dest_var = it->second.dest.root;
            bool killed = false;
            for (unsigned i = 0; i < call.getNumArgs() && !killed; ++i) {
                if (skip_effect != nullptr && i == skip_effect->param) continue;
                if (dest_var != nullptr && exprMentionsVar(call.getArg(i), dest_var))
                    killed = true;
            }
            if (!killed && accepted_dest && it->second.dest == *accepted_dest) killed = true;
            it = killed ? state.pending_out_guards.erase(it) : std::next(it);
        }
    }

    // #41: kill rule (a) -- reassignment of the result variable or any
    // write to the destination storage.
    // #41: kill rule (a) -- reassignment of the result variable or any
    // write to the destination storage. `exempt_call` spares the entry
    // recorded by that very call (a C2 embedded assignment's own RHS).
    void killPendingGuardsForWrite(const StorageId &storage, FlowState &state,
                                   const CallExpr *exempt_call = nullptr) const {
        if (!pointer_output_contracts_ || state.pending_out_guards.empty()) return;
        for (auto it = state.pending_out_guards.begin(); it != state.pending_out_guards.end();) {
            if (storage == it->first || storage == it->second.dest) {
                if (it->second.call != nullptr && it->second.call == exempt_call) {
                    ++it;
                    continue;
                }
                it = state.pending_out_guards.erase(it);
            }
            else ++it;
        }
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
        // #41: produces_out_owner handling. The acceptance predicate runs
        // first; a refused call skips the contract branch entirely and
        // falls through to the ordinary unknown-call path, so its rows are
        // byte-identical to a run without the feature (v1 matrix identity).
        const FunctionSummary *summary = summaryFor(call);
        const OutOwnerContract *out_effect = nullptr;
        std::optional<StorageId> accepted_dest;
        if (pointer_output_contracts_ && summary != nullptr && summary->out_owner) {
            out_effect = &*summary->out_owner;
            accepted_dest = acceptsOutOwnerProduce(call, *out_effect, state);
        }
        // Only an accepted produce's own out-slot argument is exempt from
        // the argument scan; a refused call's out-slot pass is an ordinary
        // escape of the destination address.
        const OutOwnerContract *skip_effect = accepted_dest ? out_effect : nullptr;
        killPendingGuardsInCall(call, state, skip_effect, accepted_dest);
        noteUnrefinedOutOwnerUsesInArgs(call, state, skip_effect);
        if (summary && (!out_effect || accepted_dest)) {
            if (summary->conflict) {
                markContractConflict(call, *summary);
                return;
            }
            if (summary->return_effect == ReturnEffect::Unknown) {
                markUnsupported(call, "unknown-pointer-return-ownership");
                return;
            }
            for (unsigned i = 0; i < call.getNumArgs() && i < summary->params.size(); ++i) {
                if (summary->params[i] == ParamEffect::Destroy) {
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
            // Variadic and other argument positions beyond the modelled
            // parameter list carry no param effect, yet the callee may read
            // or retain tracked storage passed there (the summary scan
            // already treats such positions as ParamEffect::Unknown). Report
            // the same escape and borrow-retention obligations the
            // unknown-call path below would report, so these positions can
            // never yield a trustworthy PASS.
            for (unsigned i = summary->params.size(); i < call.getNumArgs(); ++i) {
                const Expr *arg = call.getArg(i);
                if (!containsTrackedStorage(arg, state)) continue;
                std::string kind = "unknown-call-with-tracked-pointer";
                if (const FunctionDecl *callee = call.getDirectCallee())
                    kind += ":" + callee->getNameAsString();
                else
                    kind += ":indirect";
                markUnsupported(call, kind);
                if (borrowFor(arg, state) != nullptr) {
                    std::string retention = "unknown-call-borrow-retention";
                    if (const FunctionDecl *callee = call.getDirectCallee())
                        retention += ":" + callee->getNameAsString();
                    else
                        retention += ":indirect";
                    markUnsupported(call, retention);
                    if (emitting_) collector_.noteUnsupportedBorrow();
                }
            }
            // #41: apply the accepted produce after the sibling parameter
            // effects, then record the pending result guard when the call's
            // result flows into a function-local variable (C2/C3). The
            // result variable's address must not be taken anywhere in the
            // function: a callee could overwrite the stored result and the
            // guard would no longer testify about the call.
            if (out_effect != nullptr && accepted_dest) {
                applyOutOwnerProduce(call, *out_effect, *accepted_dest, state);
                const auto result = call_result_storages_.find(&call);
                if (out_effect->write_on_success && result != call_result_storages_.end() &&
                    result->second.kind == StorageKind::LocalVariable &&
                    (result->second.root == nullptr ||
                     escaped_addr_locals_.count(result->second.root) == 0)) {
                    state.pending_out_guards[result->second] = {*accepted_dest,
                                                                out_effect->success_nonzero,
                                                                &call};
                }
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
            const llvm::StringRef override_kind = annotationOverrideKind(call);
            markUnsupported(call, override_kind.empty() ? "unknown-call-with-pointer-output"
                                                        : override_kind);
        }
        if (!call.getType()->isPointerType() && typeMayContainPointer(call.getType())) {
            markUnsupported(call, "unknown-aggregate-return-ownership");
        }
        if (tracked_argument) {
            std::string kind = "unknown-call-with-tracked-pointer";
            const llvm::StringRef override_kind = annotationOverrideKind(call);
            if (!override_kind.empty()) {
                kind = override_kind.str();
            } else if (const FunctionDecl *callee = call.getDirectCallee()) {
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
        const unsigned site = allocationSiteFor(init);
        unsigned generation = state.allocation_generations[site];
        if (cand1_profile_ && state.widened_allocation_sites.count(site) != 0) {
            state.storages[storage] =
                {kUnknownObjectId, PointerRelation::Unknown, location(init->getExprLoc())};
            markUnsupported(*init, "loop-heap-instance-widening");
            if (emitting_) collector_.noteHeapWidening();
            return;
        }
        if (cand1_profile_ && hasRetainedGenerationReference(site, storage, state)) {
            if (generation >= 1) {
                state.widened_allocation_sites.insert(site);
                state.storages[storage] =
                    {kUnknownObjectId, PointerRelation::Unknown, location(init->getExprLoc())};
                markUnsupported(*init, "loop-heap-instance-widening");
                if (emitting_) collector_.noteHeapWidening();
                return;
            }
            generation = 1;
            state.allocation_generations[site] = generation;
        }
        if (!cand1_profile_ && containsLoopAllocation(init)) {
            markUnsupported(*init, "loop-allocation-site");
        }
        const unsigned object_id = objectIdForAllocation(init, generation);
        if (object_id == kUnknownObjectId) {
            state.storages[storage] =
                {kUnknownObjectId, PointerRelation::Unknown, location(init->getExprLoc())};
            markUnsupported(*init, "allocation-object-id-exhausted");
            return;
        }
        state.storages[storage] =
            {object_id, PointerRelation::Owner, location(init->getExprLoc())};
        auto &object = state.objects[object_id];
        object.state = ObjectState::Owned;
        object.allocation = location(init->getExprLoc());
        bound_objects_.insert(object_id);
    }

    unsigned allocationSiteFor(const Expr *init) {
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

    unsigned objectIdForAllocation(const Expr *init, unsigned generation = 0) {
        const unsigned site = allocationSiteFor(init);
        // The stride keeps generation identities deterministic without adding
        // another global allocator to the fixed-point state. Widening occurs
        // before generation 2, so this remains a finite abstraction.
        constexpr unsigned kGenerationStride = 1000000;
        if (generation == 0)
            return site < kParameterObjectIdBase ? site : kUnknownObjectId;
        if (site > std::numeric_limits<unsigned>::max() / kGenerationStride)
            return kUnknownObjectId;
        const unsigned object_id = site + generation * kGenerationStride;
        return object_id < kParameterObjectIdBase ? object_id : kUnknownObjectId;
    }

    bool hasRetainedGenerationReference(unsigned site, const StorageId &destination,
                                        const FlowState &state) const {
        constexpr unsigned kGenerationStride = 1000000;
        const auto belongsToSite = [site](unsigned object_id) {
            if (object_id == kNullObjectId || object_id == kUnknownObjectId) return false;
            return object_id % kGenerationStride == site;
        };
        for (const auto &entry : state.storages) {
            if (entry.first == destination) continue;
            if (belongsToSite(entry.second.object_id)) return true;
        }
        for (const auto &entry : state.borrows) {
            if (belongsToSite(entry.second.parent_object_id)) return true;
        }
        return false;
    }

    void bindSummaryReturn(const StorageId &storage, const CallExpr &call,
                           FlowState &state) {
        const FunctionSummary *summary = summaryFor(call);
        if (!summary || summary->conflict || summary->origin == SummaryOrigin::Unknown) {
            const llvm::StringRef override_kind = annotationOverrideKind(call);
            markUnsupported(call, override_kind.empty() ? "unknown-pointer-return-ownership"
                                                        : override_kind);
            return;
        }
        if (summary->return_effect == ReturnEffect::Owned) {
            if (!cand1_profile_ && containsLoopAllocation(&call))
                markUnsupported(call, "loop-allocation-site");
            const unsigned site = allocationSiteFor(&call);
            unsigned generation = state.allocation_generations[site];
            if (cand1_profile_ && state.widened_allocation_sites.count(site) != 0) {
                state.storages[storage] =
                    {kUnknownObjectId, PointerRelation::Unknown, location(call.getExprLoc())};
                markUnsupported(call, "loop-heap-instance-widening");
                if (emitting_) collector_.noteHeapWidening();
                return;
            }
            if (cand1_profile_ && hasRetainedGenerationReference(site, storage, state)) {
                if (generation >= 1) {
                    state.widened_allocation_sites.insert(site);
                    state.storages[storage] =
                        {kUnknownObjectId, PointerRelation::Unknown, location(call.getExprLoc())};
                    markUnsupported(call, "loop-heap-instance-widening");
                    if (emitting_) collector_.noteHeapWidening();
                    return;
                }
                generation = 1;
                state.allocation_generations[site] = generation;
            }
            const unsigned id = objectIdForAllocation(&call, generation);
            if (id == kUnknownObjectId) {
                state.storages[storage] =
                    {kUnknownObjectId, PointerRelation::Unknown, location(call.getExprLoc())};
                markUnsupported(call, "allocation-object-id-exhausted");
                return;
            }
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
                    // ADR-0031 (issue #73, Area C): an interior argument
                    // yields an interior borrow; the relation must not
                    // silently degrade to a base Alias, which would let a
                    // later destroy of this storage bypass the
                    // exact-base-required predicate.
                    const PointerRelation relation =
                        it->second.relation == PointerRelation::Interior
                            ? PointerRelation::Interior
                            : PointerRelation::Alias;
                    state.storages[storage] = {it->second.object_id, relation,
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
                } else if ((var->getType()->isRecordType() || var->getType()->isArrayType()) &&
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
                if (isExplicitMove(init)) {
                    markUnsupported(decl_stmt, "move-borrowed-storage");
                    if (emitting_) collector_.noteUnsupportedBorrow();
                    continue;
                }
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
                        // ADR-0031 (issue #73, Area C): keep Interior
                        // through an explicit borrow annotation (an
                        // interior cursor can be borrowed; its destroys
                        // stay fail-closed via the exact-base predicate).
                        const PointerRelation relation =
                            it->second.relation == PointerRelation::Interior
                                ? PointerRelation::Interior
                                : PointerRelation::Alias;
                        state.storages[storage] = {it->second.object_id, relation,
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
                                  : it->second.relation == PointerRelation::Interior
                                        ? PointerRelation::Interior
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

    // ADR-0031 (issue #73, Area C): `w ± e` cross-lvalue advance
    // (`q = p + 1`). Returns the base storage's binding when rhs is
    // pointer arithmetic on a single tracked base storage by a pure
    // integer delta from a relation that supports the advance; the
    // caller binds the destination to the same object with relation
    // Interior and emits no obligation. Anything else (pointer-mentioning
    // delta, untracked/unknown/null base, moved, maybe-null, or unrefined
    // maybe-produced base) returns null and falls through to today's
    // fail-closed paths.
    const StorageBinding *integerAdvanceBinding(const Expr *rhs,
                                                const FlowState &state) const {
        const auto *advance = dyn_cast<BinaryOperator>(rhs->IgnoreParenCasts());
        if (advance == nullptr) return nullptr;
        if (advance->getOpcode() != clang::BO_Add &&
            advance->getOpcode() != clang::BO_Sub)
            return nullptr;
        if (!isPureIntegerDelta(advance->getRHS())) return nullptr;
        const auto source = storageFor(advance->getLHS());
        if (!source) return nullptr;
        const auto it = state.storages.find(*source);
        if (it == state.storages.end() ||
            !relationSupportsIntegerAdvance(it->second))
            return nullptr;
        return &it->second;
    }

    void handleAssignment(const BinaryOperator &binary, FlowState &state) {
        const Expr *lhs = binary.getLHS();
        const Expr *rhs = binary.getRHS();
        const auto lhs_storage = storageFor(lhs);
        // #41 kill rule (a): any write to a pending guard's result
        // variable or destination storage kills the guard entry. Placed
        // before every early return so compound assignments are covered.
        // Exemption: a C2 embedded-assignment guard (`(r = f(&out)) == 0`)
        // is materialized by the CFG as the call element followed by the
        // assignment element, so this assignment's own RHS call already
        // recorded the entry; that entry survives to the branch refinement.
        if (lhs_storage) killPendingGuardsForWrite(*lhs_storage, state, asCall(rhs));
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
            // ADR-0031 (issue #73, Area C): a compound advance `p += e` /
            // `p -= e` whose delta e is a pure integer expression cannot
            // rebind the pointer to a different object in a well-defined
            // execution (see isPureIntegerDelta): keep the parent object
            // id and record the interior position instead of poisoning
            // the storage, with no obligation here. The exact-base-
            // required destruction predicate keeps every later destroy/
            // free/move/consume of the advanced cursor fail-closed.
            // Pointer-mentioning deltas (e.g. `p += (q - p)`) and any
            // other compound opcode keep today's poisoning.
            const clang::BinaryOperatorKind opcode = binary.getOpcode();
            if ((opcode == clang::BO_AddAssign || opcode == clang::BO_SubAssign) &&
                lhs_storage && isPureIntegerDelta(rhs)) {
                const auto it = state.storages.find(*lhs_storage);
                if (it != state.storages.end() &&
                    relationSupportsIntegerAdvance(it->second)) {
                    it->second.relation = PointerRelation::Interior;
                    it->second.relation_location = location(binary.getExprLoc());
                    return;
                }
            }
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
                // ADR-0031 (issue #73, Area C): the cross-lvalue integer
                // advance `q = w ± e` preserves w's parent object with
                // relation Interior (no obligation); every other
                // non-storage rhs keeps today's fail-closed handling.
                const StorageBinding *integer_advance =
                    lhs_storage ? integerAdvanceBinding(rhs, state) : nullptr;
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
                                      : source_it->second.relation == PointerRelation::Interior
                                            ? PointerRelation::Interior
                                            : PointerRelation::Alias;
                        state.storages[*lhs_storage] =
                            {source_it->second.object_id, relation,
                             location(binary.getExprLoc())};
                    }
                } else if (integer_advance != nullptr) {
                    state.storages[*lhs_storage] =
                        {integer_advance->object_id, PointerRelation::Interior,
                         location(binary.getExprLoc())};
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
                                  : source_it->second.relation == PointerRelation::Interior
                                        ? PointerRelation::Interior
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
            bool live_borrowed_return = false;
            if (current_summary_ &&
                current_summary_->return_effect == ReturnEffect::BorrowFromArg) {
                const StorageBinding *binding = bindingFor(ret, state);
                if (binding == nullptr) binding = findTrackedBinding(ret, state);
                const ObjectInfo *object = objectFor(binding, state);
                if (object == nullptr || object->state == ObjectState::Unknown) {
                    markUnsupported(return_stmt, "return-unknown-ownership-state");
                    return;
                }
                if (object->state == ObjectState::Dead ||
                    object->state == ObjectState::MaybeDead) {
                    auto access_storage = storageFor(ret);
                    if (!access_storage) access_storage = findTrackedStorage(ret, state);
                    if (access_storage)
                        reportUseAfterDestroy(*binding, *object, *access_storage,
                                              return_stmt.getReturnLoc(), state,
                                              object->state == ObjectState::MaybeDead);
                    return;
                }
                live_borrowed_return = true;
            }
            if (current_summary_ && current_summary_->return_effect == ReturnEffect::Owned) {
                if (const auto storage = storageFor(ret)) {
                    const auto binding = state.storages.find(*storage);
                    if (binding != state.storages.end()) {
                        const auto object = state.objects.find(binding->second.object_id);
                        live_owned_return = object != state.objects.end() && object->second.state == ObjectState::Owned;
                    }
                }
            }
            if (!live_owned_return && !live_borrowed_return)
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
                const FunctionSummary *summary = summaryFor(*call);
                if (isAllocatorCall(*call) || call->getType()->isPointerType() ||
                    (summary && summary->return_effect == ReturnEffect::Owned))
                    loop_allocation_sites_.insert(call);
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

    FlowState seedParameterState() {
        FlowState state;
        if (current_summary_ == nullptr || current_summary_->function == nullptr) return state;
        const FunctionDecl &function = *current_summary_->function;
        for (unsigned index = 0; index < function.param_size(); ++index) {
            const ParmVarDecl *param = function.getParamDecl(index);
            if (!param->getType()->isPointerType() || index >= current_summary_->params.size())
                continue;

            ParameterCapability capability = ParameterCapability::Unknown;
            PointerRelation relation = PointerRelation::Alias;
            switch (current_summary_->params[index]) {
            case ParamEffect::Borrow:
                capability = ParameterCapability::Borrow;
                break;
            case ParamEffect::TakeOwnership:
                capability = ParameterCapability::TakeOwnership;
                relation = PointerRelation::Owner;
                break;
            case ParamEffect::Destroy:
                capability = ParameterCapability::Destroy;
                break;
            case ParamEffect::None:
            case ParamEffect::Unknown:
                // Parameters neither proven-borrowed nor proven-consumed by the
                // verified body are still tracked objects: they are live at
                // function entry regardless of who ultimately holds authority,
                // so reads and writes through them are decidable until a destroy
                // or an opaque escape is actually observed. Ownership authority
                // is deliberately absent (ParameterCapability::Unknown), which
                // keeps every transfer operation fail-closed via the capability
                // checks in moveBinding/transferBinding, and makes an escape
                // into an unknown or indirect call produce the same
                // unknown-call-with-tracked-pointer obligation a local pointer
                // would (parity; the escape can no longer pass silently).
                // ParamEffect::None parameters are never referenced by the
                // verified body, so seeding them is a no-op.
                capability = ParameterCapability::Unknown;
                break;
            }

            const auto object_id = parameterObjectIdForIndex(index);
            const StorageId storage{StorageKind::LocalVariable, param, {}, -1};
            if (!object_id) {
                emitUnsupported({"parameter-object-id-exhausted", "", location(param->getLocation())});
                state.storages[storage] =
                    {kUnknownObjectId, PointerRelation::Unknown, location(param->getLocation())};
                continue;
            }
            state.storages[storage] = {*object_id, relation, location(param->getLocation())};
            ObjectInfo object;
            object.state = ObjectState::Owned;
            object.origin = ObjectOrigin::Parameter;
            object.capability = capability;
            object.allocation = location(param->getLocation());
            state.objects[*object_id] = std::move(object);
        }
        return state;
    }

    void run() {
        std::map<unsigned, FlowState> in_states;
        std::map<unsigned, FlowState> out_states;
        std::set<unsigned> worklist;
        const CFGBlock &entry = cfg_->getEntry();
        in_states[entry.getBlockID()] = seedParameterState();
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

            // #41: per-edge refinement for recognized single-form out-owner
            // guards (plan 2.4). Applied to the edge state derived from this
            // block's out state, before the successor join. Loop
            // terminators clear pending guard entries from both out-edges
            // AFTER the per-edge refinement (kill rule (c)): a guard
            // established before a loop may refine the loop's own condition
            // edges, but never survives into the body or past the loop.
            const Stmt *terminator = block->getTerminatorStmt();
            const Expr *terminator_cond =
                terminator != nullptr ? terminatorCondition(terminator) : nullptr;
            const bool loop_terminator = terminator != nullptr &&
                (isa<WhileStmt>(terminator) || isa<ForStmt>(terminator) ||
                 isa<DoStmt>(terminator));
            unsigned edge_index = 0;
            for (CFGBlock::const_succ_iterator si = block->succ_begin();
                 si != block->succ_end(); ++si, ++edge_index) {
                const CFGBlock *successor = *si;
                if (successor == nullptr) {
                    continue;
                }
                const unsigned sid = successor->getBlockID();
                FlowState joined = out;
                if (terminator_cond != nullptr) {
                    applyOutOwnerRefinement(terminator_cond, edge_index == 0, joined);
                }
                if (loop_terminator) joined.pending_out_guards.clear();
                const auto existing = in_states.find(sid);
                if (existing != in_states.end()) {
                    joined = joinFlow(existing->second, joined);
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
            const Expr *defensive_cond = nullptr;
            if (const auto *if_stmt = dyn_cast<IfStmt>(terminator)) {
                defensive_cond = if_stmt->getCond();
            } else if (const auto *switch_stmt = dyn_cast<SwitchStmt>(terminator)) {
                defensive_cond = switch_stmt->getCond();
            } else if (const auto *while_stmt = dyn_cast<WhileStmt>(terminator)) {
                defensive_cond = while_stmt->getCond();
            } else if (const auto *for_stmt = dyn_cast<ForStmt>(terminator)) {
                defensive_cond = for_stmt->getCond();
            } else if (const auto *do_stmt = dyn_cast<DoStmt>(terminator)) {
                defensive_cond = do_stmt->getCond();
            } else if (isa<IndirectGotoStmt>(terminator)) {
                markUnsupported(*terminator, "indirect-goto");
            }
            if (defensive_cond != nullptr) {
                // #41: short-circuit chains (A || B, A && B) materialize
                // each operand's side effects as elements of the operand's
                // own block, but every chain block carries the FULL
                // condition as its terminator. A call that is an element
                // of another block must be processed by that block with
                // its own in-state; processing it here would pre-apply its
                // produce and make the element pass see a live destination
                // (a spurious refusal). With the feature off the set is
                // empty and the defensive pass is unchanged.
                skipElementCallsOfOtherBlocks(defensive_cond, processed);
                processStmt(defensive_cond, state, processed);
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
    bool cand1_profile_ = false;
    const AnnotationReviewFacts &annotation_review_;
    const FunctionSummary *current_summary_ = nullptr;
    // #41: the pointer-output contract profile (cand1 only). Every
    // produces_out_owner behavior below is gated on this flag; with it off
    // the analyzer is bit-identical to the v1 rule set.
    bool pointer_output_contracts_ = false;
    // #41: per-function pre-pass result -- locals whose address is taken
    // anywhere other than as the out-slot argument of a produces call.
    std::set<const VarDecl *> escaped_addr_locals_;
    // #41: locals used as the out-slot argument of a produces call.
    std::set<const VarDecl *> out_slot_locals_;
    // #41: per-function pre-pass result -- for every assignment or
    // declaration initializer whose right-hand side is a call, the local
    // storage that receives the call's result. Deterministic
    // (AST-parent-free) recording of where a produces call's result lands.
    std::map<const CallExpr *, StorageId> call_result_storages_;
    // #41: the AddrOf expressions that are the out-slot argument of a
    // produces call (excluded from the escape walk).
    std::set<const Stmt *> produce_slot_addrs_;
    // #41: produces calls lexically inside a loop (While/For/Do) -- the
    // back-edge-joined destination pre-state leaves the allowed set, and
    // the syntactic refusal also keeps the worklist fixed point free of
    // accept-then-refuse oscillation (v1 row identity for loop shapes).
    std::set<const CallExpr *> loop_scoped_calls_;
    // #41: every CallExpr materialized as (or within) a CFG block element
    // of the current function. Populated only when the feature is on.
    std::set<const CallExpr *> cfg_element_calls_;
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

    // #41 kill rule (a), increment/decrement form: `r++` overwrites a
    // pending guard's result variable.
    if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
        const clang::UnaryOperatorKind opcode = unary->getOpcode();
        if (opcode == clang::UO_PreInc || opcode == clang::UO_PostInc ||
            opcode == clang::UO_PreDec || opcode == clang::UO_PostDec) {
            if (const auto storage = storageFor(unary->getSubExpr()))
                killPendingGuardsForWrite(*storage, state);
        }
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
            // ADR-0031 (issue #73, Area C): `p++` / `p--` (both pre and
            // post forms) advance the cursor by the pure integer delta 1,
            // so the parent object is preserved with relation Interior
            // and no obligation is emitted (see isPureIntegerDelta for
            // the rebind boundary; the exact-base-required destruction
            // predicate keeps destroys of the advanced cursor
            // fail-closed). Bases that do not support the advance keep
            // today's poisoning.
            const auto storage = storageFor(unary->getSubExpr());
            bool advanced = false;
            if (storage) {
                const auto it = state.storages.find(*storage);
                if (it != state.storages.end() &&
                    relationSupportsIntegerAdvance(it->second)) {
                    it->second.relation = PointerRelation::Interior;
                    it->second.relation_location = location(unary->getOperatorLoc());
                    advanced = true;
                }
            }
            if (!advanced) {
                markUnsupported(*unary, "pointer-arithmetic-reassignment");
                if (storage) {
                    state.storages[*storage] =
                        {kUnknownObjectId, PointerRelation::Unknown,
                         location(unary->getOperatorLoc())};
                }
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
        // ADR-0031 (issue #73), Area R: resolve local-origin return
        // chains before the scan so the ReturnStmt handler can fall back
        // to the origin dataflow wherever today's direct resolution
        // fails closed.
        computeReturnOrigins(function);
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
        // Incident #64: "first parameter contained anywhere in the
        // expression" is a syntactic occurrence test, not an origin
        // resolution; compound expressions referencing several pointer
        // parameters (conditional/comma operators) or mixing a parameter
        // with a foreign pointer misattributed the borrow origin and
        // produced false PASSes at callers. Resolve an origin only from
        // expressions unambiguously derived from a single pointer
        // parameter; everything else fails closed to Unknown.
        std::optional<unsigned> origin;
        if (!parameterDerivedOrigin(expr, f, origin)) return std::nullopt;
        return origin;
    }

    // Null-form check usable without ASTContext: NULL / (void*)0 reduce to
    // an IntegerLiteral 0 after IgnoreParenCasts; __null is GNUNullExpr.
    static bool neutralNullForm(const Expr *expr) {
        if (const auto *lit = dyn_cast<clang::IntegerLiteral>(expr))
            return lit->getValue() == 0;
        return isa<clang::GNUNullExpr>(expr);
    }

    // Incident #64 resolver. Returns true when `expr` is derived (at object
    // granularity) from a single pointer parameter or is a neutral null
    // form; `origin` carries the parameter index when there is one.
    //   accepted: p, p->arr (array-member decay), p->s.arr, &p->f, &p[i],
    //             p + n, p - n, c ? p : p, c ? p : NULL, (a, b) on b
    //   rejected: distinct-parameter conditionals/comma, references to
    //             non-parameter pointers, pointer-returning calls,
    //             value reads from parameter storage (p->f with pointer
    //             member, p[i] on T**, *pp), &p (the parameter object
    //             itself), member accesses on by-value parameters
    static bool parameterDerivedOrigin(const Expr *raw, const FunctionDecl &f,
                                       std::optional<unsigned> &origin) {
        const Expr *expr = raw ? raw->IgnoreParenCasts() : nullptr;
        if (!expr) return false;
        if (neutralNullForm(expr)) return true;
        if (const auto direct = parameterIndex(expr, f)) {
            if (f.getParamDecl(*direct)->getType()->isPointerType()) {
                origin = *direct;
                return true;
            }
            return false;  // non-pointer parameter cannot be an origin
        }
        if (const auto *cond = dyn_cast<ConditionalOperator>(expr)) {
            std::optional<unsigned> lhs, rhs;
            if (!parameterDerivedOrigin(cond->getTrueExpr(), f, lhs)) return false;
            if (!parameterDerivedOrigin(cond->getFalseExpr(), f, rhs)) return false;
            if (lhs && rhs && *lhs != *rhs) return false;
            origin = lhs ? lhs : rhs;
            return true;
        }
        if (const auto *comma = dyn_cast<BinaryOperator>(expr);
            comma && comma->isCommaOp()) {
            const Expr *last = comma->getRHS();
            while (true) {
                const auto *inner = dyn_cast<BinaryOperator>(last->IgnoreParenCasts());
                if (!inner || !inner->isCommaOp()) break;
                last = inner->getRHS();
            }
            return parameterDerivedOrigin(last, f, origin);
        }
        if (const auto *bin = dyn_cast<BinaryOperator>(expr);
            bin && (bin->getOpcode() == clang::BO_Add || bin->getOpcode() == clang::BO_Sub) &&
            bin->getType()->isPointerType()) {
            // p + n / p - n: the pointer-typed side must resolve to the
            // single origin; the other side is an integer offset (a
            // non-pointer-typed expression cannot contribute an origin)
            std::optional<unsigned> found;
            unsigned origins = 0;
            for (const Expr *side : {bin->getLHS(), bin->getRHS()}) {
                const Expr *s = side->IgnoreParenCasts();
                if (!s->getType()->isPointerType()) continue;
                std::optional<unsigned> o;
                if (!parameterDerivedOrigin(s, f, o)) return false;
                if (o) {
                    origins++;
                    found = o;
                }
            }
            if (origins != 1) return false;
            origin = found;
            return true;
        }
        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            // A pointer-typed member access reads a pointer VALUE out of
            // the parameter's storage; its target is a different object
            // (incident #64, value-read family). Array and record members
            // only appear as interior chains (p->arr decay, p->s.arr).
            if (member->getType()->isPointerType()) return false;
            return parameterDerivedOrigin(member->getBase(), f, origin);
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(expr);
            unary && unary->getOpcode() == clang::UO_AddrOf) {
            const Expr *inner = unary->getSubExpr()->IgnoreParenCasts();
            // &p is the parameter object itself, not a borrow of its pointee
            if (parameterIndex(inner, f)) return false;
            // &p->f / &p[i]: the address of a member or element of the
            // parameter's pointee is interior to the parameter's object
            if (const auto *m = dyn_cast<MemberExpr>(inner))
                return parameterDerivedOrigin(m->getBase(), f, origin);
            if (const auto *s = dyn_cast<ArraySubscriptExpr>(inner))
                return parameterDerivedOrigin(s->getBase(), f, origin);
            return false;
        }
        // ArraySubscriptExpr as a value (p[i] on T**), UnaryOperator deref
        // (*pp), CallExpr and every other shape: no origin resolution.
        return false;
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

    static bool addressTaken(const Stmt *stmt, const VarDecl *var) {
        if (!stmt) return false;
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt);
            unary && unary->getOpcode() == clang::UO_AddrOf) {
            const auto *ref = dyn_cast<DeclRefExpr>(unary->getSubExpr()->IgnoreParenCasts());
            if (ref && ref->getDecl() == var) return true;
        }
        for (const Stmt *child : stmt->children()) if (addressTaken(child, var)) return true;
        return false;
    }

    // Milestone #54 (ADR-0028): bounded local-alias resolution. Returns the
    // parameter index whose object the expression unambiguously holds, or
    // nullopt (fail closed). The whitelist: the expression is a plain
    // (paren/cast-stripped) reference to a function-local pointer variable
    // that is not itself a parameter, not a static local, and neither
    // volatile nor atomic; it has an initializer that is exactly one
    // parameter reference (after paren/cast stripping -- no derived,
    // field, conditional, or call-shaped initializers); it is never the
    // left-hand side of an assignment and never has its address taken
    // anywhere in the body; and the aliased parameter is never assigned
    // in the body (so the local holds the parameter's entry value). This
    // mirrors the ADR-0026 single-origin whitelist philosophy: any doubt
    // leaves the pre-rule behavior in place.
    static std::optional<unsigned> aliasedParameterIndex(const Expr *expr, const FunctionDecl &f) {
        expr = expr ? expr->IgnoreParenCasts() : nullptr;
        const auto *ref = expr ? dyn_cast<DeclRefExpr>(expr) : nullptr;
        const auto *var = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
        if (!var || isa<ParmVarDecl>(var) || var->isStaticLocal()) return std::nullopt;
        if (!var->getType()->isPointerType()) return std::nullopt;
        if (var->getType().isVolatileQualified() || var->getType()->isAtomicType())
            return std::nullopt;
        const Stmt *body = f.getBody();
        if (!body || !var->getInit()) return std::nullopt;
        const auto index = parameterIndex(var->getInit(), f);
        if (!index) return std::nullopt;
        if (assignedLater(body, var) || addressTaken(body, var)) return std::nullopt;
        if (assignedLater(body, f.getParamDecl(*index))) return std::nullopt;
        return index;
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

    // ===== ADR-0031 (issue #73), Area R: summary-side origin dataflow =====
    //
    // A per-function, intraprocedural, flow-insensitive, MONOTONE-JOIN
    // origin-set dataflow, used ONLY to resolve the function's summary
    // return effect (param-effect logic and the flow-side analysis are
    // untouched; the flow-level B003 backstop in
    // FlowAnalyzer::handleReturn stays exactly as-is). The lattice per
    // pointer variable v is the powerset of {Param 0..n-1, FRESH} plus a
    // top element Unresolvable (any doubt fails closed to Unknown);
    // join is union and no transfer narrows a set, so
    // `v = p; if (c) v = q; return v;` joins to {0,1} and never yields a
    // last-write-wins singleton (a wrong singleton would let a caller
    // bind the returned borrow to the wrong parameter's object).
    // NULL contributes nothing to any set: it is absorbed by every
    // non-empty origin and never creates a singleton on its own.
    struct OriginSet {
        std::set<unsigned> params;  // parameter indices the value may borrow
        bool fresh = false;         // may be a freshly owned allocation
        bool unresolvable = false;  // top: unproven origin, fail closed

        bool operator==(const OriginSet &other) const {
            return params == other.params && fresh == other.fresh &&
                   unresolvable == other.unresolvable;
        }
    };
    static OriginSet unresolvableOrigin() {
        OriginSet top;
        top.unresolvable = true;
        return top;
    }
    static OriginSet joinOrigin(OriginSet a, const OriginSet &b) {
        if (a.unresolvable) return a;
        if (b.unresolvable) return b;
        a.params.insert(b.params.begin(), b.params.end());
        a.fresh = a.fresh || b.fresh;
        return a;
    }

    // A pointer variable the dataflow tracks: a function-local (including
    // parameters) that is not static, not volatile, and not atomic.
    // Globals and static locals carry state across calls and are
    // unresolvable.
    static bool trackedOriginVar(const VarDecl *var) {
        if (var == nullptr || !var->getType()->isPointerType()) return false;
        if (var->hasGlobalStorage() || var->isStaticLocal()) return false;
        if (var->getType().isVolatileQualified() || var->getType()->isAtomicType())
            return false;
        return true;
    }

    // EXCLUSION (plan risk 2 / tests/p2/undeclared_borrow_return.c):
    // locals initialized by EXPLICIT borrow annotations do not
    // participate. An annotation is a reviewed claim, not
    // machine-verified provenance, so such locals are unresolvable and
    // so is everything assigned from them. All three spellings
    // (cand:borrow, cand:borrow_mut, cand:borrow_shared) are excluded.
    static bool borrowAnnotated(const VarDecl *var) {
        return hasCandAnnotation(var, "cand:borrow") ||
               hasCandAnnotation(var, "cand:borrow_mut") ||
               hasCandAnnotation(var, "cand:borrow_shared");
    }

    // Plan risk 6: `&v` (or `&v.f`, or `&v[i]` with v an ARRAY -- the
    // address of storage inside v itself) appearing ANYWHERE in the body
    // (call arguments, initializers, stores) escapes v's storage, so v's
    // origin is unresolvable. Arrow members (`&v->f`) and subscripts of
    // a pointer (`&v[i]` on a pointer v) address v's POINTEE, not v's
    // storage, and stay whitelist shapes.
    static bool addressTakenOf(const Expr *lvalue, const VarDecl *var) {
        const Expr *cur = lvalue->IgnoreParenCasts();
        while (true) {
            if (const auto *member = dyn_cast<MemberExpr>(cur)) {
                if (member->isArrow()) return false;  // inside the base's pointee
                cur = member->getBase()->IgnoreParenCasts();
                continue;
            }
            if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(cur)) {
                // Subscripting a pointer dereferences it; only an array
                // base (after stripping the implicit array-to-pointer
                // decay) indexes the variable's own storage.
                const Expr *base = subscript->getBase()->IgnoreParenImpCasts();
                if (base == nullptr || !base->getType()->isArrayType()) return false;
                cur = base->IgnoreParenCasts();
                continue;
            }
            break;
        }
        const auto *ref = dyn_cast<DeclRefExpr>(cur);
        return ref != nullptr && ref->getDecl() == var;
    }
    static bool addressTakenAnywhere(const Stmt *stmt, const VarDecl *var) {
        if (stmt == nullptr) return false;
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt);
            unary != nullptr && unary->getOpcode() == clang::UO_AddrOf &&
            addressTakenOf(unary->getSubExpr(), var)) {
            return true;
        }
        for (const Stmt *child : stmt->children())
            if (addressTakenAnywhere(child, var)) return true;
        return false;
    }

    // One transfer site of the fixpoint: join eval(rhs) into var's
    // origin. A `poison` site (compound assignment whose delta mentions
    // a pointer value) joins Unresolvable instead. `v++` / `v--` need no
    // site at all: the pure integer delta 1 preserves the origin.
    struct OriginSite {
        const VarDecl *var;
        const Expr *rhs;
        bool poison;
    };

    static void collectOriginSites(const Stmt *stmt, std::vector<OriginSite> &sites) {
        if (stmt == nullptr) return;
        if (const auto *decl = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *item : decl->decls()) {
                const auto *var = dyn_cast<VarDecl>(item);
                if (var != nullptr && trackedOriginVar(var) && var->getInit() != nullptr)
                    sites.push_back({var, var->getInit(), false});
            }
        } else if (const auto *binary = dyn_cast<BinaryOperator>(stmt);
                   binary != nullptr && binary->isAssignmentOp()) {
            const auto *ref = dyn_cast<DeclRefExpr>(binary->getLHS()->IgnoreParenCasts());
            const auto *var = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
            if (var != nullptr && trackedOriginVar(var)) {
                if (binary->getOpcode() == clang::BO_Assign) {
                    sites.push_back({var, binary->getRHS(), false});
                } else {
                    // `v += e` / `v -= e`: a pure integer delta preserves
                    // the origin (ADR-0031 Area C rule, isPureIntegerDelta);
                    // a pointer-mentioning delta rebinds and poisons.
                    sites.push_back({var, nullptr, !isPureIntegerDelta(binary->getRHS())});
                }
            }
        }
        for (const Stmt *child : stmt->children()) collectOriginSites(child, sites);
    }

    // Evaluates the origin set of an expression under the current
    // (still-growing) origin map. The whitelist mirrors the incident #64
    // resolver (parameterDerivedOrigin); subscript and pointer-member
    // VALUE READS (`w[i]`, `w->f`), `&w`, dereferences, globals, and
    // every unmodelled shape are unresolvable (plan risk 3).
    OriginSet evalOrigin(const Expr *raw, const FunctionDecl &f,
                         const std::map<const VarDecl *, OriginSet> &origins) const {
        const Expr *expr = raw ? raw->IgnoreParenCasts() : nullptr;
        if (expr == nullptr) return unresolvableOrigin();
        if (nullPointerValue(expr)) return {};  // NULL: absorbed, never a singleton
        if (const auto *cond = dyn_cast<ConditionalOperator>(expr))
            return joinOrigin(evalOrigin(cond->getTrueExpr(), f, origins),
                              evalOrigin(cond->getFalseExpr(), f, origins));
        if (const auto *comma = dyn_cast<BinaryOperator>(expr);
            comma != nullptr && comma->isCommaOp()) {
            // `(a, b)`: the last operand is the value.
            const Expr *last = comma->getRHS();
            while (true) {
                const auto *inner = dyn_cast<BinaryOperator>(last->IgnoreParenCasts());
                if (inner == nullptr || !inner->isCommaOp()) break;
                last = inner->getRHS();
            }
            return evalOrigin(last, f, origins);
        }
        if (const auto *bin = dyn_cast<BinaryOperator>(expr);
            bin != nullptr && (bin->getOpcode() == clang::BO_Add ||
                               bin->getOpcode() == clang::BO_Sub) &&
            bin->getType()->isPointerType()) {
            // `w ± e`: the single pointer-typed side contributes its
            // origin; the other side must be a pure integer delta
            // (isPureIntegerDelta -- the Area C rebind boundary, so
            // `w + (q - w)` stays fail-closed).
            const Expr *base = nullptr;
            unsigned pointers = 0;
            for (const Expr *side : {bin->getLHS(), bin->getRHS()}) {
                if (!side->getType()->isPointerType()) continue;
                pointers++;
                base = side;
            }
            if (pointers != 1) return unresolvableOrigin();
            const Expr *delta = base == bin->getLHS() ? bin->getRHS() : bin->getLHS();
            if (!isPureIntegerDelta(delta)) return unresolvableOrigin();
            return evalOrigin(base, f, origins);
        }
        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            // A pointer-typed member access reads a pointer VALUE out of
            // storage (incident #64); array/record members only appear
            // as interior chains (`w->arr` decay, `w->s.arr`).
            if (member->getType()->isPointerType()) return unresolvableOrigin();
            return evalOrigin(member->getBase(), f, origins);
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(expr);
            unary != nullptr && unary->getOpcode() == clang::UO_AddrOf) {
            const Expr *inner = unary->getSubExpr()->IgnoreParenCasts();
            // `&p` is the parameter object itself, not a borrow of its
            // pointee; `&w` is likewise rejected.
            if (parameterIndex(inner, f)) return unresolvableOrigin();
            // `&w->f` / `&w[i]`: interior to the base's pointee.
            if (const auto *m = dyn_cast<MemberExpr>(inner))
                return evalOrigin(m->getBase(), f, origins);
            if (const auto *s = dyn_cast<ArraySubscriptExpr>(inner))
                return evalOrigin(s->getBase(), f, origins);
            return unresolvableOrigin();
        }
        if (const auto *call = dyn_cast<CallExpr>(expr)) {
            // Call transfer: a callee whose (trusted or same-TU
            // body-verified) summary returns BorrowFromArg(k) contributes
            // the JOIN set of its k-th argument (safe over-approximation);
            // an Owned callee contributes FRESH; anything else is
            // unresolvable.
            if (call->getDirectCallee() != nullptr &&
                (call->getDirectCallee()->getNameAsString() == "malloc" ||
                 call->getDirectCallee()->getNameAsString() == "calloc")) {
                OriginSet fresh;
                fresh.fresh = true;
                return fresh;
            }
            const FunctionSummary *callee = old_.find(call->getDirectCallee());
            if (callee == nullptr || callee->conflict ||
                callee->origin == SummaryOrigin::Unknown)
                return unresolvableOrigin();
            if (callee->return_effect == ReturnEffect::Owned) {
                OriginSet fresh;
                fresh.fresh = true;
                return fresh;
            }
            if (callee->return_effect == ReturnEffect::BorrowFromArg &&
                callee->return_borrow_arg && *callee->return_borrow_arg < call->getNumArgs())
                return evalOrigin(call->getArg(*callee->return_borrow_arg), f, origins);
            return unresolvableOrigin();
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            const auto *var = dyn_cast<VarDecl>(ref->getDecl());
            if (var == nullptr || !var->getType()->isPointerType())
                return unresolvableOrigin();
            const bool is_param = isa<ParmVarDecl>(var);
            // Parameters and plain locals participate; globals, statics,
            // volatile/atomic variables, and explicitly borrow-annotated
            // locals (exclusion) are unresolvable.
            if (!is_param && (!trackedOriginVar(var) || borrowAnnotated(var)))
                return unresolvableOrigin();
            // The map entry carries the parameter's seed PLUS every
            // origin joined in by reassignment (`p = r` must join to
            // {0,1}, never keep the entry-value singleton {0}).
            const auto it = origins.find(var);
            if (it != origins.end()) return it->second;
            if (is_param) {
                OriginSet seed;
                seed.params.insert(*parameterIndex(expr, f));
                return seed;
            }
            return OriginSet{};  // local never assigned: bottom
        }
        // ArraySubscriptExpr value reads (`w[i]`, incident #64),
        // dereferences, statement expressions, and every other shape.
        return unresolvableOrigin();
    }

    static void collectPointerLocals(const Stmt *stmt, std::vector<const VarDecl *> &out) {
        if (stmt == nullptr) return;
        if (const auto *decl = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *item : decl->decls()) {
                const auto *var = dyn_cast<VarDecl>(item);
                if (var != nullptr && trackedOriginVar(var)) out.push_back(var);
            }
        }
        for (const Stmt *child : stmt->children()) collectPointerLocals(child, out);
    }

    // Runs the monotone origin dataflow to its fixpoint (joins only grow
    // sets, so iteration terminates) and records the origin set of every
    // pointer-typed ReturnStmt's value for scan's fallback resolution.
    void computeReturnOrigins(const FunctionDecl &f) {
        return_origins_.clear();
        const Stmt *body = f.getBody();
        if (body == nullptr) return;
        std::map<const VarDecl *, OriginSet> origins;
        for (unsigned i = 0; i < f.param_size(); ++i) {
            if (!f.getParamDecl(i)->getType()->isPointerType()) continue;
            OriginSet seed;
            seed.params.insert(i);
            origins[f.getParamDecl(i)] = std::move(seed);
        }
        std::vector<const VarDecl *> locals;
        collectPointerLocals(body, locals);
        for (const VarDecl *var : locals) origins[var];  // bottom until assigned
        // Seeds that fail closed BEFORE the fixpoint so the kill
        // propagates through joins: explicitly borrow-annotated locals
        // (exclusion) and any variable whose storage address escapes
        // anywhere in the body (plan risk 6).
        for (auto &entry : origins) {
            if (entry.second.unresolvable) continue;
            if (!isa<ParmVarDecl>(entry.first) && borrowAnnotated(entry.first))
                entry.second = unresolvableOrigin();
            else if (addressTakenAnywhere(body, entry.first))
                entry.second = unresolvableOrigin();
        }
        std::vector<OriginSite> sites;
        collectOriginSites(body, sites);
        bool changed = true;
        while (changed) {
            changed = false;
            for (const OriginSite &site : sites) {
                // `v += e` with a pure integer delta preserves the origin
                // (rhs == nullptr, no join); a pointer-mentioning delta
                // poisons; every plain assignment joins eval(rhs).
                OriginSet joined = origins[site.var];
                if (site.poison)
                    joined = joinOrigin(joined, unresolvableOrigin());
                else if (site.rhs != nullptr)
                    joined = joinOrigin(joined, evalOrigin(site.rhs, f, origins));
                if (!(joined == origins[site.var])) {
                    origins[site.var] = std::move(joined);
                    changed = true;
                }
            }
        }
        collectReturnOrigins(body, f, origins);
    }

    void collectReturnOrigins(const Stmt *stmt, const FunctionDecl &f,
                              const std::map<const VarDecl *, OriginSet> &origins) {
        if (stmt == nullptr) return;
        if (const auto *ret = dyn_cast<ReturnStmt>(stmt)) {
            if (const Expr *value = ret->getRetValue();
                value != nullptr && value->getType()->isPointerType())
                return_origins_[ret] = evalOrigin(value, f, origins);
        }
        for (const Stmt *child : stmt->children()) collectReturnOrigins(child, f, origins);
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
                    // Incident #62: a pointer parameter that is assigned
                    // anywhere in the body no longer holds its entry value at
                    // the return site, so the syntactically referenced
                    // parameter is not a sound borrow/ownership origin.
                    // Fail closed to Unknown.
                    if (assignedLater(f.getBody(), f.getParamDecl(*borrow))) {
                        effect = ReturnEffect::Unknown;
                        borrow.reset();
                    } else {
                        effect = s.params[*borrow] == ParamEffect::TakeOwnership
                                     ? ReturnEffect::Owned
                                     : ReturnEffect::BorrowFromArg;
                    }
                }
                else if (const auto *call = dyn_cast<CallExpr>(value)) {
                    effect = returnEffect(*call, old_, borrow);
                    if (effect == ReturnEffect::BorrowFromArg && borrow && *borrow < call->getNumArgs()) {
                        const auto mapped = parameterIndex(call->getArg(*borrow), f);
                        if (mapped && assignedLater(f.getBody(), f.getParamDecl(*mapped))) {
                            // Incident #62 (callee-mapping form): the argument
                            // name does not identify the object passed once
                            // that parameter is reassigned in this body.
                            effect = ReturnEffect::Unknown;
                            borrow.reset();
                        }
                        else if (mapped) borrow = mapped;
                        else effect = ReturnEffect::Unknown;
                    }
                }
                else if (const auto *ref = dyn_cast<DeclRefExpr>(value)) {
                    const auto *var = dyn_cast<VarDecl>(ref->getDecl());
                    if (var && ownedLocal(f.getBody(), var, old_) && !assignedLater(f.getBody(), var)) effect = ReturnEffect::Owned;
                }
                if (effect == ReturnEffect::Unknown) {
                    // ADR-0031 (issue #73, Area R): fall back to the
                    // summary-side origin dataflow (computeReturnOrigins).
                    // Only a machine-verified singleton resolves: {j} or
                    // {j, NULL} -> BorrowFromArg(j), {FRESH} -> Owned;
                    // every other set (empty, multi-parameter, mixed,
                    // unresolvable) keeps today's fail-closed Unknown.
                    // All return sites still agree through combineReturn.
                    const auto origin = return_origins_.find(ret);
                    if (origin != return_origins_.end() && !origin->second.unresolvable) {
                        if (origin->second.fresh && origin->second.params.empty()) {
                            effect = ReturnEffect::Owned;
                            borrow.reset();
                        } else if (!origin->second.fresh && origin->second.params.size() == 1) {
                            effect = ReturnEffect::BorrowFromArg;
                            borrow = *origin->second.params.begin();
                        }
                    }
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
                if (currents.empty()) {
                    // Milestone #54 (ADR-0028): bounded local-alias
                    // resolution, restricted to consuming effects. When the
                    // argument is a local that unambiguously holds one
                    // parameter's entry value (single-assignment
                    // declaration-init alias, never reassigned, address
                    // never taken, parameter never reassigned), a free (or
                    // a callee that destroys/consumes that argument)
                    // attributes its effect to that parameter, exactly as
                    // the direct `free(p)` form does. Everything else --
                    // unknown callees, borrowing callees, any ambiguous
                    // alias shape -- keeps the pre-rule behavior
                    // (fail-closed).
                    const bool consuming =
                        (name == "free" && call->getNumArgs() == 1) ||
                        (callee && argument < callee->params.size() &&
                         (callee->params[argument] == ParamEffect::Destroy ||
                          callee->params[argument] == ParamEffect::TakeOwnership));
                    if (consuming) {
                        if (const auto aliased = aliasedParameterIndex(call->getArg(argument), f))
                            currents.push_back(*aliased);
                    }
                    if (currents.empty()) continue;
                }
                if (currents.size() > 1) {
                    for (unsigned current : currents) s.params[current] = ParamEffect::Unknown;
                    continue;
                }
                const unsigned current = currents.front();
                ParamEffect effect = ParamEffect::Unknown;
                if (name == "free" && call->getNumArgs() == 1) effect = ParamEffect::Destroy;
                else if (callee && argument < callee->params.size()) effect = callee->params[argument];
                if (conditional) {
                    if (effect == ParamEffect::Borrow || effect == ParamEffect::None) {
                        // Milestone #61 (ADR-0027): a parameter that is at
                        // most borrowed (or untouched) on every path is at
                        // most borrowed overall. Conditional direct
                        // member/deref/subscript borrows are already kept
                        // unconditionally; this unifies call-mediated
                        // borrows with that treatment. Conditional
                        // consume/destroy and unresolved-callee effects
                        // still fail closed to Unknown below.
                    } else {
                        effect = ParamEffect::Unknown;
                    }
                }
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
    // ADR-0031 (issue #73), Area R: origin set of every pointer-typed
    // ReturnStmt's value, computed by computeReturnOrigins before scan.
    std::map<const ReturnStmt *, OriginSet> return_origins_;
};

class TranslationUnitVisitor : public RecursiveASTVisitor<TranslationUnitVisitor> {
public:
    TranslationUnitVisitor(ASTContext &context, Collector &collector, bool cand1_profile,
                           bool pointer_output_contracts)
        : context_(context), collector_(collector), cand1_profile_(cand1_profile),
          pointer_output_contracts_(pointer_output_contracts) {}

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
            const bool stable = next == summaries_;
            summaries_ = std::move(next);
            if (stable) break;
        }
        // Reviewed declaration-site annotations seed external summaries with
        // contract-equivalent trust (ADR-0029). They are applied before the
        // contract loader so the loader can keep an agreeing contract's
        // provenance and fail closed on disagreement, and before the second
        // fixed point so the seeded summaries survive its copy-initialization.
        seedReviewedAnnotations();
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
            const bool stable = next == summaries_;
            summaries_ = std::move(next);
            if (stable) break;
        }
    }

    void loadContracts() {
        if (ContractFile.empty()) return;
        std::map<std::string, ContractSummary> parsed;
        // #41: produces_out_owner and its output: block parse only under
        // the pointer-output contract profile; a bundle using the v2
        // vocabulary without the feature is an invalid trusted contract
        // (fail-closed exit 2, never a silent degradation).
        if (!parseSymbolFactsFile(ContractFile.getValue(),
                                  "schema: cand.api-contract/v1", parsed,
                                  pointer_output_contracts_)) {
            collector_.noteContractError();
            return;
        }
        for (auto &entry : parsed)
            applyContractSymbol(entry.first, entry.second);
    }

    // A body-less declaration's gathered annotation facts (milestone #39).
    struct DeclaredAnnotation {
        const FunctionDecl *decl = nullptr;      // first body-less declaration
        bool any_annotation = false;
        bool conflicting = false;                // contradictory facts across redecls
        bool eligible = true;                    // shape / K&R / realloc guards
        std::optional<ReturnEffect> return_effect;
        std::optional<unsigned> return_borrow_arg;
        std::vector<ParamEffect> params;         // per-parameter, None default
    };

    // Collects every body-less function declaration that carries C&
    // ownership annotations. Declarations whose redeclaration chain has a
    // body anywhere in the TU are skipped: visible bodies always win and
    // keep following the ordinary SummaryBuilder path (issue #39
    // constraint). Clang inherits annotations forward across redecls, so
    // scanning every declaration and unioning yields the complete fact set;
    // contradictory unions are marked conflicting and never seed.
    std::map<std::string, DeclaredAnnotation> gatherAnnotatedDeclarations() {
        std::map<std::string, DeclaredAnnotation> by_name;
        if (context_.getLangOpts().CPlusPlus) return by_name; // C-only profile (SPEC-0010)
        for (const clang::Decl *item : context_.getTranslationUnitDecl()->decls()) {
            const auto *function = dyn_cast<FunctionDecl>(item);
            if (function == nullptr || function->hasBody()) continue;
            const std::string name = function->getNameAsString();
            DeclaredAnnotation &entry = by_name[name];
            if (entry.decl == nullptr) {
                entry.decl = function;
                entry.params.assign(function->param_size(), ParamEffect::None);
            }
            const bool returns_own = hasCandAnnotation(function, "cand:returns_own");
            const std::optional<unsigned> borrow_from = borrowReturnParameter(function);
            if (returns_own) entry.any_annotation = true;
            if (borrow_from) entry.any_annotation = true;
            if (returns_own && borrow_from) {
                entry.conflicting = true;
            } else if (returns_own) {
                entry.return_effect = ReturnEffect::Owned;
            } else if (borrow_from) {
                if (*borrow_from >= function->param_size()) {
                    entry.conflicting = true; // out-of-range borrow origin
                } else {
                    entry.return_effect = ReturnEffect::BorrowFromArg;
                    entry.return_borrow_arg = *borrow_from;
                }
            }
            for (unsigned i = 0; i < function->param_size(); ++i) {
                const ParmVarDecl *param = function->getParamDecl(i);
                std::optional<ParamEffect> effect;
                if (hasCandAnnotation(param, "cand:takes"))
                    effect = ParamEffect::TakeOwnership;
                else if (hasCandAnnotation(param, "cand:destroys"))
                    effect = ParamEffect::Destroy;
                else if (hasCandAnnotation(param, "cand:borrow") ||
                         hasCandAnnotation(param, "cand:borrow_shared") ||
                         hasCandAnnotation(param, "cand:borrow_mut"))
                    effect = ParamEffect::Borrow;
                if (!effect) continue;
                entry.any_annotation = true;
                if (i >= entry.params.size()) continue; // incompatible redecl
                if (entry.params[i] != ParamEffect::None && entry.params[i] != *effect)
                    entry.conflicting = true;
                else
                    entry.params[i] = *effect;
            }
        }
        for (auto &item : by_name) {
            const std::string &name = item.first;
            DeclaredAnnotation &entry = item.second;
            if (!entry.any_annotation) continue;
            const FunctionDecl *decl = entry.decl;
            // realloc's conditional semantics stay outside the vocabulary
            // (mirrors the contract loader's guard).
            if (name == "realloc") { entry.eligible = false; continue; }
            // Unspecified-parameter (K&R) declarations carry no reviewable
            // parameter facts.
            if (!decl->getType()->isFunctionProtoType()) { entry.eligible = false; continue; }
            if (isPointerToPointerShape(decl->getReturnType())) { entry.eligible = false; continue; }
            for (const ParmVarDecl *param : decl->parameters()) {
                if (isPointerToPointerShape(param->getType())) {
                    entry.eligible = false; // #41 scope guard
                    break;
                }
            }
        }
        return by_name;
    }

    // True when the gathered declaration facts exactly equal the reviewed
    // manifest facts (unspecified manifest entries mean "no ownership
    // effect", matching the annotation side's default).
    static bool annotationFactsMatch(const DeclaredAnnotation &entry,
                                     const ContractSummary &reviewed) {
        const std::optional<ReturnEffect> decl_return = entry.return_effect;
        const std::optional<ReturnEffect> manifest_return = reviewed.return_effect;
        if (decl_return.has_value() != manifest_return.has_value()) return false;
        if (decl_return) {
            if (*decl_return != *manifest_return) return false;
            if (*decl_return == ReturnEffect::BorrowFromArg &&
                entry.return_borrow_arg != reviewed.return_borrow_arg) return false;
        }
        const unsigned count = entry.params.size();
        for (unsigned i = 0; i < count; ++i) {
            const std::optional<ParamEffect> manifest_effect =
                i < reviewed.params.size() ? reviewed.params[i] : std::nullopt;
            if (!manifest_effect || *manifest_effect == ParamEffect::None) {
                if (entry.params[i] != ParamEffect::None) return false;
            } else if (*manifest_effect != entry.params[i]) {
                return false;
            }
        }
        return true;
    }

    // Seeds external summaries from reviewed declaration-site annotations
    // (ADR-0029). A body-less annotated declaration seeds a summary only
    // when a reviewed manifest records exactly the same facts; anything
    // else keeps today's fail-closed behavior, with a distinct obligation
    // kind so adopters know the annotation still needs review.
    void seedReviewedAnnotations() {
        std::map<std::string, DeclaredAnnotation> annotated = gatherAnnotatedDeclarations();
        if (annotated.empty()) return;
        std::map<std::string, ContractSummary> manifest;
        bool have_manifest = false;
        if (!AnnotationReviewPath.empty()) {
            have_manifest = parseSymbolFactsFile(AnnotationReviewPath.getValue(),
                                                 "schema: cand.annotation-review/v1", manifest);
            if (!have_manifest) {
                collector_.noteContractError();
                return;
            }
        }
        for (auto &item : annotated) {
            const std::string &name = item.first;
            DeclaredAnnotation &entry = item.second;
            if (!entry.any_annotation) continue;
            if (entry.conflicting) {
                annotation_review_.conflicting.insert(name);
                continue;
            }
            if (!entry.eligible) continue; // guarded shapes keep today's kinds
            if (!have_manifest) {
                annotation_review_.unreviewed.insert(name);
                continue;
            }
            const auto reviewed = manifest.find(name);
            if (reviewed == manifest.end()) {
                annotation_review_.unreviewed.insert(name);
                continue;
            }
            if (reviewed->second.params.size() > entry.decl->param_size() ||
                (reviewed->second.return_borrow_arg &&
                 *reviewed->second.return_borrow_arg >= entry.decl->param_size())) {
                collector_.noteContractError(); // malformed review entry
                return;
            }
            if (!annotationFactsMatch(entry, reviewed->second)) {
                annotation_review_.unreviewed.insert(name);
                continue;
            }
            FunctionSummary summary;
            summary.function = entry.decl;
            summary.origin = SummaryOrigin::AnnotationTrusted;
            summary.return_effect = entry.return_effect.value_or(
                entry.decl->getReturnType()->isPointerType() ? ReturnEffect::Unknown
                                                             : ReturnEffect::None);
            summary.return_borrow_arg = entry.return_borrow_arg;
            summary.params = entry.params;
            summaries_.set(name, std::move(summary));
        }
    }

    // True when a reviewed contract's explicitly stated facts agree with a
// reviewed-annotation summary. Contract silence (unspecified return or
// parameter) is compatible; explicit facts must agree exactly, and an
// explicit contract fact never contradicts an unannotated parameter
// (fail closed on disagreement, ADR-0029).
bool annotationSummaryMatchesContract(const FunctionSummary &annotation,
                                       const ContractSummary &contract) {
    if (contract.return_effect && *contract.return_effect != ReturnEffect::Unknown) {
        if (annotation.return_effect != *contract.return_effect) return false;
        if (*contract.return_effect == ReturnEffect::BorrowFromArg &&
            annotation.return_borrow_arg != contract.return_borrow_arg) return false;
    }
    for (unsigned i = 0; i < contract.params.size(); ++i) {
        if (!contract.params[i] || *contract.params[i] == ParamEffect::Unknown) continue;
        const ParamEffect annotated =
            i < annotation.params.size() ? annotation.params[i] : ParamEffect::Unknown;
        if (annotated != *contract.params[i]) return false;
    }
    return true;
}

// Parses a contract-format facts file (the reviewed contract bundle or
    // the reviewed declaration-annotation manifest, which records the same
    // fact vocabulary). Pure parse: no store interaction, no declaration
    // lookup. Returns false on any malformed input; the caller reports the
    // contract error.
    bool parseSymbolFactsFile(const std::string &path, const llvm::StringLiteral schema_line,
                              std::map<std::string, ContractSummary> &out,
                              bool allow_out_owner = false) {
        std::ifstream input(path);
        if (!input) return false;
        std::string line, symbol;
        std::set<std::string> seen_symbols;
        std::set<unsigned> seen_param_indices, seen_param_effects;
        std::optional<unsigned> last_index;
        std::optional<ReturnEffect> return_seen;
        bool borrow_index_seen = false;
        ContractSummary contract;
        bool in_symbol = false;
        bool schema_seen = false, name_seen = false, version_seen = false;
        bool symbols_seen = false, kind_seen = false;
        bool in_platform = false, in_notes_block = false;
        // #41 out-owner parse state. `await_output` enforces that the
        // mandatory output: block directly follows effect:
        // produces_out_owner; the block accepts exactly write/success/
        // nullable at indent 10 with success required iff write:
        // on_success. A bare nullable: at the legacy indent-6 position is
        // hard-rejected for symbols that carry a produces_out_owner param.
        bool await_output = false, in_output = false;
        bool out_write_seen = false, out_success_seen = false, out_nullable_seen = false;
        bool legacy_nullable_seen = false;
        std::optional<OutOwnerContract> parsed_out;
        const auto parseUnsigned = [](const std::string &text, unsigned &value) {
            std::size_t end = 0;
            try {
                const unsigned long parsed = std::stoul(text, &end);
                if (end != text.size() || parsed > std::numeric_limits<unsigned>::max()) return false;
                value = static_cast<unsigned>(parsed);
                return true;
            } catch (...) { return false; }
        };
        const auto finish = [&]() {
            if (!in_symbol || symbol.empty()) return true;
            if (!kind_seen) return false;
            if (seen_param_indices.size() != seen_param_effects.size()) return false;
            if (contract.return_effect && *contract.return_effect == ReturnEffect::BorrowFromArg &&
                !contract.return_borrow_arg) return false;
            if (contract.return_effect && *contract.return_effect != ReturnEffect::BorrowFromArg &&
                contract.return_borrow_arg) return false;
            if (await_output) return false;
            if (in_output) {
                // EOF directly inside the output: block: the block is
                // complete only under the same rules as the dedent close
                // (write mandatory; success required iff write:
                // on_success). An EOF right after effect:
                // produces_out_owner (await_output) is still a
                // missing-output error.
                if (!out_write_seen) return false;
                if (parsed_out && parsed_out->write_on_success != out_success_seen) return false;
                in_output = false;
            }
            if (parsed_out) {
                if (!out_write_seen) return false;
                if (parsed_out->write_on_success != out_success_seen) return false;
                if (legacy_nullable_seen) return false;
                contract.out_owner = parsed_out;
            }
            out[symbol] = contract;
            return true;
        };
        while (std::getline(input, line)) {
            const auto trim = [](std::string s) { const auto a = s.find_first_not_of(" \t"); const auto b = s.find_last_not_of(" \t\r"); return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1); };
            const std::size_t indent = line.find_first_not_of(" \t");
            if (indent == std::string::npos) continue;
            if (line.substr(0, indent).find('\t') != std::string::npos) { return false; }
            std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            if (in_notes_block && indent >= 6) continue;
            in_notes_block = false;
            // #41: the output: block must directly follow effect:
            // produces_out_owner and accepts exactly write/success/nullable
            // at indent 10; success is required iff write: on_success.
            if (await_output) {
                if (indent != 8 || t != "output:") { return false; }
                await_output = false;
                in_output = true;
                continue;
            }
            if (in_output) {
                if (indent == 10) {
                    if (t.rfind("write:", 0) == 0) {
                        const std::string v = trim(t.substr(6));
                        if (out_write_seen || (v != "always" && v != "on_success")) return false;
                        out_write_seen = true;
                        if (parsed_out) parsed_out->write_on_success = v == "on_success";
                    } else if (t.rfind("success:", 0) == 0) {
                        const std::string v = trim(t.substr(8));
                        if (out_success_seen || (v != "zero" && v != "nonzero")) return false;
                        out_success_seen = true;
                        if (parsed_out) parsed_out->success_nonzero = v == "nonzero";
                    } else if (t.rfind("nullable:", 0) == 0) {
                        const std::string v = trim(t.substr(9));
                        if (out_nullable_seen || (v != "true" && v != "false")) return false;
                        out_nullable_seen = true;
                        if (parsed_out) parsed_out->nullable = v == "true";
                    } else { return false; }
                    continue;
                }
                if (indent > 8) { return false; }
                if (!out_write_seen) return false;
                if (parsed_out && parsed_out->write_on_success != out_success_seen) return false;
                in_output = false;
            }
            if (!in_symbol) {
                if (t.rfind("schema:", 0) == 0) {
                    if (indent != 0 || schema_seen || t != schema_line) { return false; }
                    schema_seen = true;
                    continue;
                }
                if (t.rfind("name:", 0) == 0) {
                    const std::string value = trim(t.substr(5));
                    if (indent != 0 || name_seen || value.empty()) { return false; }
                    name_seen = true;
                    continue;
                }
                if (t.rfind("version:", 0) == 0) {
                    const std::string value = trim(t.substr(8));
                    if (indent != 0 || version_seen || value.empty()) { return false; }
                    version_seen = true;
                    continue;
                }
                if (t == "symbols:") {
                    if (indent != 0 || symbols_seen) { return false; }
                    symbols_seen = true;
                    in_platform = false;
                    continue;
                }
                if (t.rfind("- symbol:", 0) == 0) {
                    if (indent != 2 || !symbols_seen || !schema_seen || !name_seen || !version_seen) { return false; }
                    symbol = trim(t.substr(t.find(':') + 1));
                    in_symbol = !symbol.empty();
                    contract = ContractSummary{};
                    last_index.reset(); return_seen.reset(); kind_seen = false;
                    seen_param_indices.clear(); seen_param_effects.clear(); borrow_index_seen = false;
                    await_output = in_output = false;
                    out_write_seen = out_success_seen = out_nullable_seen = false;
                    legacy_nullable_seen = false;
                    parsed_out.reset();
                    if (!in_symbol || !seen_symbols.insert(symbol).second) { return false; }
                    continue;
                }
                if (t == "platform:") { if (indent != 0) { return false; } in_platform = true; continue; }
                if (in_platform && (t.rfind("os:", 0) == 0 || t.rfind("libc:", 0) == 0)) {
                    const std::string value = trim(t.substr(t.find(':') + 1));
                    if (indent != 2 || value.size() < 2 || value.front() != '[' || value.back() != ']') { return false; }
                    continue;
                }
                if (t.rfind("provenance:", 0) == 0) {
                    const std::string value = trim(t.substr(11));
                    if (indent != 0 || value != "{}") { return false; }
                    continue;
                }
                return false;
            }
            if (t.rfind("- symbol:", 0) == 0) {
                if (indent != 2) { return false; }
                if (in_symbol && !finish()) { return false; }
                symbol = trim(t.substr(t.find(':') + 1)); in_symbol = !symbol.empty(); contract = ContractSummary{}; last_index.reset(); return_seen.reset();
                seen_param_indices.clear(); seen_param_effects.clear(); borrow_index_seen = false;
                kind_seen = false;
                await_output = in_output = false;
                out_write_seen = out_success_seen = out_nullable_seen = false;
                legacy_nullable_seen = false;
                parsed_out.reset();
                if (!in_symbol) { return false; }
                if (!seen_symbols.insert(symbol).second) { return false; }
                continue;
            }
            if (t.rfind("ownership:", 0) == 0) {
                if (indent != 6) { return false; }
                std::string v = trim(t.substr(10));
                ReturnEffect effect;
                if (v == "owned") effect = ReturnEffect::Owned;
                else if (v == "borrowed") effect = ReturnEffect::BorrowFromArg;
                else if (v == "none") effect = ReturnEffect::None;
                else if (v == "unknown") effect = ReturnEffect::Unknown;
                else { return false; }
                if (return_seen) { return false; }
                return_seen = effect; contract.return_effect = effect;
            } else if (t.rfind("from_param:", 0) == 0) {
                if (indent != 8) { return false; }
                unsigned index;
                if (borrow_index_seen || !parseUnsigned(trim(t.substr(11)), index)) { return false; }
                borrow_index_seen = true;
                contract.return_borrow_arg = index;
            } else if (t.rfind("index:", 0) == 0 || t.rfind("- index:", 0) == 0) {
                if (indent != 6) { return false; }
                unsigned index;
                const std::size_t colon = t.find(':');
                if (!parseUnsigned(trim(t.substr(colon + 1)), index)) { return false; }
                if (!seen_param_indices.insert(index).second) { return false; }
                if (contract.params.size() <= index) contract.params.resize(index + 1);
                last_index = index;
                contract.params[index].reset();
            } else if (t.rfind("effect:", 0) == 0) {
                if (indent != 8) { return false; }
                if (!last_index || *last_index >= contract.params.size() ||
                    !seen_param_effects.insert(*last_index).second) { return false; }
                std::string v = trim(t.substr(7));
                ParamEffect effect;
                if (v == "borrow" || v == "borrow_shared") effect = ParamEffect::Borrow;
                else if (v == "consumes") effect = ParamEffect::TakeOwnership;
                else if (v == "destroys") effect = ParamEffect::Destroy;
                else if (v == "no_ownership_effect") effect = ParamEffect::None;
                else if (v == "unknown") effect = ParamEffect::Unknown;
                else if (v == "produces_out_owner") {
                    // #41: profile-gated vocabulary; the mandatory output:
                    // block must follow immediately and at most one
                    // out-owner parameter may appear per contract.
                    if (!allow_out_owner || parsed_out) { return false; }
                    OutOwnerContract owner;
                    owner.param = *last_index;
                    parsed_out = owner;
                    contract.params[*last_index].reset();
                    await_output = true;
                    continue;
                }
                else { return false; }
                contract.params[*last_index] = effect;
            } else if (t == "kind: function") {
                if (indent != 4) { return false; }
                if (kind_seen) { return false; }
                kind_seen = true;
            } else if (t == "returns:" || t == "params:") {
                if (indent != 4) { return false; }
            } else if (t == "lifetime:") {
                if (indent != 6) { return false; }
            } else if (t.rfind("borrow_kind:", 0) == 0) {
                if (indent != 6 || trim(t.substr(12)) != "shared") {
                    return false;
                }
            } else if (t == "conditional_effects:" || t == "callbacks:") {
                if (indent != 4) { return false; }
            } else if (t.rfind("notes:", 0) == 0) {
                if (indent != 4) { return false; }
                const std::string value = trim(t.substr(6));
                in_notes_block = value == ">-" || value == ">" || value == "|" || value == "|-" || value == "|+";
            } else if (t.rfind("allocation_family:", 0) == 0) {
                if (indent != 6 && indent != 8) { return false; }
            } else if (t.rfind("nullable:", 0) == 0) {
                if (indent != 6) { return false; }
                // #41: the legacy indent-6 nullable: key stays
                // accepted-and-ignored for non-produces symbols but is
                // hard-rejected for symbols with a produces_out_owner
                // parameter (the output nullability lives in output:).
                legacy_nullable_seen = true;
                continue;
            } else if (t.find(':') != std::string::npos) {
                return false;
            } else {
                return false;
            }
        }
        if (!schema_seen || !name_seen || !version_seen || !symbols_seen || !finish())
            return false;
        return true;
    }
    // Applies one reviewed contract symbol: validates it against the TU
    // declaration, builds the trusted external summary, and merges it with
    // any body-derived or reviewed-annotation summary. Preserved from the
    // pre-#39 contract loader; order across symbols is irrelevant because
    // each symbol touches only its own store entry.
    void applyContractSymbol(const std::string &symbol, ContractSummary &contract) {
        const FunctionDecl *decl = nullptr;
        for (const clang::Decl *item : context_.getTranslationUnitDecl()->decls()) {
            const auto *candidate = dyn_cast<FunctionDecl>(item);
            if (candidate && candidate->getNameAsString() == symbol) { decl = candidate; break; }
        }
        if (decl && (contract.params.size() > decl->param_size() ||
                     (contract.return_borrow_arg && *contract.return_borrow_arg >= decl->param_size()))) {
            collector_.noteContractError();
            return;
        }
        // #41: produces_out_owner application guards. The declared
        // parameter type must be a pointer to a single-level pointer
        // (T **/void **); T ***, function-pointer, and array shapes are
        // invalid trusted contracts. A symbol with no visible TU
        // declaration has no type evidence and never receives the effect
        // (fail closed); a symbol with a visible same-TU body never
        // receives it either (contract-body conflict, fail closed).
        if (contract.out_owner) {
            if (!decl) {
                contract.out_owner.reset();
            } else if (contract.out_owner->param >= decl->param_size()) {
                collector_.noteContractError();
                return;
            } else {
                const clang::QualType param_type =
                    decl->getParamDecl(contract.out_owner->param)->getType().getCanonicalType();
                bool single_level_slot = false;
                if (const auto *outer = param_type->getAs<clang::PointerType>()) {
                    if (const auto *inner = outer->getPointeeType()->getAs<clang::PointerType>()) {
                        const clang::QualType value = inner->getPointeeType();
                        single_level_slot = !value->isPointerType() && !value->isArrayType() &&
                                            !value->isFunctionType();
                    }
                }
                if (!single_level_slot) {
                    collector_.noteContractError();
                    return;
                }
                if (decl->hasBody()) contract.out_owner.reset();
            }
        }
        FunctionSummary external;
        external.function = decl;
        external.origin = SummaryOrigin::ExternalTrusted;
        external.return_effect = contract.return_effect.value_or(
            decl && decl->getReturnType()->isPointerType() ? ReturnEffect::Unknown : ReturnEffect::None);
        external.return_borrow_arg = contract.return_borrow_arg;
        external.out_owner = contract.out_owner;
        // The produces parameter itself is modelled by the out-owner
        // machinery, not an unknown effect: an accepted produce must not
        // report its own out slot as an unknown tracked-pointer argument.
        if (external.out_owner)
            external.params[external.out_owner->param] = ParamEffect::None;
        const unsigned parameter_count = decl ? decl->param_size() :
            static_cast<unsigned>(contract.params.size());
        external.params.assign(parameter_count, ParamEffect::Unknown);
        for (unsigned i = 0; i < contract.params.size(); ++i) {
            if (contract.params[i]) external.params[i] = *contract.params[i];
        }
        // realloc's success/failure and old-object lifetime are conditional
        // and cannot be represented by P0.4's simple effects.
        if (symbol == "realloc") {
            external.return_effect = ReturnEffect::Unknown;
            external.return_borrow_arg.reset();
            std::fill(external.params.begin(), external.params.end(), ParamEffect::Unknown);
        }
        const FunctionSummary *body = summaries_.find(symbol);
        if (body && body->origin == SummaryOrigin::BodyVerified) {
            FunctionSummary conflict = *body;
            const bool has_annotation = decl &&
                (hasCandAnnotation(decl, "cand:returns_own") ||
                 borrowReturnParameter(decl).has_value() ||
                 std::any_of(decl->param_begin(), decl->param_end(), [](const ParmVarDecl *param) {
                     return hasCandAnnotation(param, "cand:takes") ||
                            hasCandAnnotation(param, "cand:destroys") ||
                            hasCandAnnotation(param, "cand:borrow") ||
                            hasCandAnnotation(param, "cand:borrow_shared") ||
                            hasCandAnnotation(param, "cand:borrow_mut");
                 }));
            const auto bodyReason = [&](const std::string &fact) {
                if (has_annotation)
                    return std::string("annotation/body mismatch");
                if (body->conflict)
                    return std::string("conditional/unrepresentable body behavior");
                if (fact == "unknown") return std::string("unknown body effect");
                return std::string("conditional/unrepresentable body behavior");
            };
            const auto addConflict = [&](const std::string &reason,
                                         std::optional<unsigned> parameter,
                                         const std::string &contract_fact,
                                         const std::string &body_fact) {
                conflict.conflicts.push_back(
                    {reason, parameter, contract_fact, body_fact});
            };
            if (contract.return_effect && *contract.return_effect != ReturnEffect::Unknown) {
                const bool borrow_match = *contract.return_effect != ReturnEffect::BorrowFromArg ||
                    (body->return_effect == ReturnEffect::BorrowFromArg &&
                     body->return_borrow_arg == contract.return_borrow_arg);
                if (body->return_effect == ReturnEffect::Unknown) {
                    addConflict(bodyReason("unknown"), std::nullopt,
                                returnEffectName(*contract.return_effect), "unknown");
                } else if (!borrow_match) {
                    addConflict("return borrow-origin mismatch", std::nullopt,
                                returnEffectName(*contract.return_effect),
                                returnEffectName(body->return_effect));
                } else if (body->return_effect != *contract.return_effect) {
                    addConflict(*contract.return_effect == ReturnEffect::None
                                    ? "explicit no-effect mismatch"
                                    : "return ownership mismatch",
                                std::nullopt, returnEffectName(*contract.return_effect),
                                returnEffectName(body->return_effect));
                }
            }
            for (unsigned i = 0; i < contract.params.size(); ++i) {
                if (!contract.params[i] || *contract.params[i] == ParamEffect::Unknown) continue;
                const ParamEffect body_effect = i < body->params.size()
                    ? body->params[i] : ParamEffect::Unknown;
                if (body_effect == ParamEffect::Unknown) {
                    addConflict(bodyReason("unknown"), i,
                                paramEffectName(*contract.params[i]), "unknown");
                } else if (body_effect != *contract.params[i]) {
                    addConflict(*contract.params[i] == ParamEffect::None
                                    ? "explicit no-effect mismatch"
                                    : "param effect mismatch",
                                i, paramEffectName(*contract.params[i]),
                                paramEffectName(body_effect));
                }
            }
            if (!conflict.conflicts.empty()) {
                conflict.conflict = true;
                summaries_.set(symbol, std::move(conflict));
            }
        } else if (body && body->origin == SummaryOrigin::AnnotationTrusted) {
            // Reviewed declaration annotations and reviewed contracts are
            // both trusted sources; they must agree exactly. On agreement
            // the contract keeps its provenance; on disagreement the symbol
            // fails closed (ADR-0029).
            if (annotationSummaryMatchesContract(*body, contract)) {
                summaries_.set(symbol, std::move(external));
            } else {
                FunctionSummary conflict;
                conflict.function = decl;
                conflict.origin = SummaryOrigin::ExternalTrusted;
                conflict.conflict = true;
                conflict.return_effect = ReturnEffect::Unknown;
                const unsigned parameter_count = decl ? decl->param_size() :
                    static_cast<unsigned>(contract.params.size());
                conflict.params.assign(parameter_count, ParamEffect::Unknown);
                conflict.conflicts.push_back({"annotation/contract mismatch", std::nullopt,
                                              contractFactsString(contract),
                                              annotationSummaryFactsString(*body)});
                summaries_.set(symbol, std::move(conflict));
            }
        } else if (!body || body->origin == SummaryOrigin::Unknown) {
            summaries_.set(symbol, std::move(external));
        }
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
        FlowAnalyzer analyzer(context_, collector_, summaries_, cand1_profile_, annotation_review_, pointer_output_contracts_);
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
        FlowAnalyzer analyzer(context_, collector_, summaries_, cand1_profile_, annotation_review_, pointer_output_contracts_);
        analyzer.analyzeGlobal(*var);
        return true;
    }

private:
    ASTContext &context_;
    Collector &collector_;
    SummaryStore summaries_;
    AnnotationReviewFacts annotation_review_;
    bool cand1_profile_ = false;
    bool pointer_output_contracts_ = false;
};

class CandConsumer : public ASTConsumer {
public:
    CandConsumer(ASTContext &context, Collector &collector, bool cand1_profile,
                 bool pointer_output_contracts)
        : visitor_(context, collector, cand1_profile, pointer_output_contracts),
          collector_(collector) {}

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
    CandAction(Collector &collector, bool cand1_profile, bool pointer_output_contracts)
        : collector_(collector), cand1_profile_(cand1_profile),
          pointer_output_contracts_(pointer_output_contracts) {}

    std::unique_ptr<ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler,
                                                   llvm::StringRef) override {
        return std::make_unique<CandConsumer>(compiler.getASTContext(), collector_,
                                               cand1_profile_, pointer_output_contracts_);
    }

private:
    Collector &collector_;
    bool cand1_profile_ = false;
    bool pointer_output_contracts_ = false;
};

class CandActionFactory : public clang::tooling::FrontendActionFactory {
public:
    CandActionFactory(Collector &collector, bool cand1_profile, bool pointer_output_contracts)
        : collector_(collector), cand1_profile_(cand1_profile),
          pointer_output_contracts_(pointer_output_contracts) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<CandAction>(collector_, cand1_profile_,
                                            pointer_output_contracts_);
    }

private:
    Collector &collector_;
    bool cand1_profile_ = false;
    bool pointer_output_contracts_ = false;
};

struct AgentPolicyState {
    cand::Policy policy;
    cand::PolicyDiff delta;
    bool policy_failed = false;
    bool review_required = false;
    std::vector<std::string> policy_errors;
    std::vector<std::tuple<std::string, std::string, std::string>> contract_inputs;
    std::vector<std::tuple<std::string, std::string, std::string>> annotation_review_inputs;
    unsigned unsafe_boundaries = 0;
    unsigned suppressions = 0;
    bool toolchain_supported = true;
};

// The only C&1 success authority. A semantic pass without trusted bindings is
// deliberately not a C&1 pass; it is an ordinary analyzer result.
bool canEmitCand1Pass(const Collector &collector, const AgentPolicyState &state,
                      bool evidence_bound) {
    if (collector.hasFindings() || collector.hasUnsupported() ||
        collector.hasFrontendError() || collector.hasContractError() ||
        collector.unsupportedTransportCount() != 0) return false;
    // #41 [R4/F9]: the pointer-output rule set is cand1/v1.1-draft; it is
    // a measurement profile, never a C&1 pass authority.
    if (collector.pointerOutputContracts()) return false;
    if (!evidence_bound || state.policy_failed || state.review_required ||
        state.delta.weakened || state.delta.review_required) return false;
    if (!state.toolchain_supported) return false;
    if (state.policy.profile != "generated" || state.policy.safety_level != "cand1") return false;
    if (state.policy.scope_files.empty() || state.policy.sha256.empty()) return false;
    for (const auto &contract : state.contract_inputs) {
        const std::string &trust = std::get<2>(contract);
        if (trust != "builtin" && trust != "verified" && trust != "reviewed") return false;
    }
    for (const auto &review : state.annotation_review_inputs) {
        const std::string &trust = std::get<2>(review);
        if (trust != "builtin" && trust != "verified" && trust != "reviewed") return false;
    }
    return state.unsafe_boundaries == 0 && state.suppressions == 0;
}

std::string readFile(const std::string &path, std::string &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "cannot read file: " + path; return {}; }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) { error = "failed reading file: " + path; return {}; }
    return contents.str();
}

std::string gitHead() {
    FILE *pipe = popen("/usr/bin/git rev-parse HEAD 2>/dev/null", "r");
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
    FILE *pipe = popen("/usr/bin/git rev-parse --verify 'origin/main^{commit}' 2>/dev/null", "r");
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

void rejectToolchain(AgentPolicyState &state, const std::string &detail) {
    state.toolchain_supported = false;
    state.policy_failed = true;
    state.policy_errors.push_back("unsupported C&1 toolchain: " + detail);
}

void validateSupportedToolchain(AgentPolicyState &state) {
    std::string os_error;
    const std::string os_release = readFile("/etc/os-release", os_error);
    if (!os_error.empty() || os_release.find("ID=ubuntu") == std::string::npos ||
        os_release.find("VERSION_ID=\"24.04\"") == std::string::npos)
        rejectToolchain(state, "runtime OS is not Ubuntu 24.04");
    if (std::string(CLANG_VERSION_STRING) != CAND_TOOLCHAIN_CLANG_VERSION)
        rejectToolchain(state, "Clang " + std::string(CLANG_VERSION_STRING));
    if (std::string(LLVM_VERSION_STRING) != CAND_TOOLCHAIN_LLVM_VERSION)
        rejectToolchain(state, "LLVM " + std::string(LLVM_VERSION_STRING));
    if (std::string(CAND_BUILD_COMPILER_ID) != "Clang")
        rejectToolchain(state, "verifier build compiler " + std::string(CAND_BUILD_COMPILER_ID));
    if (std::string(CAND_BUILD_COMPILER_VERSION) != CAND_TOOLCHAIN_CLANG_VERSION)
        rejectToolchain(state, "verifier build compiler version " + std::string(CAND_BUILD_COMPILER_VERSION));
    const std::string target = llvm::sys::getDefaultTargetTriple();
    if (target != CAND_TOOLCHAIN_TARGET)
        rejectToolchain(state, "target " + target);
    if (std::string(CAND_TOOLCHAIN_ENVIRONMENT_DIGEST) == "unknown")
        rejectToolchain(state, "missing reference environment digest");
}

llvm::json::Object toolchainJson() {
    llvm::json::Object toolchain;
    toolchain["host"] = CAND_TOOLCHAIN_HOST;
    toolchain["architecture"] = CAND_TOOLCHAIN_ARCH;
    toolchain["container"] = CAND_TOOLCHAIN_CONTAINER;
    toolchain["compiler"] = "clang";
    toolchain["clang_version"] = CLANG_VERSION_STRING;
    toolchain["llvm_version"] = LLVM_VERSION_STRING;
    toolchain["build_compiler"] = CAND_BUILD_COMPILER_ID;
    toolchain["build_compiler_version"] = CAND_BUILD_COMPILER_VERSION;
    toolchain["build_compiler_path"] = CAND_BUILD_COMPILER_PATH;
    toolchain["cmake_version"] = CAND_TOOLCHAIN_CMAKE_VERSION;
    toolchain["ninja_version"] = CAND_TOOLCHAIN_NINJA_VERSION;
    toolchain["target"] = llvm::sys::getDefaultTargetTriple();
    toolchain["standard"] = CAND_TOOLCHAIN_STANDARD;
    toolchain["sysroot"] = CAND_TOOLCHAIN_SYSROOT;
    toolchain["environment_digest"] = CAND_TOOLCHAIN_ENVIRONMENT_DIGEST;
    return toolchain;
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
    cand_info["build_identity"] = llvm::formatv(
        "cand-0.1.0-dev/clang-{0}/llvm-{1}/cxx-{2}-{3}/target-{4}/cmake-{5}/ninja-{6}/env-{7}",
        CLANG_VERSION_STRING, LLVM_VERSION_STRING, CAND_BUILD_COMPILER_ID,
        CAND_BUILD_COMPILER_VERSION, llvm::sys::getDefaultTargetTriple(),
        CAND_TOOLCHAIN_CMAKE_VERSION, CAND_TOOLCHAIN_NINJA_VERSION,
        CAND_TOOLCHAIN_ENVIRONMENT_DIGEST).str();
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
    frontend["standard"] = CAND_TOOLCHAIN_STANDARD;
    frontend["target"] = llvm::sys::getDefaultTargetTriple();
    frontend["sysroot"] = CAND_TOOLCHAIN_SYSROOT;
    frontend["arguments"] = std::move(frontend_args);
    evidence["frontend"] = std::move(frontend);
    evidence["toolchain"] = toolchainJson();
    llvm::json::Object verification;
    verification["profile"] = state.policy.profile;
    verification["safety_level"] = state.policy.safety_level;
    verification["policy_path"] = PolicyPath.getValue();
    verification["base_ref"] = BaseRef.getValue();
    verification["trusted_base_sha"] = TrustedBaseCommit;
    verification["effective_policy_sha256"] = state.policy.sha256;
    verification["checked_scope"] = state.policy.scope_files;
    verification["policy_revision"] = gitHead();
    if (SafetyLevel == "cand1") {
        verification["profile_version"] = "cand1/v1";
        verification["acceptance_invariant"] = "can_emit_cand1_pass";
        verification["heap_abstraction"] = "allocation-site-generation-v1";
    }
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
        if (auto value = counts->getInteger("heap_generation_widenings")) coverage["heap_generation_widenings"] = *value;
        if (auto value = counts->getInteger("unsupported_transport_operations")) coverage["unsupported_transport_operations"] = *value;
    }
    coverage["ownership_rule_set"] = "p1-unique-ownership-v1";
    // #41: the evidence transport rule set follows the effective rule set
    // (the embedded semantic report already carries cand1/v1.1-draft).
    coverage["transport_rule_set"] = collector.pointerOutputContracts()
                                         ? "cand1-pointer-transport-v2"
                                         : "cand1-pointer-transport-v1";
    if (const auto *borrows = analysis.getObject("borrow_analysis")) {
        llvm::json::Object borrow_copy;
        for (const auto &entry : *borrows) borrow_copy[entry.first] = entry.second;
        coverage["borrow_analysis"] = std::move(borrow_copy);
    }
    coverage["unsafe_boundaries"] = static_cast<std::int64_t>(state.unsafe_boundaries);
    coverage["suppressions"] = static_cast<std::int64_t>(state.suppressions);
    evidence["analysis"] = std::move(coverage);
    llvm::json::Object completeness;
    completeness["unsupported_transport_operations"] =
        static_cast<std::int64_t>(collector.unsupportedTransportCount());
    completeness["unsupported_borrow_operations"] =
        static_cast<std::int64_t>(collector.unsupportedBorrowCount());
    completeness["unsupported_ownership_operations"] =
        static_cast<std::int64_t>(collector.unsupportedCount());
    evidence["completeness"] = std::move(completeness);
    llvm::json::Array contracts;
    for (const auto &entry : state.contract_inputs) {
        llvm::json::Object contract;
        contract["path"] = std::get<0>(entry);
        contract["sha256"] = std::get<1>(entry);
        contract["trust_class"] = std::get<2>(entry);
        contracts.push_back(std::move(contract));
    }
    evidence["contracts"] = std::move(contracts);
    llvm::json::Array annotation_reviews;
    for (const auto &entry : state.annotation_review_inputs) {
        llvm::json::Object review;
        review["path"] = std::get<0>(entry);
        review["sha256"] = std::get<1>(entry);
        review["trust_class"] = std::get<2>(entry);
        annotation_reviews.push_back(std::move(review));
    }
    evidence["annotation_reviews"] = std::move(annotation_reviews);
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
    const auto *recorded_toolchain = evidence.getObject("toolchain");
    const auto toolchain = toolchainJson();
    const auto toolchainFieldMatches = [&](llvm::StringRef key) {
        const auto expected = toolchain.getString(key);
        const auto actual = recorded_toolchain ? recorded_toolchain->getString(key) : std::nullopt;
        return expected && actual && expected->str() == actual->str();
    };
    for (const llvm::StringRef key : {"host", "architecture", "container", "compiler",
                                      "clang_version", "llvm_version", "build_compiler",
                                      "build_compiler_version", "build_compiler_path", "cmake_version",
                                      "ninja_version", "target", "standard", "sysroot",
                                      "environment_digest"}) {
        if (!toolchainFieldMatches(key)) {
            status = "stale"; detail = "toolchain identity differs from the running verifier"; return false;
        }
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
    auto recorded_safety_level = verification ? verification->getString("safety_level") : std::nullopt;
    if (!recorded_safety_level ||
        (recorded_safety_level->str() != "p0-temporal-lifecycle" &&
         recorded_safety_level->str() != "cand1")) {
        status = "tampered"; detail = "invalid recorded safety level"; return false;
    }
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
    const auto *annotation_reviews = evidence.getArray("annotation_reviews");
    if (annotation_reviews) for (const auto &item : *annotation_reviews) {
        const auto *review = item.getAsObject();
        auto review_path = review ? review->getString("path") : std::nullopt;
        auto expected = review ? review->getString("sha256") : std::nullopt;
        std::string actual;
        if (!review_path || !relative_manifest_path(*review_path) || !expected ||
            !cand::sha256File(review_path->str(), actual, error) || actual != expected->str()) {
            status = "stale"; detail = "reviewed annotation manifest changed or unavailable"; return false;
        }
    }
    const auto *frontend = evidence.getObject("frontend");
    const auto *arguments = frontend ? frontend->getArray("arguments") : nullptr;
    const auto *scope = verification ? verification->getArray("checked_scope") : nullptr;
    if (!arguments || !scope || scope->empty() || CandExecutablePath.empty()) {
        status = "tampered"; detail = "evidence cannot be replayed"; return false;
    }
    cand::Policy replay_policy;
    if (!cand::loadPolicy(policy_path->str(), replay_policy, error)) {
        status = "stale"; detail = "effective policy cannot be loaded for frontend replay"; return false;
    }
    std::vector<std::string> replay_arguments;
    for (const auto &item : *arguments) {
        auto argument = item.getAsString();
        if (!argument) {
            status = "tampered"; detail = "invalid frontend argument"; return false;
        }
        if (argument->str() != "-std=c11") replay_arguments.push_back(argument->str());
    }
    if (replay_arguments != replay_policy.frontend_arguments) {
        status = "stale"; detail = "frontend arguments differ from the policy"; return false;
    }
    for (const llvm::StringRef key : {"compiler", "version", "llvm_version", "standard", "target", "sysroot"}) {
        const auto expected = toolchain.getString(key == "compiler" ? "compiler" :
                                                   key == "version" ? "clang_version" : key);
        const auto actual = frontend->getString(key);
        if (!expected || !actual || expected->str() != actual->str()) {
            status = "stale"; detail = "frontend identity differs from the running verifier"; return false;
        }
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
        " check --agent --level " + quoteShellArgument(recorded_safety_level->str()) +
        " --base origin/main --policy " + quoteShellArgument(policy_path->str()) +
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

// The reviewed annotation manifest is a trusted input exactly like a contract
// bundle and is pinned through the same policy pin list (path + sha256 +
// trust class). A candidate or substituted manifest never reaches the
// analyzer (ADR-0029).
bool validateAnnotationReview(AgentPolicyState &state) {
    if (AnnotationReviewPath.empty()) return true;
    std::string digest, error, trust;
    const std::string normalized = cand::normalizedRelativePath(AnnotationReviewPath);
    if (normalized.empty() || !cand::sha256File(AnnotationReviewPath, digest, error) ||
        !cand::contractIsPinned(state.policy, normalized, digest, trust)) {
        state.policy_failed = true;
        state.review_required = true;
        state.policy_errors.push_back("annotation review path/content is not pinned by the effective policy");
        state.delta.review_required = true;
        state.delta.classification = state.delta.weakened ? "PROOF_WEAKENING" : "REVIEW_REQUIRED";
        state.delta.changes.push_back({"annotation-review-set-substitution", "pinned trusted inputs", normalized,
                                       "REVIEW_REQUIRED"});
        AnnotationReviewPath = ""; // candidate or substituted manifests never reach the analyzer
        return false;
    }
    state.annotation_review_inputs.emplace_back(normalized, digest, trust);
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
        "CFLAGS", "CPPFLAGS", "CXXFLAGS", "LDFLAGS", "CLANG_CONFIG_FILE",
        "LD_LIBRARY_PATH", "LD_PRELOAD", "LIBRARY_PATH"
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
    bool pending_forbidden_path = false;
    for (const auto &entry : arguments) {
        const auto value = entry.getAsString();
        if (!value) { reject("frontend argument is not a string"); continue; }
        const std::string argument = value->str();
        if (pending_forbidden_path) {
            reject("frontend path flag is not permitted in generated verification: " + argument);
            pending_forbidden_path = false;
            continue;
        }
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
        if (argument == "-isysroot" || argument == "--sysroot" || argument == "-target" ||
            argument == "--target" || llvm::StringRef(argument).starts_with("-isysroot=") ||
            llvm::StringRef(argument).starts_with("--sysroot=") || llvm::StringRef(argument).starts_with("@") ||
            argument == "-Xclang" || argument == "-load" || llvm::StringRef(argument).starts_with("-fplugin") ||
            llvm::StringRef(argument).starts_with("-fmodule") ||
            llvm::StringRef(argument).starts_with("-fpass-plugin") ||
            llvm::StringRef(argument).starts_with("-load-pass-plugin") || argument == "-mllvm" ||
            argument == "-include-pch" || argument == "-fpch-preprocess" ||
            llvm::StringRef(argument).starts_with("-resource-dir") ||
            llvm::StringRef(argument).starts_with("-working-directory") ||
            llvm::StringRef(argument).starts_with("--target=") ||
            llvm::StringRef(argument).starts_with("-target=") || argument == "-m32" ||
            argument == "-m64") {
            reject("frontend argument is not permitted in generated verification: " + argument);
            if (argument == "-isysroot" || argument == "--sysroot" || argument == "-target" ||
                argument == "--target") pending_forbidden_path = true;
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
    if (pending_path || pending_forbidden_path)
        reject("frontend path flag is missing its path");
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
                 << " check [--agent] [--profile semantic|generated] [--level p0-temporal-lifecycle|cand1] [--policy file] [--base ref] [--emit-evidence file] <source...> [-- <clang-args...>]\n"
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
    if (SafetyLevel != "p0-temporal-lifecycle" && SafetyLevel != "cand1") {
        llvm::errs() << "cand: unsupported safety level; supported values are p0-temporal-lifecycle and experimental cand1\n";
        return 2;
    }
    // #41 [A2]: the CLI modifier is non-authoritative and cand1-only. The
    // usage error fires immediately after option parsing, before any
    // policy load, so it can never pollute policy_failed accounting.
    if (PointerOutputContracts && SafetyLevel != "cand1") {
        llvm::errs() << "cand: --pointer-output-contracts requires --level=cand1\n";
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
        if (SafetyLevel == "cand1") validateSupportedToolchain(agent_state);
        // #41 [F11]: in agent mode the policy features block is the sole
        // rule-set authority for the pointer-output contracts. The CLI
        // modifier is a request reconciled against it: enabling the
        // modifier without the policy feature is a fail-policy condition
        // (never a silent downgrade), while the policy feature alone
        // enables it (never a silent upgrade from the modifier's absence).
        if (SafetyLevel == "cand1" && PointerOutputContracts &&
            !agent_state.policy.pointer_output_contracts.value_or(false)) {
            agent_state.policy_failed = true;
            agent_state.policy_errors.push_back(
                "--pointer-output-contracts requires features.pointer_output_contracts "
                "in the effective policy");
        }
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
        (void)validateAnnotationReview(agent_state);
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
    // #41 [F11]: agent mode derives the feature from the policy features
    // block (cand1 runs only); a non-agent run derives it from the CLI
    // modifier. A feature-enabled p0 run never happens (the usage check
    // above exits first for the modifier path; the policy feature is only
    // consulted at cand1).
    const bool pointer_output_enabled =
        AgentMode
            ? (SafetyLevel == "cand1" &&
               agent_state.policy.pointer_output_contracts.value_or(false))
            : static_cast<bool>(PointerOutputContracts);
    collector.setProfile(SafetyLevel, pointer_output_enabled);
    CandActionFactory factory(collector, SafetyLevel == "cand1", pointer_output_enabled);
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
        const bool cand1 = SafetyLevel == "cand1";
        const bool evidence_bound = AgentMode && !source_inputs.empty() &&
            !frontend_args.empty() && !CandExecutableSha256.empty() &&
            CAND_VERIFIER_SOURCE_COMMIT != std::string("unknown");
        const bool cand1_pass = cand1 && canEmitCand1Pass(collector, agent_state, evidence_bound);
        const char *final_result = policy_fail ? "fail-policy" :
                                   (review ? "review-required" :
                                    (cand1 ? (cand1_pass ? "pass" :
                                              (semantic == "fail" ? "fail" : "incomplete"))
                                            : semantic.data()));
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
        if (SafetyLevel == "cand1") {
            llvm::json::Object result = collector.jsonObject();
            result["result"] = collector.hasFindings() ? "fail" : "incomplete";
            result["cand1_acceptance"] = "requires trusted generated evidence and policy bindings";
            llvm::outs() << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(result)));
        } else {
            collector.printJson();
        }
    } else {
        collector.printHuman();
    }

    if (SafetyLevel == "cand1" && !AgentMode)
        return collector.hasFindings() ? 1 : 3;
    return collector.exitCode();
}
