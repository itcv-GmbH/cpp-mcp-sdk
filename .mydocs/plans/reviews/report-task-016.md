# Review Report: Task 016 - Split include/mcp/jsonrpc/router.hpp (cpp-sdk-codebase-cleanup)

## Status
**FAIL**

## Compliance Check
- [x] Implementation matches `task-016.md` instructions (header split)
- [ ] Definition of Done NOT met (build fails)
- [x] No unauthorized architectural changes

## Verification Output
- **Umbrella Header Types**: `include/mcp/jsonrpc/router.hpp` contains 0 class/struct declarations ✓
- **Per-type Headers Created**: ✓
  - `handler_types.hpp`
  - `outbound_request_options.hpp` (OutboundRequestOptions)
  - `progress_types.hpp` (ProgressUpdate)
  - `router_class.hpp` (Router)
  - `router_enums.hpp`
  - `router_options.hpp` (RouterOptions)
- **Check Script**: Reports violations in other headers (pre-existing, not this task's scope)
- **Build**: **FAILED** - clang-tidy misc-include-cleaner warnings treated as errors
- **Tests**: Skipped due to build failure

## Issues Found

### Critical
- **Build Failure**: clang-tidy `misc-include-cleaner` warnings are causing build failures
  - `src/lifecycle/session.cpp:18`: included header router.hpp is not used directly
  - `src/lifecycle/session.cpp:313`: no header providing "mcp::jsonrpc::RouterOptions" is directly included
  - `src/lifecycle/session.cpp:317`: no header providing "mcp::jsonrpc::RequestHandler" is directly included
  - `src/lifecycle/session.cpp:323`: no header providing "mcp::jsonrpc::NotificationHandler" is directly included
  - `src/client/client.cpp:31`: included header router.hpp is not used directly
  - `src/client/client.cpp:1215`: no header providing "mcp::jsonrpc::RouterOptions" is directly included
  - `src/client/client.cpp:2299`: no header providing "mcp::jsonrpc::OutboundRequestOptions" is directly included
  - `src/server/server.cpp:30`: included header router.hpp is not used directly
  - `src/server/server.cpp:601`: no header providing "mcp::jsonrpc::OutboundMessageSender" is directly included
  - `src/server/server.cpp:667`: no header providing "mcp::jsonrpc::RouterOptions" is directly included
  - `src/server/server.cpp:756`: no header providing "mcp::jsonrpc::RequestHandler" is directly included
  - `src/server/server.cpp:783`: no header providing "mcp::jsonrpc::NotificationHandler" is directly included
  - `src/server/server.cpp:834`: no header providing "mcp::jsonrpc::OutboundRequestOptions" is directly included

## Required Actions
1. Update `src/lifecycle/session.cpp` to include specific router-related headers instead of the umbrella header
2. Update `src/client/client.cpp` to include specific router-related headers instead of the umbrella header
3. Update `src/server/server.cpp` to include specific router-related headers instead of the umbrella header
4. Re-run build and tests after fixing includes
