/* doctest/automerge/autodoc

\pset linestyle unicode
\pset border 2
-- # autodoc
--
-- This documentation is also tests for the code, the examples below
-- show the literal output of these statements from Postgres.
--
-- Some setup to make sure the extension is installed.

set search_path to public,automerge; -- pragma:hide
set client_min_messages = 'WARNING'; -- pragma:hide
create extension if not exists automerge;

*/

#include "../automerge.h"

/* Callback function for freeing autodoc arrays. */
static void autodoc_free_context_callback(void *);

/* Invalidate the flat cache when document is modified.
 * This ensures that the next save operation will regenerate the binary data
 * from the current document state, rather than using stale cached data. */
void invalidate_flat_cache(autodoc_Autodoc *doc) {
  if (doc->flat_data) {
    pfree(doc->flat_data);
    doc->flat_data = NULL;
  }
  doc->flat_size = 0;
  /* Note: We do NOT clear base_data or base_heads here.
   * They remain valid benchmarks for the incremental save logic
   * until the object is destroyed. */
}

/* Expanded Object Header "methods" for flattening for storage */
static Size autodoc_get_flat_size(ExpandedObjectHeader *eohptr);

static void autodoc_flatten_into(ExpandedObjectHeader *eohptr, void *result,
                                 Size allocated_size);

static const ExpandedObjectMethods autodoc_methods = {autodoc_get_flat_size,
                                                      autodoc_flatten_into};

/* Compute flattened size of storage needed for a autodoc */
static Size autodoc_get_flat_size(ExpandedObjectHeader *eohptr) {
  autodoc_Autodoc *doc = (autodoc_Autodoc *)eohptr;
  size_t flat_size;
  AMbyteSpan binary;
  AMitems changes;
  AMitem *item;
  AMchange *change;

  LOGF();

  /* This is a sanity check that the object is initialized */
  Assert(doc->em_magic == autodoc_MAGIC);

  /* Use cached value if already computed */
  if (doc->flat_size) {
    return doc->flat_size;
  }

  /* APPEND-ONLY OPTIMIZATION:
   * Instead of full re-serialization (AMsave), we calculate the delta
   * between our base_data (loaded from disk) and the current state. */
  
  if (doc->base_data && doc->base_len > 0) {
    /* Temporarily load base_data to get its heads (safe because we don't mutate it) */
    AMdoc *base_doc;
    AMitemToDoc(AMstackItem(&doc->stack, AMload(doc->base_data, doc->base_len),
                           _abort_cb, AMexpect(AM_VAL_TYPE_DOC)),
               &base_doc);
    
    /* Get heads - may be empty for new documents */
    AMitems base_heads_temp = AMstackItems(&doc->stack, AMgetHeads(base_doc), 
                                           _abort_cb, NULL);
    AMitem *heads_item = AMitemsNext(&base_heads_temp, 1);
    
    /* Only use append-only if base has changes */
    if (heads_item && AMitemValType(heads_item) == AM_VAL_TYPE_CHANGE_HASH) {
      AMitems base_heads = AMitemsRewound(&base_heads_temp);
      
      /* Get changes since base_heads (may be empty) */
      changes = AMstackItems(&doc->stack, 
                             AMgetChanges(doc->doc, &base_heads), 
                             _abort_cb, NULL);

      /* Calculate total size: Base + Sum(Changes) */
      flat_size = doc->base_len;
      while ((item = AMitemsNext(&changes, 1)) != NULL) {
        AMitemToChange(item, &change);
        binary = AMchangeRawBytes(change);
        flat_size += binary.count;
      }
      
      doc->flat_size = AUTODOC_OVERHEAD() + flat_size;
      return doc->flat_size;
    }
    /* Fall through to full save if base has no changes */
  }
  
  /* No base data or base is empty - perform full save */
  AMitemToBytes(AMstackItem(&doc->stack, AMsave(doc->doc), _abort_cb,
                            AMexpect(AM_VAL_TYPE_BYTES)),
                &binary);
  
  doc->flat_data = palloc0(binary.count);
  memcpy(doc->flat_data, binary.src, binary.count);
  flat_size = AUTODOC_OVERHEAD() + binary.count;
  
  /* Cache this value in the expanded object */
  doc->flat_size = flat_size;
  return flat_size;
}

