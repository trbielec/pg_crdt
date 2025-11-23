--
-- production_fixes.sql
-- Regression tests for Critical Stability & Integrity Fixes (Nov 2025)
--

SET client_min_messages = 'WARNING';
CREATE EXTENSION IF NOT EXISTS automerge;
SET search_path TO public, automerge;

--
-- Test 1: Stale Cache Invalidation (Fixes Data Corruption)
-- Description: Ensure that sequential updates to the same row in the same session
-- actually persist the latest value, rather than using cached binary data.
--
CREATE TABLE cache_test (id serial, doc autodoc);
INSERT INTO cache_test (doc) VALUES ('{"counter": 0}'::jsonb);

-- First update
UPDATE cache_test SET doc = put_int(doc, '.counter', 1) WHERE id = 1;
-- Second update (This would fail/revert to 1 without the fix)
UPDATE cache_test SET doc = put_int(doc, '.counter', 2) WHERE id = 1;

SELECT get_int(doc, '.counter') as cache_result FROM cache_test;

--
-- Test 2: Text List Data Loss
-- Description: Verify that Text objects inside Lists are correctly exported to JSONB.
-- Before fix: Result was {"list": []}
--
SELECT to_jsonb(put_text('{"list": []}'::jsonb, '.list[0]', 'Hello World', true)) as text_list_result;

--
-- Test 3: JSONB Import Buffer Safety
-- Description: Ensure strings are read using explicit length, not strlen().
-- We verify that adjacent keys ("A", "B") are imported strictly as "A", not "AB".
--
SELECT 
    CASE 
        WHEN get_str(from_jsonb('{"val1": "A", "val2": "B"}'::jsonb), '.val1') = 'A' THEN 'PASS'
        ELSE 'FAIL'
    END as buffer_safety_result;

--
-- Test 4: Numeric Precision Warning
-- Description: Ensure the system warns (but doesn't crash) on high precision.
-- Note: We cannot easily assert WARNING messages in standard SQL tests, 
-- but this exercises the code path to ensure no Segfault occurs.
--
SELECT from_jsonb('{"high_precision": 123.45678901234567890}'::jsonb) IS NOT NULL as precision_check;

--
-- Test 5: Commit Return Type
-- Description: Verify put_ functions accept commit messages without type errors.
--
SELECT to_jsonb(put_int('{"v":0}'::jsonb, '.v', 1, false, 'optimization fix')) as commit_msg_result;

-- Cleanup
DROP TABLE cache_test;
