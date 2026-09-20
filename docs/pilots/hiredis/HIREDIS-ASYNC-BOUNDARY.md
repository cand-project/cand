# Hiredis async boundary

The first integration scope does not claim async callback retention. Async
results are kept visible as excluded or INCOMPLETE rather than hidden from
coverage.

## Reviewed constructs

| Construct | Observation | C&1/v1 treatment |
|---|---|---|
| `redisAsyncContext` | embeds context state and owns callback dictionaries | excluded / INCOMPLETE |
| `redisAsyncConnect*` | creates an async context with error and event-loop paths | excluded / INCOMPLETE |
| `redisAsyncFree`, `redisAsyncDisconnect` | teardown interacts with pending callbacks and loop state | excluded / INCOMPLETE |
| `redisCallbackFn` and connection callbacks | callback invocation is separated from registration | excluded / INCOMPLETE |
| subscription and push callbacks | callback dictionaries retain user callbacks and `privdata` | excluded / INCOMPLETE |
| event-loop adapters | adapter objects and function pointers retain external loop state | excluded / INCOMPLETE |
| retained `privdata` | application lifetime is not described by a simple owned parameter | excluded / INCOMPLETE |

`async.c` includes dictionary and callback machinery. A callback may be
registered, retained, invoked later, removed, or destroyed through a different
path. The available C&1/v1 evidence model does not establish those temporal
relationships across the event loop and application-owned `privdata`.

No trusted contract was added for these behaviors. Treating a callback as an
owned return, or treating retained `privdata` as a borrowed parameter, would
overstate the qualified claim. These observations are requirements evidence
for later work, not C&1/v2 implementation in this pilot.
