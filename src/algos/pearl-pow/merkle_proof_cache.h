#pragma once
#include "job.h"
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace om::pearl {
// Single-owner cache for one immutable matrix generation. The caller MUST clear
// before rewriting that matrix (including any unopened bytes), and on release.
// Hits recheck all requested chunks, not the entire current matrix. The caller
// must still compare the proof root/commitment against the winning device state.
// Not thread-safe: build/clear must share the device owner's serial host path.
class MerkleProofCache {
 public:
    static constexpr size_t maxTreeBytes = size_t{64} * 1024 * 1024;
    void clear() noexcept {
        std::vector<std::vector<uint8_t>>().swap(layers_);
        key_.fill(0); length_ = 0;
    }
    bool build(const uint8_t *data, size_t len, const uint8_t key[32],
               const std::vector<uint64_t> &leaves, MerkleProof *out,
               unsigned leafWorkers = 4, bool *cacheHit = nullptr) {
        if (cacheHit) *cacheHit = false;
        if (!data || !key || !out || !len || len % kChunkLen || leaves.empty() ||
            (leafWorkers != 1 && leafWorkers != 4)) return false;
        const size_t total = len / kChunkLen;
        if (leaves.back() >= total || leaves.size() > SIZE_MAX / kChunkLen) return false;
        for (size_t i = 1; i < leaves.size(); ++i)
            if (leaves[i-1] >= leaves[i]) return false;
        size_t treeBytes = 0;
        for (size_t count = total;; count = count / 2 + count % 2) {
            if (count > (maxTreeBytes - treeBytes) / 32) return false;
            treeBytes += count * 32;
            if (count == 1) break;
        }
        try {
            bool hit = length_ == len && !layers_.empty() &&
                       std::memcmp(key_.data(), key, 32) == 0;
            if (hit) {
                uint8_t cv[32];
                for (uint64_t leaf : leaves) {
                    b3::chunkCv(key, data + size_t(leaf) * kChunkLen,
                                kChunkLen, leaf, false, cv);
                    if (std::memcmp(cv, layers_[0].data() + size_t(leaf) * 32, 32)) {
                        hit = false; break;
                    }
                }
            }
            if (!hit) {
                clear(); // Never retain two trees during replacement.
                layers_.emplace_back(total * 32);
                auto hashRange = [&](size_t begin, size_t end) {
                    for (size_t c = begin; c < end; ++c)
                        b3::chunkCv(key, data + c * kChunkLen, kChunkLen, c,
                                    false, &layers_[0][c * 32]);
                };
                if (leafWorkers == 1) hashRange(0, total);
                else {
                    std::vector<std::thread> workers; workers.reserve(3);
                    try {
                        for (size_t i = 0; i < 3; ++i)
                            workers.emplace_back(hashRange, total * i / 4, total * (i+1) / 4);
                        hashRange(total * 3 / 4, total);
                    } catch (...) {
                        for (auto &worker : workers) if (worker.joinable()) worker.join();
                        throw;
                    }
                    for (auto &worker : workers) worker.join();
                }
                while (layers_.back().size() / 32 > 2) {
                    std::vector<uint8_t> next;
                    b3::combineLayer(key, layers_.back(), &next);
                    layers_.push_back(std::move(next));
                }
                if (layers_.back().size() / 32 == 2) {
                    std::vector<uint8_t> root(32);
                    b3::parentCv(key, &layers_.back()[0], &layers_.back()[32], true, root.data());
                    layers_.push_back(std::move(root));
                }
                std::memcpy(key_.data(), key, 32); length_ = len;
            }
            MerkleProof proof;
            proof.leafIndices = leaves; proof.totalLeaves = total;
            std::memcpy(proof.root, layers_.back().data(), 32);
            proof.leafData.resize(leaves.size() * kChunkLen);
            for (size_t i = 0; i < leaves.size(); ++i)
                std::memcpy(&proof.leafData[i*kChunkLen], data + size_t(leaves[i])*kChunkLen, kChunkLen);
            std::vector<uint64_t> current = leaves;
            size_t levelLen = total, level = 0;
            while (levelLen > 1 && !current.empty()) {
                const auto &nodes = layers_[level];
                for (size_t at = 0; at < current.size(); ++at) {
                    const uint64_t i = current[at];
                    const bool hasPrev = at > 0 && current[at-1] == i-1;
                    const bool hasNext = at+1 < current.size() && current[at+1] == i+1;
                    if ((i % 2 && !hasPrev) || (!(i % 2) && !hasNext && i+1 < levelLen)) {
                        const size_t sibling = size_t(i % 2 ? i-1 : i+1) * 32;
                        proof.siblings.insert(proof.siblings.end(), nodes.begin()+sibling, nodes.begin()+sibling+32);
                    }
                }
                std::vector<uint64_t> parents;
                for (uint64_t i : current) {
                    const uint64_t parent = i/2;
                    if (parents.empty() || parents.back() != parent) parents.push_back(parent);
                }
                current.swap(parents); levelLen = levelLen/2 + levelLen%2; ++level;
            }
            *out = std::move(proof);
            if (cacheHit) *cacheHit = hit;
            return true;
        } catch (...) {
            clear(); return false;
        }
    }
 private:
    std::vector<std::vector<uint8_t>> layers_;
    std::array<uint8_t,32> key_{};
    size_t length_ = 0;
};
} // namespace om::pearl

