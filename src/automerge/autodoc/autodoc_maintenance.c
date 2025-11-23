#include "../automerge.h"

/* 
 * Optimization #2: History Pruning (Squashing)
 * 
 * This file implements logic to deeply copy the *visible state* of an 
 * Automerge document into a fresh document, effectively discarding 
 * all history (tombstones, old operations).
 */

static void _recursive_copy(AMdoc *dest, AMobjId const *dest_obj, 
                            AMdoc *src, AMobjId const *src_obj,
                            AMstack **stack);

PG_FUNCTION_INFO_V1(autodoc_squash_history);
Datum autodoc_squash_history(PG_FUNCTION_ARGS) {
    autodoc_Autodoc *src_doc;
    autodoc_Autodoc *new_doc;

    LOGF();

    src_doc = AUTODOC_GETARG(0);

    /* Create a fresh, empty document */
    new_doc = new_expanded_autodoc(NULL, CurrentMemoryContext);

    /* Recursively copy state from src to new */
    /* Note: We use the stack from new_doc for lifecycle management */
    _recursive_copy(new_doc->doc, AM_ROOT, src_doc->doc, AM_ROOT, &new_doc->stack);

    /* Force a clean save state */
    invalidate_flat_cache(new_doc);
    
    AUTODOC_RETURN(new_doc);
}

static void _recursive_copy(AMdoc *dest, AMobjId const *dest_obj, 
                            AMdoc *src, AMobjId const *src_obj,
                            AMstack **stack) {
    AMitems items;
    AMitem *item;
    AMbyteSpan key_str;
    AMvalType val_type;
    
    check_stack_depth();

    /* Iterate over all keys/indices in the source object */
    /* Note: This iterates the *visible* state */
    if (AMobjObjType(src, src_obj) == AM_OBJ_TYPE_LIST) {
        items = AMstackItems(stack, AMlistRange(src, src_obj, 0, SIZE_MAX, NULL), 
                             _abort_cb, NULL);
    } else {
        items = AMstackItems(stack, AMkeys(src, src_obj, NULL), 
                             _abort_cb, NULL);
    }

    while ((item = AMitemsNext(&items, 1)) != NULL) {
        val_type = AMitemValType(item);
        
        /* Determine Key (Map) or Index (List) */
        /* For lists, we just append, so we don't need the explicit index */
        AMitemKey(item, &key_str);

        switch (val_type) {
            case AM_VAL_TYPE_BOOL: {
                bool val; 
                AMitemToBool(item, &val);
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP)
                    AMstackItem(stack, AMmapPutBool(dest, dest_obj, key_str, val), _abort_cb, NULL);
                else 
                    AMstackItem(stack, AMlistPutBool(dest, dest_obj, SIZE_MAX, true, val), _abort_cb, NULL);
                break;
            }
            case AM_VAL_TYPE_INT: {
                int64_t val;
                AMitemToInt(item, &val);
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP)
                    AMstackItem(stack, AMmapPutInt(dest, dest_obj, key_str, val), _abort_cb, NULL);
                else 
                    AMstackItem(stack, AMlistPutInt(dest, dest_obj, SIZE_MAX, true, val), _abort_cb, NULL);
                break;
            }
            case AM_VAL_TYPE_F64: {
                double val;
                AMitemToF64(item, &val);
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP)
                    AMstackItem(stack, AMmapPutF64(dest, dest_obj, key_str, val), _abort_cb, NULL);
                else 
                    AMstackItem(stack, AMlistPutF64(dest, dest_obj, SIZE_MAX, true, val), _abort_cb, NULL);
                break;
            }
            case AM_VAL_TYPE_STR: {
                AMbyteSpan val;
                AMitemToStr(item, &val);
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP)
                    AMstackItem(stack, AMmapPutStr(dest, dest_obj, key_str, val), _abort_cb, NULL);
                else 
                    AMstackItem(stack, AMlistPutStr(dest, dest_obj, SIZE_MAX, true, val), _abort_cb, NULL);
                break;
            }
            case AM_VAL_TYPE_COUNTER: {
                int64_t val;
                AMitemToCounter(item, &val);
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP)
                    AMstackItem(stack, AMmapPutCounter(dest, dest_obj, key_str, val), _abort_cb, NULL);
                else 
                    AMstackItem(stack, AMlistPutCounter(dest, dest_obj, SIZE_MAX, true, val), _abort_cb, NULL);
                break;
            }
            case AM_VAL_TYPE_TIMESTAMP: {
                int64_t val;
                AMitemToTimestamp(item, &val);
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP)
                    AMstackItem(stack, AMmapPutTimestamp(dest, dest_obj, key_str, val), _abort_cb, NULL);
                else 
                    AMstackItem(stack, AMlistPutTimestamp(dest, dest_obj, SIZE_MAX, true, val), _abort_cb, NULL);
                break;
            }
            case AM_VAL_TYPE_OBJ_TYPE: {
                AMobjId const *child_src_id = AMitemObjId(item);
                AMobjType child_type = AMobjObjType(src, child_src_id);
                AMitem *new_child_item;
                
                /* Create the corresponding container in dest */
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP) {
                    new_child_item = AMstackItem(stack, 
                        AMmapPutObject(dest, dest_obj, key_str, child_type),
                        _abort_cb, NULL);
                } else {
                    new_child_item = AMstackItem(stack, 
                        AMlistPutObject(dest, dest_obj, SIZE_MAX, true, child_type),
                        _abort_cb, NULL);
                }

                if (child_type == AM_OBJ_TYPE_TEXT) {
                    /* Special handling for Text objects - preserve text AND marks */
                    AMbyteSpan text_val;
                    AMobjId const *dest_text_id = AMitemObjId(new_child_item);
                    
                    /* 1. Copy the text content */
                    AMitemToStr(AMstackItem(stack, AMtext(src, child_src_id, NULL), _abort_cb, NULL), &text_val);
                    AMstackItem(stack, AMspliceText(dest, dest_text_id, 0, 0, text_val), _abort_cb, NULL);
                    
                    /* 2. Copy all marks (formatting: bold, italic, etc.) */
                    AMitems marks = AMstackItems(stack, AMmarks(src, child_src_id, NULL), _abort_cb, NULL);
                    AMitem *mark_item;
                    
                    while ((mark_item = AMitemsNext(&marks, 1)) != NULL) {
                        AMmark *mark;
                        AMitemToMark(mark_item, &mark);
                        
                        /* Get mark details */
                        AMbyteSpan mark_name = AMmarkName(mark);
                        size_t start = AMmarkStart(mark);
                        size_t end = AMmarkEnd(mark);
                        
                        /* Get mark value */
                        AMitem *value_item = AMstackItem(stack, AMmarkValue(mark), _abort_cb, NULL);
                        
                        /* Apply mark to destination text */
                        AMstackItem(stack, AMmarkCreate(dest, dest_text_id, start, end, 
                                                       AM_MARK_EXPAND_NONE, mark_name, value_item), 
                                   _abort_cb, NULL);
                    }
                } else {
                    /* Recurse for Maps and Lists */
                    _recursive_copy(dest, AMitemObjId(new_child_item), src, child_src_id, stack);
                }
                break;
            }
            default:
                /* Nulls, etc */
                if (AMobjObjType(dest, dest_obj) == AM_OBJ_TYPE_MAP)
                    AMstackItem(stack, AMmapPutNull(dest, dest_obj, key_str), _abort_cb, NULL);
                else 
                    AMstackItem(stack, AMlistPutNull(dest, dest_obj, SIZE_MAX, true), _abort_cb, NULL);
                break;
        }
    }
}
