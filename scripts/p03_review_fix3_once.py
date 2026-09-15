from pathlib import Path

path = Path('src/cand.cpp')
s = path.read_text()

def replace(old: str, new: str, label: str) -> None:
    global s
    if old not in s:
        raise SystemExit(f'missing patch anchor: {label}')
    s = s.replace(old, new, 1)

replace('#include "clang/AST/Expr.h"\n', '#include "clang/AST/Expr.h"\n#include "clang/AST/Type.h"\n', 'Type include')

anchor = '''    bool containsGlobalStorage(const Expr *expr) const {'''
helpers = '''    bool typeMayContainPointer(clang::QualType type) const {
        if (type.isNull()) return false;
        type = type.getCanonicalType();
        if (type->isPointerType()) return true;
        if (const auto *atomic = type->getAs<clang::AtomicType>()) {
            return typeMayContainPointer(atomic->getValueType());
        }
        if (const auto *array = context_.getAsArrayType(type)) {
            return typeMayContainPointer(array->getElementType());
        }
        if (const auto *record_type = type->getAs<clang::RecordType>()) {
            const clang::RecordDecl *record = record_type->getDecl();
            if (record == nullptr || !record->isCompleteDefinition()) return false;
            for (const clang::FieldDecl *field : record->fields()) {
                if (typeMayContainPointer(field->getType())) return true;
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

    bool mayWritePointerStorage(const Expr *arg) const {
        if (arg == nullptr) return false;
        const Expr *stripped = arg->IgnoreParenCasts();
        if (const auto *unary = dyn_cast<UnaryOperator>(stripped)) {
            if (unary->getOpcode() == clang::UO_AddrOf) {
                return typeMayContainPointer(unary->getSubExpr()->getType());
            }
        }
        const clang::QualType arg_type = arg->getType();
        if (arg_type->isPointerType()) {
            return typeMayContainPointer(arg_type->getPointeeType());
        }
        return false;
    }

    bool containsUnknownAggregateOwnershipCall(const Expr *expr) const {
        if (expr == nullptr) return false;
        expr = expr->IgnoreParenCasts();
        if (const auto *call = dyn_cast<CallExpr>(expr)) {
            if (!call->getType()->isPointerType() &&
                typeMayContainPointer(call->getType())) {
                return true;
            }
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *child_expr = llvm::dyn_cast_or_null<Expr>(child)) {
                if (containsUnknownAggregateOwnershipCall(child_expr)) return true;
            }
        }
        return false;
    }

'''
replace(anchor, helpers + anchor, 'pointer transport helpers')

replace('''        if (asUnknownPointerCall(pointer_expr) != nullptr) {
            noteUnknownPointerCallIn(pointer_expr);
            return;
        }
        auto access_storage = storageFor(pointer_expr);
        const StorageBinding *binding = bindingFor(pointer_expr, state);''', '''        if (asUnknownPointerCall(pointer_expr) != nullptr) {
            noteUnknownPointerCallIn(pointer_expr);
            return;
        }
        if (containsUnknownAggregateOwnershipCall(pointer_expr)) {
            emitUnsupported({"unknown-aggregate-return-ownership", "",
                             location(access_loc)});
            return;
        }
        auto access_storage = storageFor(pointer_expr);
        const StorageBinding *binding = bindingFor(pointer_expr, state);''', 'aggregate call access')

replace('''        if (binding == nullptr) {
            return; // genuinely untracked storage: outside the current P0 heap scope
        }''', '''        if (binding == nullptr) {
            if (containsParameterStorage(pointer_expr)) {
                emitUnsupported({"unmodelled-pointer-parameter", "",
                                 location(access_loc)});
            }
            return; // genuinely untracked storage: outside the current P0 heap scope
        }''', 'parameter access')

replace('''        bool tracked_argument = false;
        bool global_argument = false;
        for (const Expr *arg : call.arguments()) {
            tracked_argument = tracked_argument || containsTrackedStorage(arg, state);
            global_argument = global_argument || containsGlobalStorage(arg);''', '''        bool tracked_argument = false;
        bool global_argument = false;
        bool pointer_output_argument = false;
        bool pointer_parameter_argument = false;
        for (const Expr *arg : call.arguments()) {
            tracked_argument = tracked_argument || containsTrackedStorage(arg, state);
            global_argument = global_argument || containsGlobalStorage(arg);
            pointer_output_argument = pointer_output_argument || mayWritePointerStorage(arg);
            pointer_parameter_argument = pointer_parameter_argument ||
                (containsParameterStorage(arg) && arg->getType()->isPointerType());''', 'call argument classes')

