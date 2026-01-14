## Streaming Queries with `ndb_query_visit`

### What problem does this solve?

Before this change, every query in nostrdb worked the same way:
it collected *all* matching notes into a result buffer in memory, and only then returned them to the caller.

That’s fine for small queries, but it becomes wasteful or impractical when:

* You only need to *look at* results one by one
* You want to stop early after finding “enough”
* The result set could be very large
* You want to process results as a stream instead of storing them all

### The new idea: visit results as they are found

The new `ndb_query_visit` API lets you **stream query results** instead of buffering them.

Instead of saying:

> “Give me an array of results”

you say:

> “Call this function every time you find a result”

That function is called a **visitor**.

### How it works (conceptually)

* nostrdb runs the query exactly the same way as before
* Each matching note is handed to your visitor callback immediately
* No result array is allocated
* Your visitor can decide whether to:

  * keep going
  * or stop the query early

### Why this is useful

* **Lower memory usage**
  No large result buffers are allocated.

* **Early exit**
  If you only need the first N results, you can stop instantly.

* **Streaming / folding**
  Perfect for counters, aggregations, indexing, filtering, or piping results elsewhere.

* **Same query engine**
  All existing query plans (ids, authors, kinds, tags, search, etc.) work unchanged.

### Limits still work

Query limits still apply:

* If the filter specifies a `limit`, the visitor will only be called up to that many times
* If your visitor returns `NDB_VISITOR_STOP`, the query ends immediately

A limit of `0` means “no limit” — the visitor will see everything unless it stops the query.

### How it compares to `ndb_query`

| `ndb_query`                 | `ndb_query_visit`                 |
| --------------------------- | --------------------------------- |
| Allocates a result buffer   | No result buffer                  |
| Returns all results at once | Streams results one by one        |
| Must store results          | Can process immediately           |
| Good for small sets         | Ideal for large or unbounded sets |

### Mental model

Think of the difference like this:

* **`ndb_query`** → “give me a list”
* **`ndb_query_visit`** → “walk the results and call me for each one”

Same database, same filters, same plans — just a more flexible way to consume results.
