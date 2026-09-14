#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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
using clang::BinaryOperator;
using clang::CallExpr;
using clang::CompoundStmt;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::GotoStmt;
using clang::IfStmt;
using clang::MemberExpr;
using clang::RecursiveASTVisitor;
using clang::ReturnStmt;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Stmt;
using clang::SwitchStmt;
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

struct LocationLess {
    bool operator()(const Location &a, const Location &b) const {
        return std::tie(a.file, a.line, a.column) <
               std::tie(b.file, b.line, b.column);
    }
};

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
    void addFinding(Finding finding) { findings_.push_back(std::move(finding)); }

    void addUnsupported(Unsupported unsupported) {
        // The same obligation can be observed from more than one analysis
        // path (e.g. an initializer and the call expression it contains).
        // Report it once per site.
        const std::tuple<std::string, std::string, unsigned, unsigned> key{
            unsupported.kind, unsupported.primary.file,
            unsupported.primary.line, unsupported.primary.column};
        if (!seen_unsupported_.insert(key).second) {
            return;
        }
        unsupported_.push_back(std::move(unsupported));
    }

    void noteFunction() { ++functions_analyzed_; }

    void noteTrackedHeapObject() { ++tracked_heap_objects_; }

    unsigned nextObjectId() { return next_object_id_++; }

    void sort() {
        const auto by_location = [](const auto &a, const auto &b) {
            return std::tie(a.primary.file, a.primary.line, a.primary.column) <
                   std::tie(b.primary.file, b.primary.line, b.primary.column);
        };
        std::sort(findings_.begin(), findings_.end(), by_location);
        std::sort(unsupported_.begin(), unsupported_.end(), by_location);
    }

    bool hasFindings() const { return !findings_.empty(); }
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
        for (const auto &finding : findings_) {
            llvm::errs() << finding.primary.file << ':' << finding.primary.line << ':'
                         << finding.primary.column << ": error[" << finding.id << "]: "
                         << finding.message << " (" << finding.object_id << ")\n";
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
        root["result"] = hasFindings() ? "fail" : (hasUnsupported() ? "incomplete" : "pass");
        root["safety_level"] = "p0-temporal-lifecycle";
        root["profile"] = "p0-semantic-core";

        llvm::json::Array findings;
        for (const auto &finding : findings_) {
            llvm::json::Object obj;
            obj["id"] = finding.id;
            obj["rule_id"] = finding.rule_id;
            obj["severity"] = "error";
            obj["safety_level"] = "cand1";
            obj["message_key"] = finding.id;
            obj["message"] = finding.message;
            obj["repair_class"] = finding.repair_class;
            obj["object_id"] = finding.object_id;
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

    std::vector<Finding> findings_;
    std::vector<Unsupported> unsupported_;
    std::set<std::tuple<std::string, std::string, unsigned, unsigned>>
        seen_unsupported_;
    unsigned functions_analyzed_ = 0;
    unsigned tracked_heap_objects_ = 0;
    unsigned next_object_id_ = 1;
};

enum class ObjectState { Owned, Dead };

struct ObjectInfo {
    unsigned id = 0;
    ObjectState state = ObjectState::Owned;
    SourceLocation allocation;
    SourceLocation destruction;
};

/*
 * P0.1 PASS-completeness rule:
 *
 *   PASS = no known violation AND no unresolved ownership/lifetime
 *   operation inside the P0 checked scope.
 *
 * Every heap-relevant operation the analyzer encounters must classify as
 * exactly one of SUPPORTED, KNOWN SAFE, KNOWN VIOLATION or
 * UNSUPPORTED/INCOMPLETE. There is deliberately no fifth category of
 * "unknown but still PASS".
 */
class FunctionAnalyzer {
public:
    FunctionAnalyzer(ASTContext &context, Collector &collector)
        : context_(context), source_manager_(context.getSourceManager()), collector_(collector) {}

    void analyze(const FunctionDecl &function) {
        if (const Stmt *body = function.getBody()) {
            collector_.noteFunction();
            analyzeStmt(body);
        }
    }

    /*
     * File-scope pointer initializers are reachable from every function but
     * owned by none. P0 tracks per-function locals only, so an allocation or
     * an unmodelled pointer-returning call in a global initializer cannot be
     * tracked: report the model gap instead of leaving the global silently
     * untracked.
     */
    void analyzeGlobal(const VarDecl &var) {
        const Expr *init = var.getInit();
        if (init == nullptr || !var.getType()->isPointerType()) {
            return;
        }
        scanExpr(init);
        if (isAllocation(init)) {
            collector_.addUnsupported({"allocation-to-untracked-storage:global",
                                       "", location(var.getLocation())});
        } else if (isNullConstant(init)) {
            /* KNOWN SAFE: null pointer initialization */
        } else {
            checkPointerValueSource(init);
        }
    }

private:
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

    /*
     * A call whose result is a pointer and which is not a recognized
     * allocator. C& has no interprocedural summary or trusted contract for
     * it, so the ownership of the returned pointer (owned, borrowed, static,
     * retained, nullable, ...) is unknown. Any use that requires ownership
     * information must fail closed.
     */
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

    std::string unknownPointerSymbol(const CallExpr &call) const {
        if (const FunctionDecl *callee = call.getDirectCallee()) {
            return callee->getNameAsString();
        }
        return "indirect";
    }

    void noteUnknownPointerCall(const CallExpr &call) {
        Unsupported unsupported;
        unsupported.kind =
            "unknown-pointer-return-ownership:" + unknownPointerSymbol(call);
        unsupported.symbol = unknownPointerSymbol(call);
        unsupported.primary = location(call.getExprLoc());
        collector_.addUnsupported(std::move(unsupported));
    }

    void noteUnknownPointerCallIn(const Expr *expr) {
        if (const CallExpr *call = asUnknownPointerCall(expr)) {
            noteUnknownPointerCall(*call);
        }
    }

    bool isSimpleNullGuard(const IfStmt &stmt) const {
        if (stmt.getElse() != nullptr) {
            return false;
        }
        const Expr *condition = stmt.getCond()->IgnoreParenImpCasts();
        const auto *binary = dyn_cast<BinaryOperator>(condition);
        if (binary == nullptr || binary->getOpcode() != clang::BO_EQ) {
            return false;
        }

        const Expr *lhs = binary->getLHS()->IgnoreParenCasts();
        const Expr *rhs = binary->getRHS()->IgnoreParenCasts();
        const bool lhs_var_rhs_null =
            resolveVar(lhs) != nullptr &&
            rhs->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull);
        const bool rhs_var_lhs_null =
            resolveVar(rhs) != nullptr &&
            lhs->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull);
        if (!lhs_var_rhs_null && !rhs_var_lhs_null) {
            return false;
        }

        const Stmt *then_stmt = stmt.getThen();
        if (isa<ReturnStmt>(then_stmt)) {
            return true;
        }
        const auto *compound = dyn_cast<CompoundStmt>(then_stmt);
        return compound != nullptr && compound->size() == 1 &&
               isa<ReturnStmt>(*compound->body_begin());
    }

    void bindAllocation(const VarDecl &var, SourceLocation loc) {
        const unsigned id = collector_.nextObjectId();
        objects_[id] = ObjectInfo{id, ObjectState::Owned, loc, SourceLocation()};
        bindings_[&var] = id;
        collector_.noteTrackedHeapObject();
    }

    ObjectInfo *objectForVar(const VarDecl *var) {
        if (var == nullptr) {
            return nullptr;
        }
        const auto binding = bindings_.find(var);
        if (binding == bindings_.end()) {
            return nullptr;
        }
        const auto object = objects_.find(binding->second);
        return object == objects_.end() ? nullptr : &object->second;
    }

    const ObjectInfo *objectForVar(const VarDecl *var) const {
        if (var == nullptr) {
            return nullptr;
        }
        const auto binding = bindings_.find(var);
        if (binding == bindings_.end()) {
            return nullptr;
        }
        const auto object = objects_.find(binding->second);
        return object == objects_.end() ? nullptr : &object->second;
    }

    bool containsTrackedVar(const Expr *expr) const {
        if (expr == nullptr) {
            return false;
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (const auto *var = dyn_cast<VarDecl>(ref->getDecl())) {
                return objectForVar(var) != nullptr;
            }
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsTrackedVar(child_expr)) {
                    return true;
                }
            }
        }
        return false;
    }

    std::string objectName(unsigned id) const {
        return "obj:" + std::to_string(id);
    }

    void reportUseAfterDestroy(const ObjectInfo &object, SourceLocation use_loc) {
        Finding finding;
        finding.id = "CAND-T002";
        finding.rule_id = "cand1.no-use-after-death";
        finding.message = "use after object destruction";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.object_id = objectName(object.id);
        finding.primary = location(use_loc);
        finding.trace.push_back({"allocation", "Owned", location(object.allocation)});
        finding.trace.push_back({"destruction", "Dead", location(object.destruction)});
        finding.trace.push_back({"access", "Dead", location(use_loc)});
        collector_.addFinding(std::move(finding));
    }

    void reportDoubleDestroy(const ObjectInfo &object, SourceLocation destroy_loc) {
        Finding finding;
        finding.id = "CAND-T003";
        finding.rule_id = "cand1.single-destruction";
        finding.message = "object destroyed more than once";
        finding.repair_class = "SEMANTIC_REPAIR";
        finding.object_id = objectName(object.id);
        finding.primary = location(destroy_loc);
        finding.trace.push_back({"allocation", "Owned", location(object.allocation)});
        finding.trace.push_back({"first_destruction", "Dead", location(object.destruction)});
        finding.trace.push_back({"repeated_destruction", "Dead", location(destroy_loc)});
        collector_.addFinding(std::move(finding));
    }

    void checkAccess(const Expr *pointer_expr, SourceLocation access_loc) {
        ObjectInfo *object = objectForVar(resolveVar(pointer_expr));
        if (object != nullptr && object->state == ObjectState::Dead) {
            reportUseAfterDestroy(*object, access_loc);
        }
    }

    void markUnsupported(const Stmt &stmt, llvm::StringRef kind) {
        collector_.addUnsupported({kind.str(), "", location(stmt.getBeginLoc())});
    }

    /*
     * P0.1 free() rule — never silently ignore a free():
     *
     *   free(p)  tracked object  -> ownership transition (T003 if already dead)
     *   free(NULL)               -> known safe (ISO C), allowed
     *   free(p)  untracked var   -> INCOMPLETE (free-untracked-pointer)
     *   free(<expr>) anything else -> INCOMPLETE (free-untracked-expression)
     */
    void analyzeFree(const CallExpr &call) {
        const Expr *arg = call.getArg(0);

        if (isNullConstant(arg)) {
            return; /* KNOWN SAFE: free(NULL) is a no-op per ISO C */
        }

        const VarDecl *var = resolveVar(arg);
        if (var != nullptr) {
            ObjectInfo *object = objectForVar(var);
            if (object == nullptr) {
                collector_.addUnsupported(
                    {"free-untracked-pointer", "", location(call.getExprLoc())});
                return;
            }
            if (object->state == ObjectState::Dead) {
                reportDoubleDestroy(*object, call.getExprLoc());
                return;
            }
            object->state = ObjectState::Dead;
            object->destruction = call.getExprLoc();
            return;
        }

        collector_.addUnsupported(
            {"free-untracked-expression", "", location(call.getExprLoc())});
    }

    void analyzeCall(const CallExpr &call) {
        if (isNamedCall(call, "free") && call.getNumArgs() == 1) {
            analyzeFree(call);
            return;
        }

        if (isAllocatorCall(call)) {
            for (const Expr *arg : call.arguments()) {
                scanExpr(arg);
            }
            return;
        }

        bool tracked_argument = false;
        for (const Expr *arg : call.arguments()) {
            scanExpr(arg);
            tracked_argument = tracked_argument || containsTrackedVar(arg);
            // An unrecognized pointer-returning call used as an argument
            // transfers an unknown-ownership pointer somewhere C& cannot
            // model (escape into a callee). Fail closed.
            noteUnknownPointerCallIn(arg);
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

    void scanExpr(const Expr *expr) {
        if (expr == nullptr) {
            return;
        }
        expr = expr->IgnoreParenImpCasts();

        if (isa<UnaryExprOrTypeTraitExpr>(expr)) {
            return;
        }

        // GNU statement expressions embed control flow that the linear P0
        // analyzer would flatten unsoundly. They can appear at expression
        // positions (initializers, RHS), so scanExpr must reject them too.
        if (isa<clang::StmtExpr>(expr)) {
            markUnsupported(*expr, "statement-expression");
            return;
        }

        if (const auto *call = dyn_cast<CallExpr>(expr)) {
            analyzeCall(*call);
            return;
        }

        if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            if (unary->getOpcode() == clang::UO_Deref) {
                const Expr *sub = unary->getSubExpr();
                if (asUnknownPointerCall(sub) != nullptr) {
                    noteUnknownPointerCallIn(sub);
                } else {
                    checkAccess(sub, unary->getOperatorLoc());
                }
            }
        } else if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr)) {
            const Expr *base = subscript->getBase();
            if (asUnknownPointerCall(base) != nullptr) {
                noteUnknownPointerCallIn(base);
            } else {
                checkAccess(base, subscript->getExprLoc());
            }
        } else if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (member->isArrow()) {
                const Expr *base = member->getBase();
                if (asUnknownPointerCall(base) != nullptr) {
                    noteUnknownPointerCallIn(base);
                } else {
                    checkAccess(base, member->getExprLoc());
                }
            }
        }

        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                scanExpr(child_expr);
            }
        }
    }

    /*
     * Classify the storage location on the LHS of a pointer assignment that
     * is not a plain trackable variable. P0.1 does not model these
     * locations, but must know its model is incomplete.
     */
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

    /*
     * Shared logic for pointer initializers (DeclStmt) and pointer
     * assignments: fail closed when the value comes from a call whose
     * pointer-return ownership C& does not model.
     */
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

    void analyzeStmt(const Stmt *stmt) {
        if (stmt == nullptr) {
            return;
        }

        if (const auto *compound = dyn_cast<CompoundStmt>(stmt)) {
            for (const Stmt *child : compound->body()) {
                analyzeStmt(child);
            }
            return;
        }

        if (const auto *decl_stmt = dyn_cast<DeclStmt>(stmt)) {
            for (const clang::Decl *decl : decl_stmt->decls()) {
                const auto *var = dyn_cast<VarDecl>(decl);
                if (var == nullptr || var->getInit() == nullptr) {
                    continue;
                }

                const Expr *init = var->getInit();
                if (!var->getType()->isPointerType()) {
                    scanExpr(init);
                    continue;
                }

                scanExpr(init);
                if (isAllocation(init)) {
                    bindAllocation(*var, init->getExprLoc());
                } else if (containsTrackedVar(init)) {
                    markUnsupported(*decl_stmt, "pointer-alias-initialization");
                } else if (isNullConstant(init)) {
                    /* KNOWN SAFE: null pointer initialization */
                } else {
                    checkPointerValueSource(init);
                }
            }
            return;
        }

        if (const auto *if_stmt = dyn_cast<IfStmt>(stmt)) {
            scanExpr(if_stmt->getCond());
            if (!isSimpleNullGuard(*if_stmt)) {
                markUnsupported(*if_stmt, "if-control-flow");
            }
            return;
        }

        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            scanExpr(binary->getRHS());
            if (binary->isAssignmentOp()) {
                const Expr *lhs_expr = binary->getLHS();
                const VarDecl *lhs = resolveVar(lhs_expr);
                if (lhs != nullptr && lhs->getType()->isPointerType()) {
                    const bool lhs_tracked = objectForVar(lhs) != nullptr;
                    if (isAllocation(binary->getRHS())) {
                        if (lhs_tracked) {
                            markUnsupported(*binary, "tracked-owner-overwrite");
                        }
                        bindAllocation(*lhs, binary->getRHS()->getExprLoc());
                    } else {
                        if (lhs_tracked) {
                            markUnsupported(*binary, "tracked-pointer-reassignment");
                        }
                        if (containsTrackedVar(binary->getRHS())) {
                            markUnsupported(*binary, "pointer-alias-assignment");
                        } else if (!isNullConstant(binary->getRHS())) {
                            checkPointerValueSource(binary->getRHS());
                        }
                        scanExpr(binary->getLHS());
                    }
                } else if (lhs_expr->getType()->isPointerType() ||
                           (lhs != nullptr && lhs->getType()->isPointerType())) {
                    // Pointer-typed assignment to storage C& cannot model
                    // (struct member, array element, pointee, ...).
                    if (isAllocation(binary->getRHS())) {
                        collector_.addUnsupported(
                            {"allocation-to-untracked-storage:" +
                                 untrackedStorageKind(lhs_expr),
                             "", location(binary->getExprLoc())});
                    } else if (containsTrackedVar(binary->getRHS())) {
                        markUnsupported(*binary, "pointer-alias-assignment");
                    } else if (!isNullConstant(binary->getRHS())) {
                        checkPointerValueSource(binary->getRHS());
                    }
                    scanExpr(binary->getLHS());
                } else {
                    scanExpr(binary->getLHS());
                }
            } else {
                scanExpr(binary->getLHS());
            }
            return;
        }

        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            analyzeCall(*call);
            return;
        }

        if (const auto *return_stmt = dyn_cast<ReturnStmt>(stmt)) {
            const Expr *ret = return_stmt->getRetValue();
            scanExpr(ret);
            if (ret != nullptr && ret->getType()->isPointerType()) {
                if (containsTrackedVar(ret)) {
                    markUnsupported(*return_stmt, "tracked-pointer-return");
                }
                // Returning a pointer to automatic storage is always a
                // lifetime bug (the classic stack-escape). It involves no
                // tracked heap object, so without this check it would pass
                // silently. Statics, globals and parameters are excluded.
                const Expr *stripped = ret->IgnoreParenCasts();
                if (const VarDecl *var = resolveVar(stripped)) {
                    if (var->hasLocalStorage() && !isa<clang::ParmVarDecl>(var) &&
                        var->getType()->isArrayType()) {
                        markUnsupported(*return_stmt, "stack-pointer-return");
                    }
                } else if (const auto *addr =
                               dyn_cast<UnaryOperator>(stripped)) {
                    if (addr->getOpcode() == clang::UO_AddrOf) {
                        const VarDecl *var = resolveVar(addr->getSubExpr());
                        if (var != nullptr && var->hasLocalStorage() &&
                            !isa<clang::ParmVarDecl>(var)) {
                            markUnsupported(*return_stmt, "stack-pointer-return");
                        }
                    }
                }
            }
            return;
        }

        if (isa<clang::AsmStmt>(stmt)) {
            // Inline asm can affect any ownership/lifetime state and is
            // completely opaque to the analyzer.
            markUnsupported(*stmt, "inline-asm");
            return;
        }

        if (isa<clang::StmtExpr>(stmt)) {
            // GNU statement expressions embed control flow that the linear
            // P0 analyzer would flatten unsoundly.
            markUnsupported(*stmt, "statement-expression");
            return;
        }

        if (isa<ForStmt>(stmt)) {
            markUnsupported(*stmt, "for-control-flow");
            return;
        }
        if (isa<WhileStmt>(stmt)) {
            markUnsupported(*stmt, "while-control-flow");
            return;
        }
        if (isa<DoStmt>(stmt)) {
            markUnsupported(*stmt, "do-control-flow");
            return;
        }
        if (isa<SwitchStmt>(stmt)) {
            markUnsupported(*stmt, "switch-control-flow");
            return;
        }
        if (isa<GotoStmt>(stmt)) {
            markUnsupported(*stmt, "goto-control-flow");
            return;
        }

        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            scanExpr(expr);
            return;
        }

        for (const Stmt *child : stmt->children()) {
            analyzeStmt(child);
        }
    }

    ASTContext &context_;
    SourceManager &source_manager_;
    Collector &collector_;
    std::map<const VarDecl *, unsigned> bindings_;
    std::map<unsigned, ObjectInfo> objects_;
};

