from pathlib import Path

path = Path('src/cand.cpp')
s = path.read_text()

def replace(old: str, new: str, label: str) -> None:
    global s
    if old not in s:
        raise SystemExit(f'missing patch anchor: {label}')
    s = s.replace(old, new, 1)

replace('#include <cstdint>\n', '#include <cstdint>\n#include <functional>\n', 'functional include')

replace('''    bool operator<(const StorageId &other) const {
        return std::tie(kind, root, path, index) <
               std::tie(other.kind, other.root, other.path, other.index);
    }''', '''    bool operator<(const StorageId &other) const {
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
    }''', 'StorageId comparator')

replace('enum class PointerRelation { Owner, Alias, Null, Unknown };',
        'enum class PointerRelation { Owner, Alias, Null, MaybeNull, Unknown };',
        'MaybeNull relation')

replace('''    if (a.object_id == b.object_id) {
        if (a.relation == b.relation) {
            return {a.object_id, a.relation, relation_location};
        }
        return {a.object_id, PointerRelation::Unknown, relation_location};
    }''', '''    if (a.object_id == b.object_id) {
        if (a.relation == b.relation) {
            return {a.object_id, a.relation, relation_location};
        }
        if (a.relation == PointerRelation::MaybeNull ||
            b.relation == PointerRelation::MaybeNull) {
            return {a.object_id, PointerRelation::MaybeNull, relation_location};
        }
        return {a.object_id, PointerRelation::Unknown, relation_location};
    }''', 'join same object')

replace('''    if (known_null(a) && b.object_id != kNullObjectId) {
        return {b.object_id, b.relation, relation_location};
    }
    if (known_null(b) && a.object_id != kNullObjectId) {
        return {a.object_id, a.relation, relation_location};
    }''', '''    if (known_null(a) && b.object_id != kNullObjectId) {
        return {b.object_id, PointerRelation::MaybeNull, relation_location};
    }
    if (known_null(b) && a.object_id != kNullObjectId) {
        return {a.object_id, PointerRelation::MaybeNull, relation_location};
    }''', 'nullable join')

replace('''        collector_.noteFunction();
        collectAllocationSites(body);
        collectUnevaluated(body);''', '''        collector_.noteFunction();
        collectAllocationSites(body);
        collectLoopAllocations(body, false);
        collectUnevaluated(body);''', 'loop allocation prepass')

# Insert global-storage helper before storageFor.
anchor = '''    std::optional<StorageId> storageFor(const Expr *expr) const {'''
helper = '''    bool containsGlobalStorage(const Expr *expr) const {
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

'''
replace(anchor, helper + anchor, 'global helper')

replace('''        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (const auto *var = dyn_cast<VarDecl>(ref->getDecl()))
                return StorageId{StorageKind::LocalVariable, var, {}, -1};
            return std::nullopt;
        }''', '''        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            if (const auto *var = dyn_cast<VarDecl>(ref->getDecl())) {
                // File-scope and static-local storage outlives one invocation;
                // P0.3's per-function state cannot model it soundly.
                if (var->hasGlobalStorage()) return std::nullopt;
                return StorageId{StorageKind::LocalVariable, var, {}, -1};
            }
            return std::nullopt;
        }''', 'global storageFor')

replace('''    void checkAccess(const Expr *pointer_expr, SourceLocation access_loc,
                     const FlowState &state) {
        if (asUnknownPointerCall(pointer_expr) != nullptr) {''', '''    void checkAccess(const Expr *pointer_expr, SourceLocation access_loc,
                     const FlowState &state) {
        if (containsGlobalStorage(pointer_expr)) {
            emitUnsupported({"global-or-static-pointer-storage", "", location(access_loc)});
            return;
        }
        if (asUnknownPointerCall(pointer_expr) != nullptr) {''', 'global access')

replace('''        if (object->state == ObjectState::Dead || object->state == ObjectState::MaybeDead) {
            if (access_storage) {''', '''        if (binding->relation == PointerRelation::MaybeNull &&
            (object->state == ObjectState::Dead ||
             object->state == ObjectState::MaybeDead)) {
            // Nullness and object state may be correlated across predecessor
            // paths. Without edge/path predicates, reporting T002 here can be
            // a false temporal failure (the only non-null path may be Owned).
            emitUnsupported({"nullable-alias-state-correlation", "", location(access_loc)});
            return;
        }
        if (object->state == ObjectState::Dead || object->state == ObjectState::MaybeDead) {
            if (access_storage) {''', 'nullable access correlation')

