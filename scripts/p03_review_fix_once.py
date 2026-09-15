from pathlib import Path

path = Path('src/cand.cpp')
s = path.read_text()


def replace(old: str, new: str, label: str) -> None:
    global s
    if old not in s:
        raise SystemExit(f'missing patch anchor: {label}')
    s = s.replace(old, new, 1)


replace('// C& — P0.2 CFG-based flow-sensitive ownership analysis.',
        '// C& — P0.3 storage-identity and alias-aware temporal analysis.',
        'header')
replace('#include <map>\n', '#include <map>\n#include <limits>\n', 'limits include')
replace('''    // Findings are emitted during every worklist pass; the last emission for
    // a given site wins (states only move up the lattice, so "last" is the
    // most conservative and deterministic result for that program point).
''', '''    // Findings are collected only during the post-convergence emission pass.
    // The map de-duplicates any repeated observation of the same program point.
''', 'collector comment')

replace('''enum class PointerRelation { Owner, Alias, Unknown };

struct ObjectInfo {''', '''enum class PointerRelation { Owner, Alias, Null, Unknown };

constexpr unsigned kNullObjectId = 0;
constexpr unsigned kUnknownObjectId = std::numeric_limits<unsigned>::max();

struct ObjectInfo {''', 'pointer relation')

replace('''struct StorageBinding {
    unsigned object_id = 0;
    PointerRelation relation = PointerRelation::Unknown;
    bool operator==(const StorageBinding &other) const {
        return object_id == other.object_id && relation == other.relation;
    }
};''', '''struct StorageBinding {
    // 0 is an explicit, definitely-null storage value. UINT_MAX is an
    // unresolved/ambiguous target and must never be interpreted as NULL.
    unsigned object_id = kUnknownObjectId;
    PointerRelation relation = PointerRelation::Unknown;
    Location relation_location;
    bool operator==(const StorageBinding &other) const {
        return object_id == other.object_id && relation == other.relation &&
               sameLocation(relation_location, other.relation_location);
    }
};''', 'storage binding')

replace('''StorageBinding joinBinding(const StorageBinding &a, const StorageBinding &b) {
    if (a.object_id == b.object_id) {
        return {a.object_id, a.relation == b.relation ? a.relation : PointerRelation::Unknown};
    }
    if (a.object_id == 0) return {b.object_id, PointerRelation::Unknown};
    if (b.object_id == 0) return {a.object_id, PointerRelation::Unknown};
    return {0, PointerRelation::Unknown};
}''', '''StorageBinding joinBinding(const StorageBinding &a, const StorageBinding &b) {
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
        return {a.object_id, PointerRelation::Unknown, relation_location};
    }
    if (unknown(a) || unknown(b)) {
        return {kUnknownObjectId, PointerRelation::Unknown, relation_location};
    }
    // Null on one path and one known object on the other still has one heap
    // target for temporal purposes. Null-dereference safety is outside P0.
    if (known_null(a) && b.object_id != kNullObjectId) {
        return {b.object_id, b.relation, relation_location};
    }
    if (known_null(b) && a.object_id != kNullObjectId) {
        return {a.object_id, a.relation, relation_location};
    }
    // Two different non-null objects are an unresolved alias target, never NULL.
    return {kUnknownObjectId, PointerRelation::Unknown, relation_location};
}''', 'joinBinding')

anchor = '''    std::optional<StorageId> storageFor(const Expr *expr) const {'''
provenance = '''    bool containsPointerToIntegerCast(const Expr *expr) const {
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

'''
replace(anchor, provenance + anchor, 'provenance helper')

replace('''        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (member->isArrow()) return std::nullopt;
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
            result.index = static_cast<int>(literal->getValue().getSExtValue());
            return result;
        }''', '''        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
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
        }''', 'storageFor member/array')

replace('''    const ObjectInfo *objectFor(const StorageBinding *binding, const FlowState &state) const {
        if (binding == nullptr || binding->object_id == 0) return nullptr;
        const auto it = state.objects.find(binding->object_id);
        return it == state.objects.end() ? nullptr : &it->second;
    }''', '''    const ObjectInfo *objectFor(const StorageBinding *binding, const FlowState &state) const {
        if (binding == nullptr || binding->object_id == kNullObjectId ||
            binding->object_id == kUnknownObjectId) {
            return nullptr;
        }
        const auto it = state.objects.find(binding->object_id);
        return it == state.objects.end() ? nullptr : &it->second;
    }''', 'objectFor')

