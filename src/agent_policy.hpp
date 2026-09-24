#pragma once

#include <optional>
#include <string>
#include <vector>

#include "llvm/Support/JSON.h"

namespace cand {

struct ContractPin {
    std::string path;
    std::string sha256;
    std::string trust_class;
};

struct Policy {
    std::string schema;
    std::string profile;
    std::string safety_level;
    std::string base_ref;
    unsigned unsafe_budget = 0;
    unsigned suppression_budget = 0;
    unsigned level_reduction_budget = 0;
    unsigned scope_decrease_budget = 0;
    unsigned unsupported_increase_budget = 0;
    bool allow_unsupported = false;
    bool contracts_review_required = true;
    std::string frontend_standard;
    std::vector<std::string> frontend_arguments;
    std::vector<std::string> scope_files;
    std::vector<ContractPin> trusted_contracts;
    std::string sha256;
    // #41: optional feature flags. features.pointer_output_contracts is
    // the authoritative agent-mode enablement of the bounded
    // produces_out_owner rule set (cand1 runs only); absent means off.
    std::optional<bool> pointer_output_contracts;
};

struct PolicyChange {
    std::string kind;
    std::string before;
    std::string after;
    std::string classification;
};

struct PolicyDiff {
    std::string classification = "NO_CHANGE";
    std::vector<PolicyChange> changes;
    bool weakened = false;
    bool review_required = false;
    llvm::json::Object toJson() const;
};

bool loadPolicy(const std::string &path, Policy &policy, std::string &error);
bool loadPolicyText(const std::string &text, Policy &policy, std::string &error);
std::string sha256(const std::string &text);
bool sha256File(const std::string &path, std::string &digest, std::string &error);
PolicyDiff comparePolicies(const Policy &before, const Policy &after);
bool loadPolicyAtRef(const std::string &ref, const std::string &path,
                     Policy &policy, std::string &error);
bool contractIsPinned(const Policy &policy, const std::string &path,
                      const std::string &digest, std::string &trust_class);
llvm::json::Object policyJson(const Policy &policy);
bool isStrictGeneratedPolicy(const Policy &policy, std::string &reason);
std::string normalizedRelativePath(const std::string &path);

} // namespace cand