/* Flatten autodoc into a pre-allocated result buffer that is
   allocated_size in bytes.  */
static void autodoc_flatten_into(ExpandedObjectHeader *eohptr, void *result,
                                 Size allocated_size) {
  void *data;
  AMbyteSpan bs;
  const struct AMactorId *actor_id;
  AMitems changes;
  AMitem *item;
  AMchange *change;
  size_t offset;

  /* Cast EOH pointer to expanded object, and result pointer to flat
     object */
  autodoc_Autodoc *doc = (autodoc_Autodoc *)eohptr;
  autodoc_FlatAutodoc *flat = (autodoc_FlatAutodoc *)result;

  LOGF();

  /* Sanity check the object is valid */
  Assert(doc->em_magic == autodoc_MAGIC);
  Assert(allocated_size == doc->flat_size);

  /* Zero out the whole allocated buffer */
  memset(flat, 0, allocated_size);

  AMitemToActorId(AMstackItem(&doc->stack, AMgetActorId(doc->doc), _abort_cb,
                              AMexpect(AM_VAL_TYPE_ACTOR_ID)),
                  &actor_id);

  bs = AMactorIdBytes(actor_id);
  memcpy(flat->uuid, bs.src, UUID_LEN);

  data = AUTODOC_DATA(flat);
  
  /* APPEND-ONLY FLATTENING */
  if (doc->base_data && doc->base_len > 0) {
    /* 1. Copy Base Data */
    memcpy(data, doc->base_data, doc->base_len);
    offset = doc->base_len;

    /* 2. Temporarily load base_data to get its heads */
    AMdoc *base_doc;
    AMitemToDoc(AMstackItem(&doc->stack, AMload(doc->base_data, doc->base_len),
                           _abort_cb, AMexpect(AM_VAL_TYPE_DOC)),
               &base_doc);
    
    /* Get heads - may be empty for new documents */
    AMitems base_heads_temp = AMstackItems(&doc->stack, AMgetHeads(base_doc), 
                                           _abort_cb, NULL);
    AMitem *heads_item = AMitemsNext(&base_heads_temp, 1);
    
    /* Only use append-only if base has changes */
    if (heads_item && AMitemValType(heads_item) == AM_VAL_TYPE_CHANGE_HASH) {
      AMitems base_heads = AMitemsRewound(&base_heads_temp);
      
      /* 3. Append Changes (may be empty) */
      changes = AMstackItems(&doc->stack, 
                             AMgetChanges(doc->doc, &base_heads), 
                             _abort_cb, NULL);
    
      while ((item = AMitemsNext(&changes, 1)) != NULL) {
        AMitemToChange(item, &change);
        bs = AMchangeRawBytes(change);
        memcpy((char*)data + offset, bs.src, bs.count);
        offset += bs.count;
      }
      /* Set the size of the varlena object */
      SET_VARSIZE(flat, allocated_size);
      return; /* Append-only path complete */
    }
    /* Fall through to full save if base has no changes */
  }
  
  /* Full Save path (New Document or empty base) */
  if (doc->flat_data) {
    /* Use cached flat_data from get_flat_size */
    memcpy(data, doc->flat_data, doc->flat_size - AUTODOC_OVERHEAD());
  } else {
    /* Fallback: generate it now */
    AMitemToBytes(AMstackItem(&doc->stack, AMsave(doc->doc), _abort_cb,
                              AMexpect(AM_VAL_TYPE_BYTES)),
                  &bs);
    memcpy(data, bs.src, bs.count);
  }

  /* Set the size of the varlena object */
  SET_VARSIZE(flat, allocated_size);
}

