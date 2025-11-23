-- Test History Pruning (Squashing) Optimization
-- This demonstrates how squash_history() compresses documents by discarding history

DROP EXTENSION IF EXISTS automerge CASCADE;
CREATE EXTENSION automerge;

\timing on

-- Test 1: Simple squash test
SELECT 'Test 1: Basic Squash Functionality' as test;

-- Create a simple document
CREATE TEMP TABLE squash_test (doc autodoc);
INSERT INTO squash_test VALUES ('{"counter": 0}');

-- Update it multiple times (creates history)
UPDATE squash_test SET doc = put_int(doc, '.counter', 1);
UPDATE squash_test SET doc = put_int(doc, '.counter', 2);
UPDATE squash_test SET doc = put_int(doc, '.counter', 3);
UPDATE squash_test SET doc = put_int(doc, '.counter', 4);
UPDATE squash_test SET doc = put_int(doc, '.counter', 5);

SELECT 'Before squash:' as status, 
       pg_column_size(doc) as size_bytes,
       get_int(doc, '.counter') as counter_value
FROM squash_test;

-- Squash the history
UPDATE squash_test SET doc = squash_history(doc);

SELECT 'After squash:' as status,
       pg_column_size(doc) as size_bytes,
       get_int(doc, '.counter') as counter_value
FROM squash_test;

-- Test 2: Verify data integrity after squash
SELECT 'Test 2: Data Integrity Check' as test;

CREATE TEMP TABLE integrity_test (doc autodoc);
INSERT INTO integrity_test VALUES ('{}');

-- Create a complex document
UPDATE integrity_test SET doc = put_int(doc, '.field1', 100);
UPDATE integrity_test SET doc = put_str(doc, '.field2', 'hello');
UPDATE integrity_test SET doc = put_bool(doc, '.field3', true);
UPDATE integrity_test SET doc = put_double(doc, '.field4', 3.14);

SELECT 'Before squash:' as status,
       get_int(doc, '.field1') as field1,
       get_str(doc, '.field2') as field2,
       get_bool(doc, '.field3') as field3,
       get_double(doc, '.field4') as field4
FROM integrity_test;

UPDATE integrity_test SET doc = squash_history(doc);

SELECT 'After squash:' as status,
       get_int(doc, '.field1') as field1,
       get_str(doc, '.field2') as field2,
       get_bool(doc, '.field3') as field3,
       get_double(doc, '.field4') as field4
FROM integrity_test;

-- Test 3: Size reduction with many operations
SELECT 'Test 3: Size Reduction Test' as test;

CREATE TEMP TABLE size_test (doc autodoc);
INSERT INTO size_test VALUES ('{"value": 0}');

-- Perform 20 updates
UPDATE size_test SET doc = put_int(doc, '.value', 1);
UPDATE size_test SET doc = put_int(doc, '.value', 2);
UPDATE size_test SET doc = put_int(doc, '.value', 3);
UPDATE size_test SET doc = put_int(doc, '.value', 4);
UPDATE size_test SET doc = put_int(doc, '.value', 5);
UPDATE size_test SET doc = put_int(doc, '.value', 6);
UPDATE size_test SET doc = put_int(doc, '.value', 7);
UPDATE size_test SET doc = put_int(doc, '.value', 8);
UPDATE size_test SET doc = put_int(doc, '.value', 9);
UPDATE size_test SET doc = put_int(doc, '.value', 10);

SELECT pg_column_size(doc) as original_size FROM size_test \gset

UPDATE size_test SET doc = squash_history(doc);

SELECT pg_column_size(doc) as squashed_size FROM size_test \gset

SELECT 'Original size:' as metric, :original_size as bytes
UNION ALL
SELECT 'Squashed size:', :squashed_size
UNION ALL
SELECT 'Reduction:', :original_size - :squashed_size
UNION ALL
SELECT 'Compression %:', ROUND(100.0 * (1 - :squashed_size::numeric / :original_size::numeric), 1)::text::int;
