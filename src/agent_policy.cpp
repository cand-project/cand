#include "agent_policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <sys/wait.h>

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/SHA256.h"

namespace cand {
namespace {

bool exactKeys(const llvm::json::Object &object,
               std::initializer_list<llvm::StringRef> expected) {
    if (object.size() != expected.size()) return false;
    for (llvm::StringRef key : expected) if (!object.get(key)) return false;
    return true;
}

bool readUnsigned(const llvm::json::Object *object, llvm::StringRef key,
                  unsigned &value) {
    if (!object) return false;
    const auto number = object->getInteger(key);
    if (!number || *number < 0 ||
        static_cast<std::uint64_t>(*number) > std::numeric_limits<unsigned>::max())
        return false;
    value = static_cast<unsigned>(*number);
    return true;
}

std::string quoteShell(const std::string &value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') out += "'\\''";
        else out += ch;
    }
    return out + "'";
}

bool validGitRef(const std::string &ref) {
    if (ref.empty() || ref.front() == '-' || ref.find("..") != std::string::npos ||
        ref.find("@{") != std::string::npos) return false;
    return std::all_of(ref.begin(), ref.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '/' ||
               c == '~' || c == '^' || c == '+' || c == '-';
    });
}

void addChange(PolicyDiff &diff, std::string kind, std::string before,
               std::string after, std::string classification) {
    diff.changes.push_back({std::move(kind), std::move(before), std::move(after),
                            classification});
    if (classification == "PROOF_WEAKENING") diff.weakened = true;
    if (classification == "REVIEW_REQUIRED") diff.review_required = true;
}

llvm::json::Array stringsJson(const std::vector<std::string> &strings) {
    llvm::json::Array out;
    for (const auto &string : strings) out.push_back(string);
    return out;
}

} // namespace

std::string sha256(const std::string &text) {
    llvm::SHA256 hasher;
    hasher.update(text);
    const auto digest = hasher.final();
    return llvm::toHex(llvm::ArrayRef<std::uint8_t>(digest), true);
}

bool sha256File(const std::string &path, std::string &digest, std::string &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "cannot read file: " + path; return false; }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) { error = "failed reading file: " + path; return false; }
    digest = sha256(contents.str());
    return true;
}

std::string normalizedRelativePath(const std::string &path) {
    if (path.empty() || path.front() == '/') return {};
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const std::string part = path.substr(start, end == std::string::npos
            ? std::string::npos : end - start);
        if (part == "..") return {};
        if (!part.empty() && part != ".") parts.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::string result;
    for (const auto &part : parts) {
        if (!result.empty()) result += '/';
        result += part;
    }
    return result;
}