replace('''    void handleFree(const CallExpr &call, FlowState &state) {
        const Expr *arg = call.getArg(0);
        if (isNullConstant(arg)) {''', '''    void handleFree(const CallExpr &call, FlowState &state) {
        const Expr *arg = call.getArg(0);
        if (containsGlobalStorage(arg)) {
            emitUnsupported({"global-or-static-pointer-storage", "", location(call.getExprLoc())});
            return;
        }
        if (isNullConstant(arg)) {''', 'global free')

replace('''        switch (object->state) {
        case ObjectState::Null:''', '''        if (binding.relation == PointerRelation::MaybeNull) {
            if (object->state == ObjectState::Owned) {
                object->state = ObjectState::MaybeDead;
                object->destruction = location(call.getExprLoc());
                object->destruction_known = true;
                object->destruction_storage = storageName(*destroy_storage);
                return;
            }
            if (object->state == ObjectState::Dead ||
                object->state == ObjectState::MaybeDead) {
                emitUnsupported({"nullable-alias-state-correlation", "",
                                 location(call.getExprLoc())});
                return;
            }
        }
        switch (object->state) {
        case ObjectState::Null:''', 'nullable free')

replace('''    void handleCall(const CallExpr &call, const FlowState &state) {
        if (isNamedCall(call, "free") && call.getNumArgs() == 1) {''', '''    void handleCall(const CallExpr &call, const FlowState &state) {
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
        if (isNamedCall(call, "free") && call.getNumArgs() == 1) {''', 'nonlocal control')

replace('''        bool tracked_argument = false;
        for (const Expr *arg : call.arguments()) {
            tracked_argument = tracked_argument || containsTrackedStorage(arg, state);''', '''        bool tracked_argument = false;
        bool global_argument = false;
        for (const Expr *arg : call.arguments()) {
            tracked_argument = tracked_argument || containsTrackedStorage(arg, state);
            global_argument = global_argument || containsGlobalStorage(arg);''', 'global call args')

replace('''        if (tracked_argument) {
            std::string kind = "unknown-call-with-tracked-pointer";''', '''        if (global_argument) {
            markUnsupported(call, "global-or-static-pointer-storage");
        }
        if (tracked_argument) {
            std::string kind = "unknown-call-with-tracked-pointer";''', 'global call result')

replace('''    void bindAllocation(StorageId storage, const Expr *init, FlowState &state) {
        const unsigned object_id = objectIdForAllocation(init);''', '''    bool containsLoopAllocation(const Expr *expr) const {
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
        const unsigned object_id = objectIdForAllocation(init);''', 'loop bind')

replace('''        // Defensive: an allocation site the pre-pass did not see gets a
        // stable id at the end of the range.
        const unsigned id = next_fallback_id_++;
        if (call != nullptr) {
            allocation_sites_[call] = id;
        }
        return id;''', '''        // Composite allocation expressions (e.g. `cond ? malloc() : NULL`)
        // need a stable synthetic id across worklist iterations as well.
        const Expr *key = init ? init->IgnoreParenCasts() : init;
        const auto synthetic = synthetic_allocation_sites_.find(key);
        if (synthetic != synthetic_allocation_sites_.end()) return synthetic->second;
        const unsigned id = next_fallback_id_++;
        if (call != nullptr) allocation_sites_[call] = id;
        if (key != nullptr) synthetic_allocation_sites_[key] = id;
        return id;''', 'stable synthetic object id')

replace('''            const Expr *init = var->getInit();
            if (!var->getType()->isPointerType()) {''', '''            const Expr *init = var->getInit();
            if (var->hasGlobalStorage() &&
                (var->getType()->isPointerType() || var->getType()->isAtomicType() ||
                 containsAllocationCall(init) || containsTrackedStorage(init, state) ||
                 containsUnknownPointerCall(init))) {
                markUnsupported(decl_stmt, "global-or-static-pointer-storage");
                continue;
            }
            if (!var->getType()->isPointerType()) {''', 'static local declaration')

replace('''                } else if (var->getType()->isRecordType() &&
                           containsTrackedStorage(init, state)) {''', '''                } else if (var->getType()->isAtomicType() &&
                           containsTrackedStorage(init, state)) {
                    markUnsupported(decl_stmt, "atomic-pointer-storage");
                } else if (var->getType()->isRecordType() &&
                           containsTrackedStorage(init, state)) {''', 'atomic declaration')

replace('''        const bool lhs_is_pointer =
            lhs->getType()->isPointerType() ||
            (var != nullptr && var->getType()->isPointerType());

        if (var != nullptr && var->getType()->isPointerType()) {''', '''        const bool lhs_is_pointer =
            lhs->getType()->isPointerType() ||
            (var != nullptr && var->getType()->isPointerType());

        if (containsGlobalStorage(lhs) &&
            (lhs_is_pointer || containsAllocationCall(rhs) ||
             containsTrackedStorage(rhs, state) || containsPointerToIntegerCast(rhs))) {
            markUnsupported(binary, "global-or-static-pointer-storage");
            return;
        }

        if (var != nullptr && var->getType()->isPointerType()) {''', 'global assignment')

