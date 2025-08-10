# Copilot Instructions (C++)

## Principles
- Follow SOLID. Enforce SRP, prefer small interfaces, apply DI via constructor injection.
- Never use raw new/delete. Use RAII and std::unique_ptr. No singletons.
- Headers: declarations only; minimize includes; prefer forward decls and Pimpl where ABI stability matters.
- Inline suggestions must compile under C++20, -Wall -Wextra -Werror.

## Design patterns to prefer
- Strategy for pluggable behavior (pricing, compression, logging).
- Factory only at composition root.
- Observer via std::function; no global registries.

## Testing
- Generate GTest first, then implementation. Tests are the contract.

## File boundaries
- Place interfaces in `include/`, impl in `src/`. Don’t introduce I/O in pure logic classes.

## Examples
- Logger interface + ConsoleLogger impl; services depend on Logger&, passed from main().
- Split fat classes along reasons-to-change; do not widen preconditions in overrides.

# Copilot Instructions (C++ desktop/services)

Principles
- C++20 baseline; treat warnings as errors. No raw new/delete. RAII, unique_ptr by default.
- Hexagonal architecture: UI/OS/IPC at edges. Core has zero I/O.
- Prefer small interfaces or C++20 concepts. Use DI via constructors. No singletons.
- Pimpl all public classes for ABI and build hygiene. Headers: declarations only, forward-declare where possible.

Concurrency & I/O
- Use executors/thread pools; no ad-hoc std::thread. Prefer coroutines for async I/O.
- Every async op supports cancellation (std::stop_token) and timeouts.
- Message passing beats shared mutable state. If locking is required, keep regions minimal.

Errors & Observability
- Use exceptions OR std::expected<T,E>. Do not mix them.
- Structured logging with correlation IDs. No silent failure paths.
- Add metrics for latency/error rates; expose health endpoints for services.

IPC & Data
- Local IPC via named pipes/domain sockets or gRPC. JSON/TOML only for human-edited config.
- Schema-first serialization (Protobuf/FlatBuffers) for interprocess traffic.

Testing & Quality
- Generate tests first (GTest). Add property tests for pure logic and fuzz tests for parsers.
- Build flags: -Wall -Wextra -Werror. Run sanitizers (ASan/UBSan/TSan) in CI.

Security & Updates
- Least privilege for services. Code signing. Updater verifies signatures and supports rollback.

When generating code
- Place interfaces in `include/`, impl in `src/`. No I/O in core logic.
- Suggest diffs that follow these rules. If a rule conflicts with an edit, explain and propose an alternative.
 (See <attachments> above for file contents. You may not need to search or read the file again.)
