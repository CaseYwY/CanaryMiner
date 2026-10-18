#include <stdint.h>
#include <stdio.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "../src/algos/pearl-pow/job.h"

using namespace om::pearl;

namespace {

int failures = 0;

void check(const char *name, bool ok) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

size_t siblingCount(uint64_t totalLeaves, uint64_t first, uint64_t last) {
    size_t count = 0;
    while (totalLeaves > 1) {
        if (first & 1u) count++;
        if ((last & 1u) == 0 && last + 1 < totalLeaves) count++;
        first /= 2;
        last /= 2;
        totalLeaves = totalLeaves / 2 + totalLeaves % 2;
    }
    return count;
}

MerkleProof fakeProof(uint64_t totalLeaves,
                      const std::vector<uint64_t> &leaves, uint8_t seed) {
    MerkleProof proof;
    proof.totalLeaves = totalLeaves;
    proof.leafIndices = leaves;
    proof.leafData.resize(leaves.size() * kChunkLen);
    for (size_t i = 0; i < proof.leafData.size(); i++)
        proof.leafData[i] = (uint8_t)(seed + i * 29u);
    for (size_t i = 0; i < sizeof(proof.root); i++)
        proof.root[i] = (uint8_t)(seed ^ (uint8_t)(i * 7u));
    if (!leaves.empty()) {
        proof.siblings.resize(
            siblingCount(totalLeaves, leaves.front(), leaves.back()) * kDigestLen);
        for (size_t i = 0; i < proof.siblings.size(); i++)
            proof.siblings[i] = (uint8_t)(seed + 3u + i * 11u);
    }
    return proof;
}

MerkleProof proofForRows(uint64_t matrixRows, uint64_t width,
                         uint64_t firstRow, uint64_t rowCount, uint8_t seed) {
    std::vector<uint64_t> leaves;
    check("test fixture row arithmetic is valid",
          leafIndicesFromRowsChecked(firstRow, rowCount, width, &leaves));
    const uint64_t bytes = matrixRows * width;
    const uint64_t totalLeaves = (bytes - 1) / kChunkLen + 1;
    return fakeProof(totalLeaves, leaves, seed);
}

MerkleProof builtProofForRows(uint64_t matrixRows, uint64_t width,
                              uint64_t firstRow, uint64_t rowCount,
                              uint8_t seed) {
    std::vector<uint64_t> leaves;
    check("real proof fixture row arithmetic is valid",
          leafIndicesFromRowsChecked(firstRow, rowCount, width, &leaves));
    const size_t bytes = (size_t)(matrixRows * width);
    const size_t padded = (bytes + kChunkLen - 1) / kChunkLen * kChunkLen;
    std::vector<uint8_t> data(padded, 0);
    for (size_t i = 0; i < bytes; i++) data[i] = (uint8_t)(seed + i * 17u);
    uint8_t key[32];
    for (size_t i = 0; i < sizeof(key); i++) key[i] = (uint8_t)(seed ^ i * 13u);
    MerkleProof proof;
    check("real buildProof fixture succeeds",
          buildProof(data.data(), data.size(), key, leaves, &proof));
    return proof;
}

void rawPutU64(std::vector<uint8_t> *out, uint64_t value) {
    for (int i = 0; i < 8; i++) out->push_back((uint8_t)(value >> (i * 8)));
}

void rawPutProof(std::vector<uint8_t> *out, const MerkleProof &proof) {
    rawPutU64(out, proof.leafIndices.size());
    for (size_t i = 0; i < proof.leafIndices.size(); i++) {
        rawPutU64(out, kChunkLen);
        out->insert(out->end(), proof.leafData.begin() + i * kChunkLen,
                    proof.leafData.begin() + (i + 1) * kChunkLen);
    }
    rawPutU64(out, proof.leafIndices.size());
    for (uint64_t index : proof.leafIndices) rawPutU64(out, index);
    rawPutU64(out, proof.totalLeaves);
    out->insert(out->end(), proof.root, proof.root + sizeof(proof.root));
    rawPutU64(out, proof.siblings.size() / kDigestLen);
    out->insert(out->end(), proof.siblings.begin(), proof.siblings.end());
}

std::vector<uint8_t> legacyEncoding(uint64_t m, uint64_t n, uint64_t k,
                                    uint64_t rank, const MerkleProof &a,
                                    uint64_t aFirst, uint64_t aRows,
                                    const MerkleProof &bt, uint64_t bFirst,
                                    uint64_t bCols) {
    std::vector<uint8_t> out;
    rawPutU64(&out, m);
    rawPutU64(&out, n);
    rawPutU64(&out, k);
    rawPutU64(&out, rank);
    rawPutProof(&out, a);
    rawPutU64(&out, aRows);
    for (uint64_t i = 0; i < aRows; i++) rawPutU64(&out, aFirst + i);
    rawPutProof(&out, bt);
    rawPutU64(&out, bCols);
    for (uint64_t i = 0; i < bCols; i++) rawPutU64(&out, bFirst + i);
    out.push_back(0);
    return out;
}

bool failsWith(const DenseProofEncodeResult &result, DenseProofEncodeError error) {
    return !result.ok() && result.error == error && result.bytes.empty();
}

}  // namespace

