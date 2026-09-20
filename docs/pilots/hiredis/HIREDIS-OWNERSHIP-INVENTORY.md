# Hiredis ownership boundary inventory

Upstream pin: `redis/hiredis@33a12fb23531f33e3455c7ed46008c20c7ad9c78`.
The inventory is based on implementation, public comments, and call-site
inspection. An uncertain boundary remains unknown; names alone are not
evidence for a trusted rule.

| Symbol or family | Source | Allocation / effect | Return / parameter treatment | Retention / callback | Confidence | C&1/v1 | Planned treatment |
|---|---|---|---|---|---|---|---|
| `hi_malloc`, `hi_calloc` | `alloc.c` | `OWNED RETURN` | allocator result is nullable | none established at C& boundary | high for local implementation, low as external family | partial | inventory only; no allocator replacement contract |
| `hi_realloc` | `alloc.c` | `REALLOCATES` | conditional result and old-storage behavior | none | high that it reallocates; model not sufficient for this pilot | no | remain unsupported |
| `hi_free` | `alloc.c` | `DESTROYS` | consumes allocator storage | none | high locally | partial | no standalone global trust rule |
| `sdsnew`, `sdsnewlen`, `sdsempty`, `sdsdup` | `sds.c` | `OWNED RETURN` | nullable SDS result | no callback retention | high | yes | H1 reviewed contract / H2 metadata |
| `sdsfree` | `sds.c` | `DESTROYS` | consumes SDS owner | no retention after return | high | yes | H1 reviewed contract / H2 metadata |
| `sdscat*`, `sdscpy*` | `sds.c` | may `REALLOCATES` | updates SDS owner conditionally | none | medium | no | remain unsupported |
| `redisReaderCreate`, `redisReaderCreateWithFunctions` | `read.c` | `OWNED RETURN` | nullable reader result | custom functions are retained; lifetime is not a simple owned pointer | high for reader allocation, low for callback retention | partial | contract only for owned result; callback form remains bounded |
| `redisReaderFree` | `read.c` | `DESTROYS` | consumes reader | destroys retained reader state | high | yes | H1 reviewed contract / H2 metadata |
| `redisConnectWithOptions` | `hiredis.c` | `OWNED RETURN` | nullable context result | options may include external data | high for context allocation, conditional for options | partial | reviewed contract for result only |
| `redisConnect*` wrappers | `hiredis.c` | `OWNED RETURN` | nullable context result | error paths and options vary | medium | partial | do not infer from names; no broad wrapper rule |
| `redisFree`, `redisFreeKeepFd` | `hiredis.c` | `DESTROYS` | consumes context; keep-fd variant preserves descriptor | callbacks/buffers and fd policy differ | high for context destruction | partial | destroy metadata only; keep-fd semantics remain explicit |
| `redisCommand`, `redisCommandArgv`, `redisvCommand` | `hiredis.c` | `OWNED RETURN` | nullable reply result | command arguments are borrowed for call | high for reply result | yes | H1 reviewed contract / H2 metadata |
| `freeReplyObject` | `read.c` | `DESTROYS` | consumes reply tree | recursively destroys nested reply state | high | yes | H1 reviewed contract / H2 metadata |
| `redisFreeCommand` | `hiredis.c` | `DESTROYS` | consumes command buffer | no callback retention | high | yes | H1 reviewed contract / H2 metadata |
| `redisFreeSdsCommand` | `hiredis.c` | `DESTROYS` | consumes SDS command buffer | no callback retention | high | yes | H1 reviewed contract / H2 metadata |
| `redisAsyncContext`, async callbacks | `async.c`, `async.h` | `RETAINS`, callback-owned state | callback and `privdata` lifetimes are external | dictionaries retain callbacks/privdata | high that retention exists | no | excluded; see async boundary |
| event-loop adapters | `adapters/` | `RETAINS` | adapter-specific state | callbacks and loop ownership vary | high | no | excluded |
| SSL-specific integration | `ssl.c`, headers | `UNKNOWN / UNSUPPORTED` | external SSL state and callbacks | provider/library retention | high | no | excluded |

The `contracts/h1-reviewed.yaml` bundle contains only the high-confidence
owned-return and destruction boundaries selected above. It intentionally
omits allocator replacement, reallocating SDS operations, callback retention,
and custom event-loop ownership.
