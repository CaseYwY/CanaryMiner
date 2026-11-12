// Helper transport identity. Plaintext is never a verified TLS state.
#pragma once
namespace om {
enum class PoolTlsTransportMode { Tls, Plaintext };
enum class PoolTlsRpcState { Dormant, Handshaking, VerifiedOpen, Closed, Failed, PlaintextOpen };
} // namespace om