int main() {
    struct ErrorNameCase {
        DenseProofEncodeError error;
        const char *name;
    };
    static const ErrorNameCase errorNames[] = {
        {DenseProofEncodeError::None, "none"},
        {DenseProofEncodeError::DimensionNotRepresentable,
         "dimension-not-representable"},
        {DenseProofEncodeError::RankNotRepresentable,
         "rank-not-representable"},
        {DenseProofEncodeError::InvalidDimension, "invalid-dimension"},
        {DenseProofEncodeError::InvalidRank, "invalid-rank"},
        {DenseProofEncodeError::ArithmeticOverflow, "arithmetic-overflow"},
        {DenseProofEncodeError::ResourceLimit, "resource-limit"},
        {DenseProofEncodeError::AllocationFailure, "allocation-failure"},
        {DenseProofEncodeError::RowRangeInvalid, "row-range-invalid"},
        {DenseProofEncodeError::EmptyProof, "empty-proof"},
        {DenseProofEncodeError::TotalLeavesMismatch,
         "total-leaves-mismatch"},
        {DenseProofEncodeError::LeafIndexCountMismatch,
         "leaf-index-count-mismatch"},
        {DenseProofEncodeError::LeafIndexMismatch, "leaf-index-mismatch"},
        {DenseProofEncodeError::LeafDataSizeMismatch,
         "leaf-data-size-mismatch"},
        {DenseProofEncodeError::SiblingLengthMismatch,
         "sibling-length-mismatch"},
        {DenseProofEncodeError::SiblingPathMismatch,
         "sibling-path-mismatch"},
    };
    for (const ErrorNameCase &entry : errorNames)
        check("every checked-encoder failure has stable diagnostics",
              std::strcmp(denseProofEncodeErrorName(entry.error),
                          entry.name) == 0);
    check("unknown checked-encoder failures stay diagnosable",
          std::strcmp(denseProofEncodeErrorName(
                          static_cast<DenseProofEncodeError>(255)),
                      "unknown") == 0);

    constexpr uint64_t m = 128, n = 128, k = 2048, rank = 128;
    constexpr uint64_t first = 16, rows = 16;
    const MerkleProof a = proofForRows(m, k, first, rows, 0x21);
    const MerkleProof bt = proofForRows(n, k, first, rows, 0x83);

    const DenseProofEncodeResult valid =
        encodePlainProofChecked(m, n, k, rank, a, first, rows, bt, first, rows);
    check("fixed production shape passes the checked encoder", valid.ok());
    const std::vector<uint8_t> legacy =
        legacyEncoding(m, n, k, rank, a, first, rows, bt, first, rows);
    check("checked success bytes equal the pre-check serializer byte-for-byte",
          valid.bytes == legacy);
    check("compatibility entry point returns the same success bytes",
          encodePlainProof(m, n, k, rank, a, first, rows, bt, first, rows) ==
              legacy);

    uint8_t goldenDigest[32];
    b3::hash(nullptr, valid.bytes.data(), valid.bytes.size(), goldenDigest);
    static const uint8_t expectedGoldenDigest[32] = {
        0x42, 0x7b, 0xa3, 0x55, 0x0c, 0x73, 0x1a, 0xfa,
        0xf3, 0x55, 0x5b, 0xbc, 0x52, 0x8c, 0x5b, 0xc5,
        0x60, 0xf6, 0xf1, 0xbf, 0xbc, 0xb2, 0xd8, 0xe0,
        0x2d, 0x73, 0x34, 0x57, 0xbc, 0xc4, 0x9d, 0x54,
    };
    check("fixed production-shape proof bytes match the frozen golden digest",
          valid.bytes.size() == 67185 &&
              memcmp(goldenDigest, expectedGoldenDigest, 32) == 0);

    check("m wider than u32 fails closed",
          failsWith(encodePlainProofChecked(
                        (uint64_t)std::numeric_limits<uint32_t>::max() + 1,
                        n, k, rank, a, first, rows, bt, first, rows),
                    DenseProofEncodeError::DimensionNotRepresentable));
    check("n wider than u32 fails closed",
          failsWith(encodePlainProofChecked(
                        m, (uint64_t)std::numeric_limits<uint32_t>::max() + 1,
                        k, rank, a, first, rows, bt, first, rows),
                    DenseProofEncodeError::DimensionNotRepresentable));
    check("k wider than u32 fails closed",
          failsWith(encodePlainProofChecked(
                        m, n,
                        (uint64_t)std::numeric_limits<uint32_t>::max() + 1,
                        rank, a, first, rows, bt, first, rows),
                    DenseProofEncodeError::DimensionNotRepresentable));
    check("rank wider than u16 fails closed",
          failsWith(encodePlainProofChecked(
                        m, n, k,
                        (uint64_t)std::numeric_limits<uint16_t>::max() + 1,
                        a, first, rows, bt, first, rows),
                    DenseProofEncodeError::RankNotRepresentable));
    check("zero dimension fails closed",
          failsWith(encodePlainProofChecked(0, n, k, rank, a, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::InvalidDimension));
    check("zero rank fails closed",
          failsWith(encodePlainProofChecked(m, n, k, 0, a, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::InvalidRank));
    check("rank wider than matrix width fails closed",
          failsWith(encodePlainProofChecked(m, n, 127, 128, a, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::InvalidRank));

    const uint64_t u32max = std::numeric_limits<uint32_t>::max();
    const MerkleProof maxA = proofForRows(u32max, 1, u32max - 1, 1, 0x44);
    const MerkleProof maxBt = proofForRows(u32max, 1, u32max - 1, 1, 0x55);
    check("the exact u32 dimension boundary remains representable",
          encodePlainProofChecked(u32max, u32max, 1, 1, maxA, u32max - 1, 1,
                                  maxBt, u32max - 1, 1)
              .ok());

    const MerkleProof rankMaxA = proofForRows(1, 65535, 0, 1, 0x61);
    const MerkleProof rankMaxBt = proofForRows(1, 65535, 0, 1, 0x62);
    check("the exact u16 rank boundary remains representable",
          encodePlainProofChecked(1, 1, 65535, 65535, rankMaxA, 0, 1,
                                  rankMaxBt, 0, 1)
              .ok());

    const MerkleProof oddA = builtProofForRows(3, 1024, 2, 1, 0x71);
    const MerkleProof oddBt = builtProofForRows(3, 1024, 2, 1, 0x72);
    check("odd-tail Merkle topology has an exact legal path",
          oddA.totalLeaves == 3 && oddA.siblings.size() == kDigestLen &&
              encodePlainProofChecked(3, 3, 1024, 128, oddA, 2, 1, oddBt, 2,
                                      1)
                  .ok());
    const MerkleProof singleA = builtProofForRows(1, 1024, 0, 1, 0x73);
    const MerkleProof singleBt = builtProofForRows(1, 1024, 0, 1, 0x74);
    check("one-leaf Merkle trees require no sibling path",
          singleA.totalLeaves == 1 && singleA.siblings.empty() &&
              encodePlainProofChecked(1, 1, 1024, 128, singleA, 0, 1,
                                      singleBt, 0, 1)
                  .ok());
    const MerkleProof rangeA = builtProofForRows(5, 1024, 1, 3, 0x75);
    const MerkleProof rangeBt = builtProofForRows(5, 1024, 1, 3, 0x76);
    check("non-power-of-two multi-leaf range uses the pinned two-node path",
          rangeA.totalLeaves == 5 && rangeA.leafIndices.size() == 3 &&
              rangeA.leafIndices.front() == 1 &&
              rangeA.leafIndices.back() == 3 &&
              rangeA.siblings.size() == 2 * kDigestLen &&
              encodePlainProofChecked(5, 5, 1024, 128, rangeA, 1, 3,
                                      rangeBt, 1, 3)
                  .ok());
    MerkleProof wrongBuiltPath = rangeA;
    wrongBuiltPath.siblings.resize(wrongBuiltPath.siblings.size() - kDigestLen);
    check("a real buildProof opening with one path node removed fails closed",
          failsWith(encodePlainProofChecked(5, 5, 1024, 128, wrongBuiltPath,
                                            1, 3, rangeBt, 1, 3),
                    DenseProofEncodeError::SiblingPathMismatch));

    MerkleProof bad = a;
    bad.totalLeaves--;
    check("A total leaf count minus one fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::TotalLeavesMismatch));
    bad = a;
    bad.totalLeaves++;
    check("A total leaf count plus one fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::TotalLeavesMismatch));
    bad = bt;
    bad.totalLeaves--;
    check("B-t total leaf count minus one fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, a, first, rows, bad,
                                            first, rows),
                    DenseProofEncodeError::TotalLeavesMismatch));
    bad = bt;
    bad.totalLeaves++;
    check("B-t total leaf count plus one fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, a, first, rows, bad,
                                            first, rows),
                    DenseProofEncodeError::TotalLeavesMismatch));

    check("equal matrix byte count under different dimensions cannot reuse A proof",
          failsWith(encodePlainProofChecked(256, n, 1024, rank, a, first, rows,
                                            bt, first, rows),
                    DenseProofEncodeError::LeafIndexCountMismatch));

    check("zero A row count fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, a, first, 0, bt,
                                            first, rows),
                    DenseProofEncodeError::RowRangeInvalid));
    check("A row start at the matrix end fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, a, m, 1, bt, first,
                                            rows),
                    DenseProofEncodeError::RowRangeInvalid));
    check("A row range crossing the matrix end fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, a, m - 1, 2, bt,
                                            first, rows),
                    DenseProofEncodeError::RowRangeInvalid));
    check("wrapping A row range fails as arithmetic overflow",
          failsWith(encodePlainProofChecked(
                        m, n, k, rank, a,
                        std::numeric_limits<uint64_t>::max(), 2, bt, first,
                        rows),
                    DenseProofEncodeError::ArithmeticOverflow));

    std::vector<uint64_t> indices = {7};
    check("checked row helper rejects row addition overflow",
          !leafIndicesFromRowsChecked(std::numeric_limits<uint64_t>::max(), 2,
                                      1, &indices) &&
              indices.empty());
    check("checked row helper rejects row-width multiplication overflow",
          !leafIndicesFromRowsChecked(std::numeric_limits<uint64_t>::max(), 1,
                                      2, &indices) &&
              indices.empty());
    check("compatibility row helper also fails closed on overflow",
          leafIndicesFromRows(std::numeric_limits<uint64_t>::max(), 2, 2)
              .empty());
    check("arithmetically valid but infeasible leaf range fails without allocation",
          !leafIndicesFromRowsChecked(0,
                                      std::numeric_limits<uint64_t>::max(), 1,
                                      &indices) &&
              indices.empty());

    const MerkleProof tinyA = fakeProof(1, std::vector<uint64_t>{0}, 0x91);
    const MerkleProof tinyBt = fakeProof(1, std::vector<uint64_t>{0}, 0x92);
    const DenseProofEncodeResult limited = encodePlainProofChecked(
        129, 1, 1, 1, tinyA, 0, 129, tinyBt, 0, 1);
    check("over-protocol row count has a stable resource-limit result",
          failsWith(limited, DenseProofEncodeError::ResourceLimit));
    check("compatibility serializer is fail-empty at the resource limit",
          encodePlainProof(129, 1, 1, 1, tinyA, 0, 129, tinyBt, 0, 1)
              .empty());

    bad = a;
    bad.leafIndices.clear();
    bad.leafData.clear();
    bad.siblings.clear();
    check("empty Merkle opening fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::EmptyProof));
    bad = a;
    bad.leafIndices.pop_back();
    bad.leafData.resize(bad.leafIndices.size() * kChunkLen);
    check("one omitted opened leaf fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::LeafIndexCountMismatch));
    bad = a;
    bad.leafIndices[1] = bad.leafIndices[0];
    check("duplicate leaf index fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::LeafIndexMismatch));
    bad = a;
    const uint64_t swap = bad.leafIndices[0];
    bad.leafIndices[0] = bad.leafIndices[1];
    bad.leafIndices[1] = swap;
    check("reordered leaf indices fail closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::LeafIndexMismatch));
    bad = a;
    bad.leafIndices.back() = bad.totalLeaves;
    check("out-of-range leaf index fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::LeafIndexMismatch));
    bad = a;
    bad.leafData.pop_back();
    check("short leaf payload fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::LeafDataSizeMismatch));
    bad = a;
    bad.leafData.push_back(0);
    check("long leaf payload fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::LeafDataSizeMismatch));
    bad = a;
    bad.siblings.push_back(0);
    check("partial sibling digest fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::SiblingLengthMismatch));
    bad = a;
    bad.siblings.resize(bad.siblings.size() - kDigestLen);
    check("missing sibling path node fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::SiblingPathMismatch));
    bad = a;
    bad.siblings.resize(bad.siblings.size() + kDigestLen);
    check("extra sibling path node fails closed",
          failsWith(encodePlainProofChecked(m, n, k, rank, bad, first, rows, bt,
                                            first, rows),
                    DenseProofEncodeError::SiblingPathMismatch));

    check("compatibility serializer returns empty rather than malformed bytes",
          encodePlainProof(m, n, k, rank, bad, first, rows, bt, first, rows)
              .empty());

    if (failures != 0) {
        fprintf(stderr, "%d dense-proof encoder checks failed\n", failures);
        return 1;
    }
    printf("PASS: checked Pearl dense-proof encoding\n");
    return 0;
}