/* Expand a flat autodoc in to an Expanded one, return as Postgres Datum. */
autodoc_Autodoc *new_expanded_autodoc(autodoc_FlatAutodoc *flat,
                                      MemoryContext parentcontext) {
  autodoc_Autodoc *doc;
  size_t flat_size;
  unsigned char *flat_data;
  MemoryContext objcxt, oldcxt;
  MemoryContextCallback *ctxcb;
  const struct AMactorId *actor_id;

  LOGF();

  /* Create a new context that will hold the expanded object. */
  objcxt = AllocSetContextCreate(parentcontext, "expanded autodoc",
                                 ALLOCSET_DEFAULT_SIZES);

  /* Allocate a new expanded autodoc */
  doc = (autodoc_Autodoc *)MemoryContextAlloc(objcxt, sizeof(autodoc_Autodoc));

  /* Initialize the ExpandedObjectHeader member with flattening
   * methods and the new object context */
  EOH_init_header(&doc->hdr, &autodoc_methods, objcxt);

  /* Used for debugging checks */
  doc->em_magic = autodoc_MAGIC;

  /* Switch to new object context */
  oldcxt = MemoryContextSwitchTo(objcxt);

  /* Setting flat size to zero tells us the object has been written. */
  doc->flat_size = 0;
  doc->flat_data = NULL;
  doc->base_data = NULL;
  doc->base_len = 0;

  doc->stack = calloc(1, sizeof(AMstack));
  AMitemToDoc(AMstackItem(&doc->stack, AMcreate(NULL), _abort_cb,
                          AMexpect(AM_VAL_TYPE_DOC)),
              &doc->doc);
  if (flat != NULL) {
    flat_size = VARSIZE(flat) - AUTODOC_OVERHEAD();
    flat_data = AUTODOC_DATA(flat);

    /* Optimization: Persist the base data for append-only saves */
    doc->base_len = flat_size;
    doc->base_data = MemoryContextAlloc(objcxt, flat_size);
    memcpy(doc->base_data, flat_data, flat_size);

    AMitemToDoc(AMstackItem(&doc->stack, AMload(flat_data, flat_size),
                            _abort_cb, AMexpect(AM_VAL_TYPE_DOC)),
                &doc->doc);
    
    /* Note: We don't store heads here because AMitems can become invalid.
     * We'll load base_data temporarily when we need to calculate deltas. */

    AMitemToActorId(AMstackItem(&doc->stack,
                                AMactorIdFromBytes(flat->uuid, UUID_LEN),
                                _abort_cb, AMexpect(AM_VAL_TYPE_ACTOR_ID)),
                    &actor_id);

    AMstackItem(&doc->stack, AMsetActorId(doc->doc, actor_id), _abort_cb,
                AMexpect(AM_VAL_TYPE_VOID));
  }

  /* Create a context callback to free autodoc when context is cleared */
  ctxcb = MemoryContextAlloc(objcxt, sizeof(MemoryContextCallback));

  ctxcb->func = autodoc_free_context_callback;
  ctxcb->arg = doc;
  MemoryContextRegisterResetCallback(objcxt, ctxcb);

  /* Switch back to old context */
  MemoryContextSwitchTo(oldcxt);
  return doc;
}

static void autodoc_free_context_callback(void *ptr) {
  autodoc_Autodoc *doc = (autodoc_Autodoc *)ptr;
  LOGF();
  AMstackFree(&doc->stack);
  free(doc->stack);
}

autodoc_Autodoc *DatumGetAutodoc(Datum d) {
  autodoc_Autodoc *doc;
  autodoc_FlatAutodoc *flat;
  LOGF();
  if (VARATT_IS_EXTERNAL_EXPANDED(DatumGetPointer(d))) {
    doc = AutodocGetEOHP(d);
    Assert(doc->em_magic == autodoc_MAGIC);
    return doc;
  }
  flat = (autodoc_FlatAutodoc *)PG_DETOAST_DATUM(d);
  doc = new_expanded_autodoc(flat, CurrentMemoryContext);
  return doc;
}
