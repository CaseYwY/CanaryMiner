// CPU-only protocol-identity probe (Gate 1b-0). From a fixed fixture (tests/fixtures/protocol_identity_fixture.txt: a
// win mined by the Python reference, accepted by the pinned Pearl verifier) build the dense PlainProof with the miner's
// own encoder, the base64 submit payload, the authorize payload and the job-extra string, and print them.
// tests/check_protocol_identity.py compares them with the pinned golden values, which were produced on the pre-debrand
// seed tree (Gate 1a seed 999cd17): the bytes a pool or verifier sees must never change.
// usage: test_pearl_protocol_identity FIXTURE
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include "../src/core/pearl_pool.h"
using namespace om;
using namespace om::pearl;

static std::vector<uint8_t> unhex(const std::string &s) {
    std::vector<uint8_t> v; v.reserve(s.size() / 2);
    for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back((uint8_t)std::stoi(s.substr(i, 2), nullptr, 16));
    return v;
}
static std::string hex(const std::vector<uint8_t> &v) {
    static const char *d = "0123456789abcdef"; std::string s;
    for (uint8_t b : v) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
}
static void readProof(std::istream &in, MerkleProof *p, uint64_t *first, uint64_t *count) {
    std::string t; in >> t; p->totalLeaves = std::stoull(t);
    in >> t; auto root = unhex(t); for (int i = 0; i < 32; i++) p->root[i] = root[i];
    in >> t; size_t n = std::stoull(t);
    for (size_t i = 0; i < n; i++) { in >> t; p->leafIndices.push_back(std::stoull(t)); }
    for (size_t i = 0; i < n; i++) { in >> t; auto d = unhex(t); p->leafData.insert(p->leafData.end(), d.begin(), d.end()); }
    in >> t; size_t s = std::stoull(t);
    for (size_t i = 0; i < s; i++) { in >> t; auto d = unhex(t); p->siblings.insert(p->siblings.end(), d.begin(), d.end()); }
    in >> t; *first = std::stoull(t); in >> t; *count = std::stoull(t);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    std::ifstream in(argv[1]);
    std::string t; in >> t; uint64_t m = std::stoull(t);
    in >> t; uint64_t n = std::stoull(t);
    in >> t; uint64_t k = std::stoull(t);
    in >> t; uint64_t rank = std::stoull(t);
    std::string headerHex; in >> headerHex;
    MerkleProof a, bt; uint64_t aFirst, aRows, bFirst, bCols;
    readProof(in, &a, &aFirst, &aRows); readProof(in, &bt, &bFirst, &bCols);
    std::string pyProof; in >> pyProof;
    DenseProofEncodeResult r = encodePlainProofChecked(m, n, k, rank, a, aFirst, aRows, bt, bFirst, bCols);
    if (!r.ok()) { std::cerr << "encode failed\n"; return 1; }
    const std::string b64 = base64(r.bytes);
    auto header = unhex(headerHex);
    std::cout << "PROOF " << hex(r.bytes) << "\n";
    std::cout << "PROOF_EQUALS_PYTHON_REFERENCE " << (hex(r.bytes) == pyProof ? "yes" : "NO") << "\n";
    std::cout << "SUBMIT " << pearlPoolSubmitRequest(7, "fixture-job-1", b64) << "\n";
     std::cout << "AUTH_EXPLICIT " << pearlPoolAuthorizeRequest("prl1-fixture-address", "fixture-worker", "x", "CanaryMiner/" CANARY_MINER_VERSION, false) << "\n";
    std::cout << "AUTH_DEFAULT " << pearlPoolAuthorizeRequest("prl1-fixture-address", "fixture-worker") << "\n";
    std::cout << "EXTRA " << packPearlExtra(header.data(), 3) << "\n";
    Solution sol{}; sol.nonce = 1; sol.extra = b64;
    SolutionFingerprint fp; computeSolutionFingerprint(sol, &fp);
    std::vector<uint8_t> fpv(fp.bytes.begin(), fp.bytes.end());
    std::cout << "BOOKKEEPING_SOLUTION_FINGERPRINT " << hex(fpv) << "\n";
    return 0;
}