class TranslationUnitVisitor : public RecursiveASTVisitor<TranslationUnitVisitor> {
public:
    TranslationUnitVisitor(ASTContext &context, Collector &collector)
        : context_(context), collector_(collector) {}

    bool VisitFunctionDecl(FunctionDecl *function) {
        if (function == nullptr || !function->hasBody()) {
            return true;
        }
        SourceLocation loc = context_.getSourceManager().getExpansionLoc(function->getLocation());
        if (!context_.getSourceManager().isWrittenInMainFile(loc)) {
            return true;
        }
        FunctionAnalyzer analyzer(context_, collector_);
        analyzer.analyze(*function);
        return true;
    }

    bool VisitVarDecl(VarDecl *var) {
        if (var == nullptr || !var->hasInit()) {
            return true;
        }
        // File-scope initializers only. Function-local declarations are
        // analyzed through their DeclStmt and would be double-reported here.
        if (!isa<clang::TranslationUnitDecl>(var->getDeclContext())) {
            return true;
        }
        SourceLocation loc = context_.getSourceManager().getExpansionLoc(var->getLocation());
        if (!context_.getSourceManager().isWrittenInMainFile(loc)) {
            return true;
        }
        FunctionAnalyzer analyzer(context_, collector_);
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

    std::unique_ptr<ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &compiler,
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
    clang::tooling::ClangTool tool(
        options_parser.getCompilations(), options_parser.getSourcePathList());

    Collector collector;
    CandActionFactory factory(collector);
    const int tool_result = tool.run(&factory);
    if (tool_result != 0) {
        // Tool/compilation failure is a distinct outcome from a C& FAIL:
        //   0 = PASS, 1 = FAIL (findings), 2 = tool/input error,
        //   3 = INCOMPLETE. Never leak ClangTool's own exit codes, which
        //   can collide with the FAIL code.
        llvm::errs() << "cand: analysis frontend failed (input or compiler error)\n";
        return 2;
    }

    collector.sort();
    if (OutputFormat == "json") {
        collector.printJson();
    } else {
        collector.printHuman();
    }

    return collector.exitCode();
}