replace('''        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            return bindingFor(expr, state) != nullptr;
        }
        if (storageFor(expr)) return state.storages.find(*storageFor(expr)) != state.storages.end();''', '''        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
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
        if (storageFor(expr)) return state.storages.find(*storageFor(expr)) != state.storages.end();''', 'containsTrackedStorage')

replace('''        if (isa<MemberExpr>(expr)) {
            return "struct-member";
        }''', '''        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (const auto *field = dyn_cast<clang::FieldDecl>(member->getMemberDecl())) {
                if (field->getParent() != nullptr && field->getParent()->isUnion()) {
                    return "union-member-storage";
                }
            }
            return "struct-member";
        }''', 'untracked member kind')

replace('''        if (binding == nullptr) {
            return; // untracked storage: not an additional obligation here
        }
        const ObjectInfo *object = objectFor(binding, state);
        if (object == nullptr) return;
        if (object->state == ObjectState::Dead || object->state == ObjectState::MaybeDead) {
            if (access_storage) reportUseAfterDestroy(*binding, *object, *access_storage,
                                                      access_loc, state);''', '''        if (binding == nullptr) {
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
                reportUseAfterDestroy(*binding, *object, *access_storage, access_loc, state);
            } else {
                emitUnsupported({"unresolved-access-storage", "", location(access_loc)});
            }''', 'checkAccess fail closed')

replace('''        StorageBinding &binding = it->second;
        ObjectInfo *object = binding.object_id == 0 ? nullptr : &state.objects[binding.object_id];
        if (binding.object_id == 0) return; // NULL storage
        if (object == nullptr) {
            emitUnsupported({"free-unknown-ownership-state", "", location(call.getExprLoc())});
            return;
        }''', '''        StorageBinding &binding = it->second;
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
        ObjectInfo *object = &object_it->second;''', 'handleFree ambiguous')

replace('''        state.storages[storage] = {object_id, PointerRelation::Owner};
        auto &object = state.objects[object_id];''', '''        state.storages[storage] =
            {object_id, PointerRelation::Owner, location(init->getExprLoc())};
        auto &object = state.objects[object_id];''', 'bind allocation origin')

replace('''            if (!var->getType()->isPointerType()) {
                if (containsAllocationCall(init)) {
                    emitUnsupported(
                        {"allocation-to-untracked-storage:initializer", "",
                         location(decl_stmt.getBeginLoc())});
                } else if (containsUnknownPointerCall(init)) {
                    checkPointerValueSource(init);
                }
                continue;
            }''', '''            if (!var->getType()->isPointerType()) {
                if (containsAllocationCall(init)) {
                    emitUnsupported(
                        {"allocation-to-untracked-storage:initializer", "",
                         location(decl_stmt.getBeginLoc())});
                } else if (containsPointerToIntegerCast(init) &&
                           containsTrackedStorage(init, state)) {
                    markUnsupported(decl_stmt, "pointer-integer-provenance");
                } else if (var->getType()->isRecordType() &&
                           containsTrackedStorage(init, state)) {
                    markUnsupported(decl_stmt, "aggregate-copy-with-tracked-pointer");
                } else if (containsUnknownPointerCall(init)) {
                    checkPointerValueSource(init);
                }
                continue;
            }''', 'nonpointer declaration')

replace('''                state.storages[storage] = {0, PointerRelation::Unknown};
                state.objects[0].state = ObjectState::Null;''', '''                state.storages[storage] =
                    {kNullObjectId, PointerRelation::Null, location(init->getExprLoc())};''', 'decl null')

replace('''                if (it == state.storages.end() || it->second.object_id == 0) {
                    markUnsupported(decl_stmt, "ambiguous-alias-target");
                } else {
                    state.storages[storage] = {it->second.object_id, PointerRelation::Alias};
                }''', '''                if (it == state.storages.end() ||
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
                }''', 'decl alias')

replace('''                if (lhs_storage) state.storages[*lhs_storage] = {0, PointerRelation::Unknown};
                state.objects[0].state = ObjectState::Null;''', '''                if (lhs_storage) {
                    state.storages[*lhs_storage] =
                        {kNullObjectId, PointerRelation::Null, location(binary.getExprLoc())};
                }''', 'assignment null')

replace('''                    if (source_it == state.storages.end() || source_it->second.object_id == 0) {
                        markUnsupported(binary, "ambiguous-alias-target");
                    } else if (lhs_storage) {
                        state.storages[*lhs_storage] = {source_it->second.object_id,
                                                        PointerRelation::Alias};
                    }''', '''                    if (source_it == state.storages.end() ||
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
                    }''', 'local alias assignment')