replace('''        if (containsPointerToIntegerCast(rhs) &&
            containsTrackedStorage(rhs, state)) {''', '''        if (lhs->getType()->isAtomicType() && containsTrackedStorage(rhs, state)) {
            markUnsupported(binary, "atomic-pointer-storage");
        } else if (containsPointerToIntegerCast(rhs) &&
            containsTrackedStorage(rhs, state)) {''', 'atomic assignment')

replace('''        const Expr *ret = return_stmt.getRetValue();
        if (ret == nullptr) return;''', '''        const Expr *ret = return_stmt.getRetValue();
        if (ret == nullptr) return;
        if (containsGlobalStorage(ret)) {
            markUnsupported(return_stmt, "global-or-static-pointer-storage");
            return;
        }''', 'global return')

replace('''    void collectAllocationSites(const Stmt *root) {
        std::vector<const CallExpr *> sites;''', '''    void collectLoopAllocations(const Stmt *stmt, bool inside_loop) {
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
        std::vector<const CallExpr *> sites;''', 'collect loop allocations')

replace('''    std::map<const CallExpr *, unsigned> allocation_sites_;
    unsigned next_fallback_id_ = 1;
    std::set<unsigned> bound_objects_;''', '''    std::map<const CallExpr *, unsigned> allocation_sites_;
    std::map<const Expr *, unsigned> synthetic_allocation_sites_;
    std::set<const CallExpr *> loop_allocation_sites_;
    unsigned next_fallback_id_ = 1;
    std::set<unsigned> bound_objects_;''', 'analysis members')

# Insert atomic handling before asm handling in processStmt.
replace('''    if (isa<AsmStmt>(stmt)) {
        markUnsupported(*stmt, "inline-asm");''', '''    if (const auto *atomic = dyn_cast<clang::AtomicExpr>(stmt)) {
        if (atomic->getType()->isPointerType() ||
            containsTrackedStorage(atomic, state) || containsGlobalStorage(atomic)) {
            markUnsupported(*stmt, "atomic-pointer-storage");
        }
        return;
    }

    if (isa<AsmStmt>(stmt)) {
        markUnsupported(*stmt, "inline-asm");''', 'atomic expression')

path.write_text(s)

tests = {
    'tests/storage/global_cross_function_uaf_incomplete.c': '''#include <stdlib.h>\nstatic int *g;\nstatic void setup(void) { g = malloc(sizeof *g); *g = 1; free(g); }\nint main(void) { setup(); return *g; }\n''',
    'tests/storage/static_local_cross_call_incomplete.c': '''#include <stdlib.h>\nstatic int use_slot(int destroy) {\n    static int *p;\n    if (!p) { p = malloc(sizeof *p); *p = 1; }\n    if (destroy) free(p);\n    return p ? *p : 0;\n}\nint main(void) { (void)use_slot(1); return use_slot(0); }\n''',
    'tests/storage/loop_allocation_alias_escape_incomplete.c': '''#include <stdlib.h>\nint main(void) {\n    int *q = NULL;\n    int sum = 0;\n    for (int i = 0; i < 2; ++i) {\n        if (q) sum += *q;\n        int *p = malloc(sizeof *p);\n        *p = i;\n        q = p;\n        free(p);\n    }\n    return sum;\n}\n''',
    'tests/storage/nullable_alias_correlation_incomplete.c': '''#include <stdlib.h>\nint main(int flag) {\n    int *p = malloc(sizeof *p);\n    int *q = p;\n    *p = 1;\n    if (flag) { free(q); q = NULL; }\n    if (q) *q = 2;\n    if (!flag) free(p);\n    return 0;\n}\n''',
    'tests/storage/setjmp_nonlocal_incomplete.c': '''#include <setjmp.h>\n#include <stdlib.h>\nstatic jmp_buf env;\nint main(void) {\n    int *p = malloc(sizeof *p);\n    if (setjmp(env) == 0) { free(p); longjmp(env, 1); }\n    return 0;\n}\n''',
    'tests/storage/atomic_pointer_incomplete.c': '''#include <stdatomic.h>\n#include <stdlib.h>\nint main(void) {\n    int *p = malloc(sizeof *p);\n    *p = 1;\n    _Atomic(int *) slot = p;\n    free(p);\n    int *q = atomic_load(&slot);\n    return *q;\n}\n''',
}
for name, content in tests.items():
    Path(name).write_text(content)