bool loadPolicyText(const std::string &text, Policy &policy, std::string &error) {
    auto parsed = llvm::json::parse(text);
    if (!parsed) { error = "policy JSON parse error: " + llvm::toString(parsed.takeError()); return false; }
    const auto *root = parsed->getAsObject();
    if (!root || !exactKeys(*root, {"schema", "profile", "safety_level", "base_ref", "budgets",
            "unsupported", "contracts", "scope", "frontend"})) {
        error = "policy must use the exact cand.policy/v1 fields"; return false;
    }
    auto schema = root->getString("schema");
    auto profile = root->getString("profile");
    auto level = root->getString("safety_level");
    auto base_ref = root->getString("base_ref");
    const auto *budgets = root->getObject("budgets");
    const auto *unsupported = root->getObject("unsupported");
    const auto *contracts = root->getObject("contracts");
    const auto *scope = root->getObject("scope");
    const auto *frontend = root->getObject("frontend");
    if (!schema || *schema != "cand.policy/v1" || !profile || !level || !base_ref ||
        !budgets || !unsupported || !contracts || !scope || !frontend ||
        !exactKeys(*budgets, {"new_unsafe_boundaries", "new_suppressions",
            "safety_level_reductions", "checked_scope_decrease", "unsupported_scope_increase"}) ||
        !exactKeys(*unsupported, {"allow_in_verified_success"}) ||
        !exactKeys(*contracts, {"trusted_changes_require_review", "trusted"}) ||
        !exactKeys(*scope, {"files"}) || !exactKeys(*frontend, {"standard", "arguments"})) {
        error = "policy v1 has missing or unsupported fields"; return false;
    }
    Policy value;
    value.schema = schema->str();
    value.profile = profile->str();
    value.safety_level = level->str();
    value.base_ref = base_ref->str();
    if (!readUnsigned(budgets, "new_unsafe_boundaries", value.unsafe_budget) ||
        !readUnsigned(budgets, "new_suppressions", value.suppression_budget) ||
        !readUnsigned(budgets, "safety_level_reductions", value.level_reduction_budget) ||
        !readUnsigned(budgets, "checked_scope_decrease", value.scope_decrease_budget) ||
        !readUnsigned(budgets, "unsupported_scope_increase", value.unsupported_increase_budget)) {
        error = "policy budgets must be non-negative integers"; return false;
    }
    auto allow_unsupported = unsupported->getBoolean("allow_in_verified_success");
    auto contract_review = contracts->getBoolean("trusted_changes_require_review");
    auto standard = frontend->getString("standard");
    const auto *files = scope->getArray("files");
    const auto *pins = contracts->getArray("trusted");
    const auto *arguments = frontend->getArray("arguments");
    if (!allow_unsupported || !contract_review || !standard || !files || !pins || !arguments) {
        error = "policy fields have invalid types"; return false;
    }
    value.allow_unsupported = *allow_unsupported;
    value.contracts_review_required = *contract_review;
    value.frontend_standard = standard->str();
    for (const auto &entry : *arguments) {
        auto argument = entry.getAsString();
        if (!argument || argument->empty() || argument->starts_with("-std=")) {
            error = "frontend.arguments must contain nonempty strings other than -std";
            return false;
        }
        value.frontend_arguments.push_back(argument->str());
    }
    std::set<std::string> unique_files;
    for (const auto &entry : *files) {
        auto path = entry.getAsString();
        if (!path) { error = "scope.files entries must be strings"; return false; }
        std::string normalized = normalizedRelativePath(path->str());
        if (normalized.empty() || normalized != path->str() || !unique_files.insert(normalized).second) {
            error = "scope.files must contain unique normalized relative paths"; return false;
        }
        value.scope_files.push_back(std::move(normalized));
    }
    std::sort(value.scope_files.begin(), value.scope_files.end());
    std::set<std::string> unique_pins;
    for (const auto &entry : *pins) {
        const auto *pin = entry.getAsObject();
        if (!pin || !exactKeys(*pin, {"path", "sha256", "trust_class"})) {
            error = "trusted contract entries require path, sha256, trust_class"; return false;
        }
        auto path = pin->getString("path");
        auto digest = pin->getString("sha256");
        auto trust = pin->getString("trust_class");
        if (!path || !digest || !trust || normalizedRelativePath(path->str()) != path->str() ||
            digest->size() != 64 || !std::all_of(digest->begin(), digest->end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            }) || (*trust != "builtin" && *trust != "verified" && *trust != "reviewed" && *trust != "candidate") ||
            !unique_pins.insert(path->str()).second) {
            error = "invalid or duplicate trusted contract pin"; return false;
        }
        value.trusted_contracts.push_back({path->str(), digest->str(), trust->str()});
    }
    std::sort(value.trusted_contracts.begin(), value.trusted_contracts.end(),
        [](const auto &a, const auto &b) { return a.path < b.path; });
    value.sha256 = sha256(text);
    policy = std::move(value);
    return true;
}

bool loadPolicy(const std::string &path, Policy &policy, std::string &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "cannot read policy: " + path; return false; }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) { error = "failed reading policy: " + path; return false; }
    return loadPolicyText(contents.str(), policy, error);
}