replace('''                if (isNullConstant(rhs)) {
                    state.storages[*lhs_storage] = {0, PointerRelation::Unknown};
                } else {
                    bindAllocation(*lhs_storage, rhs, state);
                }''', '''                if (isNullConstant(rhs)) {
                    state.storages[*lhs_storage] =
                        {kNullObjectId, PointerRelation::Null,
                         location(binary.getExprLoc())};
                } else {
                    bindAllocation(*lhs_storage, rhs, state);
                }''', 'field null assignment')

replace('''                if (source_it == state.storages.end() || source_it->second.object_id == 0) {
                    markUnsupported(binary, "ambiguous-alias-target");
                } else {
                    state.storages[*lhs_storage] = {source_it->second.object_id,
                                                    PointerRelation::Alias};
                }''', '''                if (source_it == state.storages.end() ||
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
                }''', 'field alias assignment')

replace('''        if (containsAllocationCall(rhs) && !containsTrackedStorage(rhs, state)) {
            emitUnsupported({"allocation-to-untracked-storage:initializer",
                                       "", location(binary.getExprLoc())});
        }''', '''        if (containsPointerToIntegerCast(rhs) &&
            containsTrackedStorage(rhs, state)) {
            markUnsupported(binary, "pointer-integer-provenance");
        } else if (lhs->getType()->isRecordType() &&
                   containsTrackedStorage(rhs, state)) {
            markUnsupported(binary, "aggregate-copy-with-tracked-pointer");
        } else if (containsAllocationCall(rhs) && !containsTrackedStorage(rhs, state)) {
            emitUnsupported({"allocation-to-untracked-storage:initializer",
                                       "", location(binary.getExprLoc())});
        }''', 'nonpointer assignment')

replace('''        const Expr *ret = return_stmt.getRetValue();
        if (ret == nullptr || !ret->getType()->isPointerType()) {
            return;
        }
        if (containsTrackedStorage(ret, state)) {''', '''        const Expr *ret = return_stmt.getRetValue();
        if (ret == nullptr) return;
        if (!ret->getType()->isPointerType()) {
            if (containsPointerToIntegerCast(ret) && containsTrackedStorage(ret, state)) {
                markUnsupported(return_stmt, "pointer-integer-provenance");
            } else if (ret->getType()->isRecordType() &&
                       containsTrackedStorage(ret, state)) {
                markUnsupported(return_stmt, "aggregate-return-with-tracked-pointer");
            }
            return;
        }
        if (containsTrackedStorage(ret, state)) {''', 'aggregate return')

replace('''                finding.trace.push_back({"alias_created", "Alias", object.allocation});''', '''                const Location alias_location = entry.second.relation_location.file.empty()
                                                    ? object.allocation
                                                    : entry.second.relation_location;
                finding.trace.push_back({"alias_created", "Alias", alias_location});''', 'alias trace location')

path.write_text(s)


