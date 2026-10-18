#pragma once
#include "job.h"

namespace om::pearl {
// Win-only opening of a balanced GPU tree. Layers occupy consecutive CVs,
// starting with leaves; the final ROOT output is reconstructed on the host.
// This storage belongs to proof construction, never the attempt scratch buffer.
struct CompactProofPlan {
    static constexpr uint64_t maxLeaves = uint64_t{1} << 20;
    static constexpr size_t maxSiblings = 64;
    uint64_t total = 0, first = 0, count = 0;
    std::vector<uint64_t> siblingNodes;
    size_t leafBytes() const { return size_t(count) * kChunkLen; }
    size_t openingBytes() const { return leafBytes() + siblingNodes.size() * 32; }
};

inline bool compactProofPlan(uint64_t total, uint64_t first, uint64_t count,
                             CompactProofPlan *out) {
    if (!out || total < 2 || total > CompactProofPlan::maxLeaves ||
        (total & (total - 1)) || !count || count > kMaxDenseProofOpenedLeaves ||
        first >= total || count > total - first) return false;
    try {
        CompactProofPlan p; p.total=total; p.first=first; p.count=count;
        uint64_t lo=first, hi=first+count-1, base=0;
        for (uint64_t width=total; width>1; width/=2) {
            if (lo & 1) p.siblingNodes.push_back(base+lo-1);
            if (!(hi & 1) && hi+1<width) p.siblingNodes.push_back(base+hi+1);
            base+=width;lo/=2;hi/=2;
        }
        if (p.siblingNodes.size()>CompactProofPlan::maxSiblings) return false;
        *out=std::move(p);return true;
    } catch (const std::bad_alloc &) { return false; }
      catch (const std::length_error &) { return false; }
}

// Rehash every opened chunk using its global index, then independently compute
// all opened-path parents, including the final ROOT flag. The caller compares
// this root and its derived commitments to the immutable winning attempt.
// No device-provided leaf CV or root is trusted by this reconstruction.
inline bool reconstructCompactProof(const CompactProofPlan &plan,
                                     const uint8_t *opening, size_t bytes,
                                     const uint8_t key[32], MerkleProof *out) {
    CompactProofPlan expected;
    if (!opening || !key || !out ||
        !compactProofPlan(plan.total,plan.first,plan.count,&expected) ||
        plan.siblingNodes!=expected.siblingNodes || bytes!=expected.openingBytes())
        return false;
    try {
        MerkleProof proof;proof.totalLeaves=plan.total;
        proof.leafData.assign(opening,opening+plan.leafBytes());
        proof.siblings.assign(opening+plan.leafBytes(),opening+bytes);
        std::vector<std::array<uint8_t,32>> current(size_t(plan.count));
        for (size_t i=0;i<size_t(plan.count);++i) {
            proof.leafIndices.push_back(plan.first+i);
            b3::chunkCv(key,opening+i*kChunkLen,kChunkLen,plan.first+i,false,current[i].data());
        }
        size_t cursor=0;uint64_t lo=plan.first,hi=lo+plan.count-1;
        for (uint64_t width=plan.total; width>1; width/=2) {
            auto sibling=[&]() {
                std::array<uint8_t,32> value;
                memcpy(value.data(),proof.siblings.data()+cursor,32);cursor+=32;
                return value;
            };
            // The validated plan guarantees exactly these siblings and no more.
            if (lo & 1) current.insert(current.begin(),sibling());
            if (!(hi & 1) && hi+1<width) current.push_back(sibling());
            if (current.size()%2) return false;
            std::vector<std::array<uint8_t,32>> parents(current.size()/2);
            for (size_t i=0;i<parents.size();++i)
                b3::parentCv(key,current[2*i].data(),current[2*i+1].data(),width==2,parents[i].data());
            current.swap(parents);lo/=2;hi/=2;
        }
        if (current.size()!=1 || lo || hi || cursor!=proof.siblings.size()) return false;
        memcpy(proof.root,current[0].data(),32);
        *out=std::move(proof);return true;
    } catch (const std::bad_alloc &) { return false; }
      catch (const std::length_error &) { return false; }
}
} // namespace om::pearl