bool isStrictGeneratedPolicy(const Policy &policy, std::string &reason) {
    if (policy.profile != "generated") reason = "profile must be generated";
    else if (policy.safety_level != "p0-temporal-lifecycle" &&
             policy.safety_level != "cand1") reason = "unsupported safety level";
    else if (policy.base_ref != "origin/main") reason = "generated profile requires the repository's origin/main trust base";
    else if (policy.unsafe_budget != 0 || policy.suppression_budget != 0 ||
             policy.level_reduction_budget != 0 || policy.scope_decrease_budget != 0 ||
             policy.unsupported_increase_budget != 0 || policy.allow_unsupported)
        reason = "generated profile requires zero weakening budgets and disallows unsupported success";
    else if (policy.frontend_standard != "c11") reason = "generated profile currently supports only c11";
    else if (policy.scope_files.empty()) reason = "generated profile requires an explicit non-empty checked scope";
    else return true;
    return false;
}

PolicyDiff comparePolicies(const Policy &before, const Policy &after) {
    PolicyDiff diff;
    const auto compareBudget = [&](const char *name, unsigned old_value, unsigned new_value) {
        if (old_value == new_value) return;
        addChange(diff, name, std::to_string(old_value), std::to_string(new_value),
                  new_value > old_value ? "PROOF_WEAKENING" : "PROOF_STRENGTHENING");
    };
    compareBudget("unsafe-budget-change", before.unsafe_budget, after.unsafe_budget);
    compareBudget("suppression-budget-change", before.suppression_budget, after.suppression_budget);
    compareBudget("safety-level-reduction-budget-change", before.level_reduction_budget, after.level_reduction_budget);
    compareBudget("checked-scope-decrease-budget-change", before.scope_decrease_budget, after.scope_decrease_budget);
    compareBudget("unsupported-scope-increase-budget-change", before.unsupported_increase_budget, after.unsupported_increase_budget);
    if (before.allow_unsupported != after.allow_unsupported)
        addChange(diff, "unsupported-policy-change", before.allow_unsupported ? "allowed" : "disallowed",
                  after.allow_unsupported ? "allowed" : "disallowed",
                  after.allow_unsupported ? "PROOF_WEAKENING" : "PROOF_STRENGTHENING");
    std::set<std::string> old_scope(before.scope_files.begin(), before.scope_files.end());
    std::set<std::string> new_scope(after.scope_files.begin(), after.scope_files.end());
    if (old_scope != new_scope) {
        const bool removed = std::any_of(old_scope.begin(), old_scope.end(), [&](const auto &p) { return !new_scope.count(p); });
        const bool added = std::any_of(new_scope.begin(), new_scope.end(), [&](const auto &p) { return !old_scope.count(p); });
        std::vector<std::string> old_list(old_scope.begin(), old_scope.end());
        std::vector<std::string> new_list(new_scope.begin(), new_scope.end());
        addChange(diff, "checked-scope-change", llvm::formatv("{0}", llvm::join(old_list, ",")).str(),
                  llvm::formatv("{0}", llvm::join(new_list, ",")).str(),
                  removed ? "PROOF_WEAKENING" : "PROOF_STRENGTHENING");
        if (removed && added) diff.review_required = true;
    }
    if (before.safety_level != after.safety_level)
        addChange(diff, "safety-level-change", before.safety_level, after.safety_level, "REVIEW_REQUIRED");
    if (before.base_ref != after.base_ref)
        addChange(diff, "base-ref-change", before.base_ref, after.base_ref, "REVIEW_REQUIRED");
    if (before.profile != after.profile)
        addChange(diff, "profile-change", before.profile, after.profile, "REVIEW_REQUIRED");
    if (before.frontend_standard != after.frontend_standard)
        addChange(diff, "frontend-standard-change", before.frontend_standard, after.frontend_standard, "REVIEW_REQUIRED");
    if (before.frontend_arguments != after.frontend_arguments)
        addChange(diff, "frontend-arguments-change", std::to_string(before.frontend_arguments.size()),
                  std::to_string(after.frontend_arguments.size()), "REVIEW_REQUIRED");
    if (before.contracts_review_required != after.contracts_review_required)
        addChange(diff, "contract-review-policy-change", before.contracts_review_required ? "true" : "false",
                  after.contracts_review_required ? "true" : "false", "REVIEW_REQUIRED");
    if (before.trusted_contracts.size() != after.trusted_contracts.size() ||
        !std::equal(before.trusted_contracts.begin(), before.trusted_contracts.end(),
                    after.trusted_contracts.begin(), after.trusted_contracts.end(),
                    [](const auto &a, const auto &b) { return a.path == b.path && a.sha256 == b.sha256 && a.trust_class == b.trust_class; })) {
        addChange(diff, "trusted-contract-set-change", std::to_string(before.trusted_contracts.size()),
                  std::to_string(after.trusted_contracts.size()), "REVIEW_REQUIRED");
    }
    if (diff.weakened) diff.classification = "PROOF_WEAKENING";
    else if (diff.review_required) diff.classification = "REVIEW_REQUIRED";
    else if (!diff.changes.empty()) diff.classification = "PROOF_STRENGTHENING";
    return diff;
}

