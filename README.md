[![pg_crdt tests](https://github.com/supabase/pg_crdt/actions/workflows/test.yml/badge.svg)](https://github.com/supabase/pg_crdt/actions/workflows/test.yml)

# pg_crdt: Automerge CRDTs for PostgreSQL

`pg_crdt` is a PostgreSQL extension that integrates [Automerge](https://automerge.org/), a Conflict-Free Replicated Data Type (CRDT) library. It allows PostgreSQL to store, index, and merge concurrent changes to JSON-like documents without locking or conflict errors.

## Features

*   **Conflict-Free Merging:** Automatically merge concurrent edits from distributed clients.
*   **Efficient Storage:** Uses PostgreSQL's "Expanded Datum" facility to minimize serialization overhead during transactions.
*   **JSON Compatibility:** Seamless casting between `jsonb` and `autodoc` types.
*   **Granular Operations:** Modify specific fields (`put_int`, `put_text`, `splice_text`) without rewriting the entire document.
*   **History Preservation:** mechanisms to track actor IDs and change history (experimental).

## Installation

### Prerequisites

To build `pg_crdt`, you require the following tools installed on your system:

*   **PostgreSQL 15+** (including server headers, e.g., `postgresql-server-dev-16`)
*   **Rust** (stable toolchain) & **Cargo**
*   **CMake** (3.10+)
*   **GCC** or **Clang**
*   **libssl-dev**

### Build Configuration

This extension supports two build modes. Choose the one that matches your deployment environment.

#### 1. Static Linking (Recommended for Production)
Use this mode for deployment on managed PostgreSQL platforms or environments where you cannot install system-level shared libraries. It embeds the Automerge library directly into the extension.

```bash
# 1. Clean previous builds
make clean

# 2. Build with static linking enabled
make AUTOMERGE_STATIC=1

# 3. Install extension
sudo make install
```

#### 2. Dynamic Linking (Development Default)
Use this mode for local development or self-hosted servers where you have full OS control. It links against `libautomerge.so`.

```bash
# 1. Build
make

# 2. Install
sudo make install
```

### Enabling in Database

Once installed, enable the extension in your PostgreSQL database:

```sql
CREATE EXTENSION automerge;
```

## Usage

### Basic Operations

The core data type is `autodoc`. You can initialize it from standard JSONB.

```sql
CREATE TABLE documents (
    id SERIAL PRIMARY KEY,
    doc automerge.autodoc DEFAULT '{}'
);

-- Insert data
INSERT INTO documents (doc) 
VALUES ('{"title": "Hello World", "count": 0}'::jsonb);

-- Read data (cast back to jsonb)
SELECT doc::jsonb FROM documents;
```

### Modifying Data

`pg_crdt` provides functions to modify the CRDT structure efficiently.

```sql
-- Increment a counter
UPDATE documents 
SET doc = automerge.inc_counter(doc, '.count', 1) 
WHERE id = 1;

-- Update a text field
UPDATE documents 
SET doc = automerge.put_str(doc, '.title', 'Hello Postgres') 
WHERE id = 1;

-- Insert into a list
UPDATE documents 
SET doc = automerge.put_str(doc, '.tags[0]', 'new-tag', true) -- true = insert
WHERE id = 1;
```

### Merging Concurrent Changes

The power of CRDTs is merging divergent states.

```sql
-- Suppose we have two divergent versions of a document: doc_a and doc_b
SELECT automerge.merge(doc_a, doc_b) AS merged_doc;
```

## Known Limitations & Constraints

### 1. Numeric Precision
PostgreSQL's `NUMERIC` type supports arbitrary precision, while Automerge uses IEEE 754 double-precision floats (`F64`).
*   **Impact:** Numbers with more than 15-17 significant decimal digits will experience precision loss when stored in an `autodoc`.
*   **Behavior:** The extension will issue a `WARNING` if precision loss is detected during conversion. For financial data, consider storing values as integer cents or strings.

### 2. Update Semantics
Standard SQL `UPDATE` operations overwrite the row.
*   **Impact:** If two transactions run `UPDATE tbl SET doc = put(...)` concurrently, PostgreSQL's MVCC (Last Write Wins) rules apply to the row itself.
*   **Workaround:** To utilize full CRDT merging capabilities, applications should typically apply changes ("deltas") or use specific merge workflows rather than blind overwrites.

### 3. JSON Keys
Path traversal currently does not support JSON keys containing dots (`.`) or brackets (`[`).

## Development & Testing

To run the regression test suite:

```bash
# Ensure you have a running PostgreSQL instance or pg_regress configured
make installcheck
```

The test suite covers:
*   Core data type functionality
*   Error handling and edge cases
*   Memory safety and fuzzing scenarios
*   Multi-user merge scenarios

## What is a CRDT?

CRDTs are decentralized data structures that can safely be replicated and synchronized across multiple computers/nodes. They are the enabling technology for collaborative applications like Notion and Figma.

## Architecture

The [original implementation](https://supabase.com/blog/postgres-crdt) of this library was relatively naive - we used the Automerge's Rust libary to implement a CRDT as a data type. This had a major limitation: frequently updated CRDTs produce a lot of WAL and dead tuples.

The new implementation improves on this by taking advantage of an advanced in-memory feature in Postgres called an "expanded datum", which can be used for complex in-memory objects. This is described in some detail here:

[https://www.postgresql.org/docs/current/storage-toast.html#STORAGE-TOAST-INMEMORY](https://www.postgresql.org/docs/current/storage-toast.html#STORAGE-TOAST-INMEMORY)

There's still work to be done: a more fully fleshed out example application, better change aggregate functions to apply large sets of changes, and explore the ideas of having Postgres use the sync API to sync with other peers.

## License

This project is licensed under the PostgreSQL License. See [LICENSE](LICENSE) for details.