tests = {
    'tests/storage/ambiguous_alias_branch_incomplete.c': '''#include <stdlib.h>\nint main(int argc, char **argv) {\n    (void)argv;\n    int *a = malloc(sizeof *a);\n    int *b = malloc(sizeof *b);\n    int *q;\n    if (argc > 1) q = a; else q = b;\n    free(a);\n    int value = *q;\n    free(b);\n    return value;\n}\n''',
    'tests/storage/ambiguous_alias_free_incomplete.c': '''#include <stdlib.h>\nint main(int argc, char **argv) {\n    (void)argv;\n    int *a = malloc(sizeof *a);\n    int *b = malloc(sizeof *b);\n    int *q;\n    if (argc > 1) q = a; else q = b;\n    free(q);\n    return 0;\n}\n''',
    'tests/storage/aggregate_copy_incomplete.c': '''#include <stdlib.h>\nstruct S { int *p; };\nint main(void) {\n    struct S a = {0};\n    struct S b = {0};\n    a.p = malloc(sizeof *a.p);\n    *a.p = 42;\n    b = a;\n    free(a.p);\n    return *b.p;\n}\n''',
    'tests/storage/aggregate_initializer_copy_incomplete.c': '''#include <stdlib.h>\nstruct S { int *p; };\nint main(void) {\n    int *p = malloc(sizeof *p);\n    *p = 7;\n    struct S b = { .p = p };\n    free(p);\n    return *b.p;\n}\n''',
    'tests/storage/aggregate_memcpy_incomplete.c': '''#include <stdlib.h>\n#include <string.h>\nstruct S { int *p; };\nint main(void) {\n    struct S a = {0}, b = {0};\n    a.p = malloc(sizeof *a.p);\n    *a.p = 9;\n    memcpy(&b, &a, sizeof a);\n    free(a.p);\n    return *b.p;\n}\n''',
    'tests/storage/aggregate_return_incomplete.c': '''#include <stdlib.h>\nstruct S { int *p; };\nstatic struct S make(void) {\n    struct S s = {0};\n    s.p = malloc(sizeof *s.p);\n    return s;\n}\nint main(void) { struct S s = make(); free(s.p); return 0; }\n''',
    'tests/storage/union_member_incomplete.c': '''#include <stdlib.h>\nunion U { int *p; int *q; };\nint main(void) {\n    union U u = {0};\n    u.p = malloc(sizeof *u.p);\n    *u.p = 3;\n    free(u.p);\n    return *u.q;\n}\n''',
    'tests/storage/multidim_array_distinct_safe.c': '''#include <stdlib.h>\nint main(void) {\n    int *a[2][2] = {{0}};\n    a[0][0] = malloc(sizeof *a[0][0]);\n    a[1][0] = malloc(sizeof *a[1][0]);\n    *a[0][0] = 1;\n    *a[1][0] = 2;\n    free(a[0][0]);\n    int value = *a[1][0];\n    free(a[1][0]);\n    return value == 2 ? 0 : 1;\n}\n''',
    'tests/storage/multidim_array_uaf.c': '''#include <stdlib.h>\nint main(void) {\n    int *a[2][2] = {{0}};\n    a[0][0] = malloc(sizeof *a[0][0]);\n    a[1][0] = malloc(sizeof *a[1][0]);\n    free(a[0][0]);\n    int value = *a[0][0];\n    free(a[1][0]);\n    return value;\n}\n''',
    'tests/storage/pointer_integer_provenance_incomplete.c': '''#include <stdint.h>\n#include <stdlib.h>\nint main(void) {\n    int *p = malloc(sizeof *p);\n    uintptr_t raw = (uintptr_t)p;\n    free(p);\n    return *(int *)raw;\n}\n''',
}
for name, content in tests.items():
    Path(name).write_text(content)

Path('tests/storage/run.sh').write_text('''#!/usr/bin/env bash
set -euo pipefail
cand="${1:?cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
for source in tests/storage/*.c; do
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$work/cand.json" 2>"$work/cand.err"
  rc=$?
  set -e
  result="$(python3 - "$work/cand.json" <<'PYJSON'
import json, sys
try:
    print(json.load(open(sys.argv[1], encoding='utf-8')).get('result', ''))
except Exception:
    print('')
PYJSON
)"
  base="$(basename "$source")"
  case "$base" in
    *incomplete*) [[ "$rc" == 3 && "$result" == incomplete ]] || { echo "expected incomplete: $source"; cat "$work/cand.json"; cat "$work/cand.err"; exit 1; } ;;
    *uaf*|*double_free*) [[ "$rc" == 1 && "$result" == fail ]] || { echo "expected fail: $source"; cat "$work/cand.json"; cat "$work/cand.err"; exit 1; } ;;
    *) [[ "$rc" == 0 && "$result" == pass ]] || { echo "expected pass: $source"; cat "$work/cand.json"; cat "$work/cand.err"; exit 1; } ;;
  esac

  if ! clang -std=c11 -g -fsanitize=address "$source" -o "$work/a.out" >"$work/clang.out" 2>&1; then
    echo "fixture failed to compile with ASan: $source"; cat "$work/clang.out"; exit 1
  fi
  set +e
  "$work/a.out" >"$work/asan" 2>&1
  asan_rc=$?
  set -e
  if [[ "$base" == *uaf* || "$base" == *double_free* ]]; then
    grep -q "ERROR: AddressSanitizer" "$work/asan" || { echo "ASan did not confirm expected violation: $source (rc=$asan_rc)"; cat "$work/asan"; exit 1; }
  elif [[ "$base" != *incomplete* ]]; then
    if grep -q "ERROR: AddressSanitizer" "$work/asan" || [[ "$asan_rc" != 0 ]]; then
      echo "safe fixture failed under ASan: $source"; cat "$work/asan"; exit 1
    fi
  fi
done
echo "C& P0.3 storage tests passed."
''')
