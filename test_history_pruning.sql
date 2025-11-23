-- Test History Pruning (Squashing) Optimization
-- This demonstrates how squash_history() compresses documents by discarding history

DROP EXTENSION IF EXISTS automerge CASCADE;
CREATE EXTENSION automerge;

-- Test 1: Counter with Many Operations
-- Create a counter and increment it 1000 times
-- This creates 1000 operations in the history

\timing on

SELECT 'Test 1: Counter with 1000 Increments' as test;

DO $$
DECLARE
    doc autodoc := '{}';
    i int;
BEGIN
    doc := put_counter(doc, '.counter', 0);
    FOR i IN 1..1000 LOOP
        doc := inc_counter(doc, '.counter', 1);
    END LOOP;
    
    CREATE TEMP TABLE counter_test (id serial, doc autodoc, description text);
    INSERT INTO counter_test (doc, description) VALUES (doc, 'Original with 1000 operations');
    
    -- Get the size before squashing
    RAISE NOTICE 'Original document size: % bytes', pg_column_size(doc);
    RAISE NOTICE 'Counter value: %', get_counter(doc, '.counter');
    
    -- Squash the history
    doc := squash_history(doc);
    
    INSERT INTO counter_test (doc, description) VALUES (doc, 'Squashed (1 operation)');
    
    -- Get the size after squashing
    RAISE NOTICE 'Squashed document size: % bytes', pg_column_size(doc);
    RAISE NOTICE 'Counter value after squash: %', get_counter(doc, '.counter');
END $$;

SELECT description, 
       pg_column_size(doc) as size_bytes,
       get_counter(doc, '.counter') as counter_value
FROM counter_test
ORDER BY id;

-- Test 2: Document with Many Edits
-- Create a document and make many field updates

SELECT 'Test 2: Document with 100 Field Updates' as test;

DO $$
DECLARE
    doc autodoc := '{}';
    i int;
BEGIN
    FOR i IN 1..100 LOOP
        doc := put_int(doc, '.field' || i, i);
    END LOOP;
    
    -- Now update each field 10 times
    FOR i IN 1..100 LOOP
        doc := put_int(doc, '.field' || i, i * 2);
        doc := put_int(doc, '.field' || i, i * 3);
        doc := put_int(doc, '.field' || i, i * 4);
        doc := put_int(doc, '.field' || i, i * 5);
    END LOOP;
    
    CREATE TEMP TABLE doc_test (id serial, doc autodoc, description text);
    INSERT INTO doc_test (doc, description) VALUES (doc, 'Original with 500 operations');
    
    RAISE NOTICE 'Original document size: % bytes', pg_column_size(doc);
    RAISE NOTICE 'Sample field value: %', get_int(doc, '.field1');
    
    -- Squash the history
    doc := squash_history(doc);
    
    INSERT INTO doc_test (doc, description) VALUES (doc, 'Squashed (100 operations)');
    
    RAISE NOTICE 'Squashed document size: % bytes', pg_column_size(doc);
    RAISE NOTICE 'Sample field value after squash: %', get_int(doc, '.field1');
END $$;

SELECT description, 
       pg_column_size(doc) as size_bytes,
       get_int(doc, '.field1') as sample_value
FROM doc_test
ORDER BY id;

-- Test 3: Text Document with Many Edits
-- Create a text document and make many edits

SELECT 'Test 3: Text Document with Multiple Edits' as test;

DO $$
DECLARE
    doc autodoc := '{"text":""}';
    i int;
BEGIN
    -- Insert text multiple times
    FOR i IN 1..50 LOOP
        doc := put_text(doc, '.text', 'Line ' || i || E'\n', true);
    END LOOP;
    
    CREATE TEMP TABLE text_test (id serial, doc autodoc, description text);
    INSERT INTO text_test (doc, description) VALUES (doc, 'Original with 50 text operations');
    
    RAISE NOTICE 'Original document size: % bytes', pg_column_size(doc);
    
    -- Squash the history
    doc := squash_history(doc);
    
    INSERT INTO text_test (doc, description) VALUES (doc, 'Squashed (1 text operation)');
    
    RAISE NOTICE 'Squashed document size: % bytes', pg_column_size(doc);
END $$;

SELECT description, 
       pg_column_size(doc) as size_bytes
FROM text_test
ORDER BY id;

-- Summary
SELECT 'Summary: History Pruning Results' as summary;
SELECT 'Counter Test' as test_type,
       MAX(CASE WHEN description LIKE '%Original%' THEN pg_column_size(doc) END) as original_size,
       MAX(CASE WHEN description LIKE '%Squashed%' THEN pg_column_size(doc) END) as squashed_size,
       ROUND(100.0 * (1 - MAX(CASE WHEN description LIKE '%Squashed%' THEN pg_column_size(doc) END)::numeric / 
                          MAX(CASE WHEN description LIKE '%Original%' THEN pg_column_size(doc) END)::numeric), 1) as compression_pct
FROM counter_test
UNION ALL
SELECT 'Document Test',
       MAX(CASE WHEN description LIKE '%Original%' THEN pg_column_size(doc) END),
       MAX(CASE WHEN description LIKE '%Squashed%' THEN pg_column_size(doc) END),
       ROUND(100.0 * (1 - MAX(CASE WHEN description LIKE '%Squashed%' THEN pg_column_size(doc) END)::numeric / 
                          MAX(CASE WHEN description LIKE '%Original%' THEN pg_column_size(doc) END)::numeric), 1)
FROM doc_test
UNION ALL
SELECT 'Text Test',
       MAX(CASE WHEN description LIKE '%Original%' THEN pg_column_size(doc) END),
       MAX(CASE WHEN description LIKE '%Squashed%' THEN pg_column_size(doc) END),
       ROUND(100.0 * (1 - MAX(CASE WHEN description LIKE '%Squashed%' THEN pg_column_size(doc) END)::numeric / 
                          MAX(CASE WHEN description LIKE '%Original%' THEN pg_column_size(doc) END)::numeric), 1)
FROM text_test;