bool loadPolicyAtRef(const std::string &ref, const std::string &path,
                     Policy &policy, std::string &error) {
    if (!validGitRef(ref) || normalizedRelativePath(path) != path) {
        error = "invalid git ref or policy path"; return false;
    }
    const std::string command = "git show " + quoteShell(ref + ":" + path) + " 2>/dev/null";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) { error = "cannot start git show"; return false; }
    std::string text;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) text += buffer.data();
    const int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "base policy not found at " + ref + ":" + path; return false;
    }
    return loadPolicyText(text, policy, error);
}

bool contractIsPinned(const Policy &policy, const std::string &path,
                      const std::string &digest, std::string &trust_class) {
    const std::string normalized = normalizedRelativePath(path);
    for (const auto &pin : policy.trusted_contracts) {
        if (pin.path == normalized && pin.sha256 == digest && pin.trust_class != "candidate") {
            trust_class = pin.trust_class;
            return true;
        }
    }
    return false;
}

llvm::json::Object policyJson(const Policy &policy) {
    llvm::json::Object root;
    root["schema"] = policy.schema;
    root["profile"] = policy.profile;
    root["safety_level"] = policy.safety_level;
    root["base_ref"] = policy.base_ref;
    llvm::json::Object budgets;
    budgets["new_unsafe_boundaries"] = static_cast<std::int64_t>(policy.unsafe_budget);
    budgets["new_suppressions"] = static_cast<std::int64_t>(policy.suppression_budget);
    budgets["safety_level_reductions"] = static_cast<std::int64_t>(policy.level_reduction_budget);
    budgets["checked_scope_decrease"] = static_cast<std::int64_t>(policy.scope_decrease_budget);
    budgets["unsupported_scope_increase"] = static_cast<std::int64_t>(policy.unsupported_increase_budget);
    root["budgets"] = std::move(budgets);
    root["allow_unsupported"] = policy.allow_unsupported;
    root["frontend_standard"] = policy.frontend_standard;
    root["frontend_arguments"] = stringsJson(policy.frontend_arguments);
    root["checked_files"] = stringsJson(policy.scope_files);
    llvm::json::Array contracts;
    for (const auto &pin : policy.trusted_contracts) {
        llvm::json::Object item;
        item["path"] = pin.path;
        item["sha256"] = pin.sha256;
        item["trust_class"] = pin.trust_class;
        contracts.push_back(std::move(item));
    }
    root["trusted_contracts"] = std::move(contracts);
    root["sha256"] = policy.sha256;
    return root;
}

llvm::json::Object PolicyDiff::toJson() const {
    llvm::json::Object root;
    root["schema"] = "cand.policy-diff/v1";
    root["classification"] = classification;
    root["result"] = weakened ? "fail" : (review_required ? "review_required" : "pass");
    llvm::json::Array list;
    for (const auto &change : changes) {
        llvm::json::Object item;
        item["kind"] = change.kind;
        item["before"] = change.before;
        item["after"] = change.after;
        item["classification"] = change.classification;
        list.push_back(std::move(item));
    }
    root["changes"] = std::move(list);
    return root;
}

} // namespace cand
