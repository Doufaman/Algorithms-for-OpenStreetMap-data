#include "fuzzy.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace geocoder {

// ============================================================
//  Damerau-Levenshtein distance
//
//  Full-table DP with adjacent-swap detection.  For the token
//  lengths seen in OSM data (typically 3–15 chars) this runs in
//  well under 1 µs so it is fine to call millions of times during
//  BK-tree construction.
// ============================================================
int damerau_levenshtein(const std::string& a, const std::string& b) {
    const int n = (int)a.size();
    const int m = (int)b.size();
    if (n == 0) return m;
    if (m == 0) return n;

    // Early-exit optimization: if lengths differ by more than the max
    // possible interesting distance, still compute — callers will
    // reject based on their own threshold — but avoid needless work
    // for very lopsided pairs.

    // Allocate DP grid on the stack for small strings, heap otherwise.
    // Small VLA-style avoidance: use vector, which stack-allocates
    // small buffers via SSO in modern libcxx... not guaranteed, but
    // acceptable.
    std::vector<int> flat((n + 1) * (m + 1));
    auto D = [&](int i, int j) -> int& { return flat[i * (m + 1) + j]; };

    for (int i = 0; i <= n; ++i) D(i, 0) = i;
    for (int j = 0; j <= m; ++j) D(0, j) = j;

    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int val  = std::min({
                D(i - 1, j)     + 1,     // deletion
                D(i,     j - 1) + 1,     // insertion
                D(i - 1, j - 1) + cost   // substitution / match
            });
            if (i >= 2 && j >= 2 &&
                a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                val = std::min(val, D(i - 2, j - 2) + 1);  // adjacent swap
            }
            D(i, j) = val;
        }
    }
    return D(n, m);
}

int fuzzy_threshold(std::size_t len) {
    if (len <= 4)  return 1;
    if (len <= 8)  return 2;
    return 2;      // longer tokens: 2 is enough for real typos, more = noise
}

// ============================================================
//  BK-tree
// ============================================================

void BKTree::reserve(std::size_t hint) {
    nodes_.reserve(hint);
}

void BKTree::insert(std::string word) {
    if (word.empty()) return;

    // First node becomes the root.
    if (nodes_.empty()) {
        Node n;
        n.word = std::move(word);
        nodes_.push_back(std::move(n));
        return;
    }

    // Walk from the root, choosing the child whose distance to `word`
    // matches the current node's distance. If no such child exists,
    // attach `word` as a new child.
    std::uint32_t cur = 0;
    while (true) {
        int d = damerau_levenshtein(word, nodes_[cur].word);
        if (d == 0) return;      // duplicate — ignore silently

        auto& children = nodes_[cur].children;
        bool descended = false;
        for (const auto& kv : children) {
            if (kv.first == d) {
                cur = kv.second;
                descended = true;
                break;
            }
        }
        if (descended) continue;

        // No child at this distance — install new leaf here.
        Node leaf;
        leaf.word = std::move(word);
        std::uint32_t new_idx = (std::uint32_t)nodes_.size();
        nodes_.push_back(std::move(leaf));
        nodes_[cur].children.emplace_back(d, new_idx);
        return;
    }
}

void BKTree::search(const std::string& query, int max_dist,
                    std::vector<Match>& out) const {
    if (nodes_.empty()) return;

    // Iterative DFS using an explicit stack (avoids recursion depth
    // problems on unbalanced trees).
    std::vector<std::uint32_t> stack;
    stack.reserve(32);
    stack.push_back(0);

    while (!stack.empty()) {
        std::uint32_t cur = stack.back();
        stack.pop_back();

        int d = damerau_levenshtein(query, nodes_[cur].word);
        if (d <= max_dist) out.push_back({d, nodes_[cur].word});

        // Triangle inequality: a child whose distance to `cur` is `cd`
        // has distance to `query` in [|d - cd|, d + cd]. Recurse only
        // when that range intersects [0, max_dist].
        int lo = d - max_dist;
        int hi = d + max_dist;
        for (const auto& kv : nodes_[cur].children) {
            if (kv.first >= lo && kv.first <= hi) stack.push_back(kv.second);
        }
    }
}

std::string BKTree::best_match(const std::string& query, int max_dist) const {
    std::vector<Match> matches;
    search(query, max_dist, matches);
    if (matches.empty()) return {};
    auto best = std::min_element(matches.begin(), matches.end(),
                    [](const Match& a, const Match& b) { return a.dist < b.dist; });
    return best->word;
}

} // namespace geocoder
