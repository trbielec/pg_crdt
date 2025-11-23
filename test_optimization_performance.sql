-- Performance Comparison Test for Append-Only Serialization Optimization
-- This test demonstrates the O(N) → O(1) improvement

\timing on

DROP EXTENSION IF EXISTS automerge CASCADE;
CREATE EXTENSION automerge;

SET search_path TO public,automerge;

-- Test 1: Sequential Updates Performance
-- This tests the append-only optimization by doing multiple updates to the same document

CREATE TABLE perf_test (
    id SERIAL PRIMARY KEY,
    doc autodoc NOT NULL DEFAULT '{}'
);

-- Create a document with moderate initial size
INSERT INTO perf_test (doc) VALUES ('{"data": "initial"}');

-- Perform 100 sequential updates
-- With append-only optimization, each update should be O(1) instead of O(N)
DO $$
DECLARE
    i INT;
    start_time TIMESTAMP;
    end_time TIMESTAMP;
    elapsed INTERVAL;
BEGIN
    start_time := clock_timestamp();
    
    FOR i IN 1..100 LOOP
        UPDATE perf_test 
        SET doc = put_int(doc, '.counter', i) 
        WHERE id = 1;
    END LOOP;
    
    end_time := clock_timestamp();
    elapsed := end_time - start_time;
    
    RAISE NOTICE 'Sequential Updates Test:';
    RAISE NOTICE '  100 updates completed in: %', elapsed;
    RAISE NOTICE '  Average per update: % ms', EXTRACT(MILLISECONDS FROM elapsed) / 100;
END $$;

-- Verify the final state
SELECT doc::jsonb FROM perf_test WHERE id = 1;

-- Test 2: Document Size Growth
-- Compare update performance as document grows

TRUNCATE perf_test;
INSERT INTO perf_test (doc) VALUES ('{}');

DO $$
DECLARE
    i INT;
    update_time INTERVAL;
    start_time TIMESTAMP;
BEGIN
    RAISE NOTICE 'Document Growth Test:';
    
    FOR i IN 1..10 LOOP
        -- Add 10 fields
        FOR j IN 1..10 LOOP
            UPDATE perf_test 
            SET doc = put_str(doc, '.field_' || ((i-1)*10 + j), 'value_' || ((i-1)*10 + j))
            WHERE id = 1;
        END LOOP;
        
        -- Measure update time
        start_time := clock_timestamp();
        UPDATE perf_test 
        SET doc = put_int(doc, '.test_field', i) 
        WHERE id = 1;
        update_time := clock_timestamp() - start_time;
        
        RAISE NOTICE '  After % fields: % ms', i*10, EXTRACT(MILLISECONDS FROM update_time);
    END LOOP;
END $$;

-- Test 3: WAL Size Measurement
-- Measure WAL generated per update

TRUNCATE perf_test;
INSERT INTO perf_test (doc) VALUES ('{"initial": "data"}');

DO $$
DECLARE
    wal_before PG_LSN;
    wal_after PG_LSN;
    wal_bytes BIGINT;
BEGIN
    -- Get WAL position before update
    wal_before := pg_current_wal_lsn();
    
    -- Perform update
    UPDATE perf_test 
    SET doc = put_str(doc, '.new_field', 'new_value')
    WHERE id = 1;
    
    -- Get WAL position after update
    wal_after := pg_current_wal_lsn();
    wal_bytes := wal_after - wal_before;
    
    RAISE NOTICE 'WAL Generation Test:';
    RAISE NOTICE '  WAL bytes for single field update: %', wal_bytes;
END $$;

-- Cleanup
DROP TABLE perf_test;

\echo 'Performance test completed!'