replace('''        if (global_argument) {
            markUnsupported(call, "global-or-static-pointer-storage");
        }
        if (tracked_argument) {''', '''        if (!call.getType()->isPointerType() &&
            typeMayContainPointer(call.getType())) {
            std::string kind = "unknown-aggregate-return-ownership";
            if (const FunctionDecl *callee = call.getDirectCallee()) {
                kind += ":" + callee->getNameAsString();
            }
            markUnsupported(call, kind);
        }
        if (global_argument) {
            markUnsupported(call, "global-or-static-pointer-storage");
        }
        if (pointer_output_argument) {
            markUnsupported(call, "unknown-call-with-pointer-output");
        }
        if (pointer_parameter_argument) {
            markUnsupported(call, "unknown-call-with-pointer-parameter");
        }
        if (tracked_argument) {''', 'call output/parameter')

replace('''        if (containsGlobalStorage(lhs) &&
            (lhs_is_pointer || containsAllocationCall(rhs) ||
             containsTrackedStorage(rhs, state) || containsPointerToIntegerCast(rhs))) {
            markUnsupported(binary, "global-or-static-pointer-storage");
            return;
        }

        if (var != nullptr && var->getType()->isPointerType()) {''', '''        if (containsGlobalStorage(lhs) &&
            (lhs_is_pointer || containsAllocationCall(rhs) ||
             containsTrackedStorage(rhs, state) || containsPointerToIntegerCast(rhs))) {
            markUnsupported(binary, "global-or-static-pointer-storage");
            return;
        }

        if (binary.isCompoundAssignmentOp() && lhs_is_pointer) {
            markUnsupported(binary, "pointer-arithmetic-reassignment");
            if (lhs_storage) {
                const auto it = state.storages.find(*lhs_storage);
                if (it != state.storages.end()) {
                    it->second = {kUnknownObjectId, PointerRelation::Unknown,
                                  location(binary.getExprLoc())};
                }
            }
            return;
        }

        if (var != nullptr && var->getType()->isPointerType()) {''', 'compound pointer mutation')

replace('''    if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
        if (unary->getOpcode() == clang::UO_Deref) {
            checkAccess(unary->getSubExpr(), unary->getOperatorLoc(), state);
        }
        recurseChildren(*unary, state, processed);
        return;
    }''', '''    if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
        if (unary->getOpcode() == clang::UO_Deref) {
            checkAccess(unary->getSubExpr(), unary->getOperatorLoc(), state);
        } else if ((unary->getOpcode() == clang::UO_PreInc ||
                    unary->getOpcode() == clang::UO_PostInc ||
                    unary->getOpcode() == clang::UO_PreDec ||
                    unary->getOpcode() == clang::UO_PostDec) &&
                   unary->getSubExpr()->getType()->isPointerType()) {
            markUnsupported(*unary, "pointer-arithmetic-reassignment");
            if (const auto storage = storageFor(unary->getSubExpr())) {
                const auto it = state.storages.find(*storage);
                if (it != state.storages.end()) {
                    it->second = {kUnknownObjectId, PointerRelation::Unknown,
                                  location(unary->getOperatorLoc())};
                }
            }
        }
        recurseChildren(*unary, state, processed);
        return;
    }''', 'unary pointer mutation')

path.write_text(s)

tests = {
    'tests/storage/compound_pointer_advance_incomplete.c': '''#include <stdlib.h>\nint main(void) { int *p = malloc(2 * sizeof *p); p += 1; free(p); return 0; }\n''',
    'tests/storage/unary_pointer_advance_incomplete.c': '''#include <stdlib.h>\nint main(void) { int *p = malloc(2 * sizeof *p); ++p; free(p); return 0; }\n''',
    'tests/storage/unknown_out_pointer_uaf_incomplete.c': '''#include <stdlib.h>\nstatic void make_value(int **out) { *out = malloc(sizeof **out); **out = 7; }\nint main(void) { int *p = NULL; make_value(&p); free(p); return *p; }\n''',
    'tests/storage/unknown_aggregate_return_uaf_incomplete.c': '''#include <stdlib.h>\nstruct S { int *p; };\nstatic struct S make_value(void) { struct S s = {0}; s.p = malloc(sizeof *s.p); *s.p = 4; return s; }\nint main(void) { struct S s = make_value(); free(s.p); return *s.p; }\n''',
    'tests/storage/pointer_parameter_access_incomplete.c': '''static int read_value(int *p) { return *p; }\nint main(void) { int x = 3; return read_value(&x) == 3 ? 0 : 1; }\n''',
    'tests/storage/struct_pointer_parameter_access_incomplete.c': '''struct S { int *p; };\nstatic int read_value(struct S s) { return *s.p; }\nint main(void) { int x = 3; struct S s = { &x }; return read_value(s) == 3 ? 0 : 1; }\n''',
}
for name, content in tests.items():
    Path(name).write_text(content)
