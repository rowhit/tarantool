/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "vy_log.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <msgpuck/msgpuck.h>
#include <small/mempool.h>
#include <small/region.h>
#include <small/rlist.h>

#include "assoc.h"
#include "coio_task.h"
#include "diag.h"
#include "errcode.h"
#include "errinj.h"
#include "fiber.h"
#include "iproto_constants.h" /* IPROTO_INSERT */
#include "key_def.h"
#include "latch.h"
#include "replication.h" /* INSTANCE_UUID */
#include "salad/stailq.h"
#include "say.h"
#include "trivia/util.h"
#include "wal.h"
#include "vclock.h"
#include "xlog.h"
#include "xrow.h"

/**
 * Integer key of a field in the vy_log_record structure.
 * Used for packing a record in MsgPack.
 */
enum vy_log_key {
	VY_LOG_KEY_INDEX_LSN		= 0,
	VY_LOG_KEY_RANGE_ID		= 1,
	VY_LOG_KEY_RUN_ID		= 2,
	VY_LOG_KEY_BEGIN		= 3,
	VY_LOG_KEY_END			= 4,
	VY_LOG_KEY_INDEX_ID		= 5,
	VY_LOG_KEY_SPACE_ID		= 6,
	VY_LOG_KEY_DEF			= 7,
	VY_LOG_KEY_SLICE_ID		= 8,
	VY_LOG_KEY_DUMP_LSN		= 9,
	VY_LOG_KEY_GC_LSN		= 10,
	VY_LOG_KEY_TRUNCATE_COUNT	= 11,
};

/** vy_log_key -> human readable name. */
static const char *vy_log_key_name[] = {
	[VY_LOG_KEY_INDEX_LSN]		= "index_lsn",
	[VY_LOG_KEY_RANGE_ID]		= "range_id",
	[VY_LOG_KEY_RUN_ID]		= "run_id",
	[VY_LOG_KEY_BEGIN]		= "begin",
	[VY_LOG_KEY_END]		= "end",
	[VY_LOG_KEY_INDEX_ID]		= "index_id",
	[VY_LOG_KEY_SPACE_ID]		= "space_id",
	[VY_LOG_KEY_DEF]		= "key_def",
	[VY_LOG_KEY_SLICE_ID]		= "slice_id",
	[VY_LOG_KEY_DUMP_LSN]		= "dump_lsn",
	[VY_LOG_KEY_GC_LSN]		= "gc_lsn",
	[VY_LOG_KEY_TRUNCATE_COUNT]	= "truncate_count",
};

/** vy_log_type -> human readable name. */
static const char *vy_log_type_name[] = {
	[VY_LOG_CREATE_INDEX]		= "create_index",
	[VY_LOG_DROP_INDEX]		= "drop_index",
	[VY_LOG_INSERT_RANGE]		= "insert_range",
	[VY_LOG_DELETE_RANGE]		= "delete_range",
	[VY_LOG_PREPARE_RUN]		= "prepare_run",
	[VY_LOG_CREATE_RUN]		= "create_run",
	[VY_LOG_DROP_RUN]		= "drop_run",
	[VY_LOG_FORGET_RUN]		= "forget_run",
	[VY_LOG_INSERT_SLICE]		= "insert_slice",
	[VY_LOG_DELETE_SLICE]		= "delete_slice",
	[VY_LOG_DUMP_INDEX]		= "dump_index",
	[VY_LOG_SNAPSHOT]		= "snapshot",
	[VY_LOG_TRUNCATE_INDEX]		= "truncate_index",
};

struct vy_recovery;

/** Metadata log object. */
struct vy_log {
	/**
	 * The directory where log files are stored.
	 * Note, dir.index contains vclocks of all snapshots,
	 * even those that didn't result in file creation.
	 */
	struct xdir dir;
	/** Last checkpoint vclock. */
	struct vclock last_checkpoint;
	/** Recovery context. */
	struct vy_recovery *recovery;
	/** Latch protecting the log buffer. */
	struct latch latch;
	/**
	 * Next ID to use for a vinyl object.
	 * Used by vy_log_next_id().
	 */
	int64_t next_id;
	/** A region of struct vy_log_record entries. */
	struct region pool;
	/**
	 * Records awaiting to be written to disk.
	 * Linked by vy_log_record::in_tx;
	 */
	struct stailq tx;
	/** Number of entries in the @tx list. */
	int tx_size;
	/** Start of the current transaction in the pool, for rollback */
	size_t tx_svp;
	/**
	 * Last record in the queue at the time when the current
	 * transaction was started. Used for rollback.
	 */
	struct stailq_entry *tx_begin;
	/**
	 * Flag set if vy_log_write() failed.
	 *
	 * It indicates that that the current transaction must be
	 * aborted on vy_log_commit(). Thanks to this flag, we don't
	 * need to add error handling code after each invocation of
	 * vy_log_write(), instead we only check vy_log_commit()
	 * return code.
	 */
	bool tx_failed;
	/**
	 * Diagnostic area where vy_log_write() error is stored,
	 * only relevant if @tx_failed is set.
	 */
	struct diag tx_diag;
};
static struct vy_log vy_log;

/** Recovery context. */
struct vy_recovery {
	/** space_id, index_id -> vy_index_recovery_info. */
	struct mh_i64ptr_t *index_id_hash;
	/** index_lsn -> vy_index_recovery_info. */
	struct mh_i64ptr_t *index_lsn_hash;
	/** ID -> vy_range_recovery_info. */
	struct mh_i64ptr_t *range_hash;
	/** ID -> vy_run_recovery_info. */
	struct mh_i64ptr_t *run_hash;
	/** ID -> vy_slice_recovery_info. */
	struct mh_i64ptr_t *slice_hash;
	/**
	 * Maximal vinyl object ID, according to the metadata log,
	 * or -1 in case no vinyl objects were recovered.
	 */
	int64_t max_id;
};

/** Vinyl index info stored in a recovery context. */
struct vy_index_recovery_info {
	/** LSN of the index creation. */
	int64_t index_lsn;
	/** Ordinal index number in the space. */
	uint32_t index_id;
	/** Space ID. */
	uint32_t space_id;
	/** Array of key part definitions. */
	struct key_part_def *key_parts;
	/** Number of key parts. */
	uint32_t key_part_count;
	/** True if the index was dropped. */
	bool is_dropped;
	/** LSN of the last index dump. */
	int64_t dump_lsn;
	/** Truncate count. */
	int64_t truncate_count;
	/**
	 * List of all ranges in the index, linked by
	 * vy_range_recovery_info::in_index.
	 */
	struct rlist ranges;
	/**
	 * List of all runs created for the index
	 * (both committed and not), linked by
	 * vy_run_recovery_info::in_index.
	 */
	struct rlist runs;
};

/** Vinyl range info stored in a recovery context. */
struct vy_range_recovery_info {
	/** Link in vy_index_recovery_info::ranges. */
	struct rlist in_index;
	/** ID of the range. */
	int64_t id;
	/** Start of the range, stored in MsgPack array. */
	char *begin;
	/** End of the range, stored in MsgPack array. */
	char *end;
	/**
	 * List of all slices in the range, linked by
	 * vy_slice_recovery_info::in_range.
	 *
	 * Newer slices are closer to the head.
	 */
	struct rlist slices;
};

/** Run info stored in a recovery context. */
struct vy_run_recovery_info {
	/** Link in vy_index_recovery_info::runs. */
	struct rlist in_index;
	/** ID of the run. */
	int64_t id;
	/** Max LSN stored on disk. */
	int64_t dump_lsn;
	/**
	 * For deleted runs: LSN of the last checkpoint
	 * that uses this run.
	 */
	int64_t gc_lsn;
	/**
	 * True if the run was not committed (there's
	 * VY_LOG_PREPARE_RUN, but no VY_LOG_CREATE_RUN).
	 */
	bool is_incomplete;
	/** True if the run was dropped (VY_LOG_DROP_RUN). */
	bool is_dropped;
};

/** Slice info stored in a recovery context. */
struct vy_slice_recovery_info {
	/** Link in vy_range_recovery_info::slices. */
	struct rlist in_range;
	/** ID of the slice. */
	int64_t id;
	/** Run this slice was created for. */
	struct vy_run_recovery_info *run;
	/** Start of the slice, stored in MsgPack array. */
	char *begin;
	/** End of the slice, stored in MsgPack array. */
	char *end;
};

static struct vy_recovery *
vy_recovery_new_locked(int64_t signature, bool only_checkpoint);

/**
 * Return the name of the vylog file that has the given signature.
 */
static inline const char *
vy_log_filename(int64_t signature)
{
	return xdir_format_filename(&vy_log.dir, signature, NONE);
}

/**
 * Return the lsn of the checkpoint that was taken
 * before the given lsn.
 */
static int64_t
vy_log_prev_checkpoint(int64_t lsn)
{
	int64_t ret = -1;
	for (struct vclock *vclock = vclockset_last(&vy_log.dir.index);
	     vclock != NULL;
	     vclock = vclockset_prev(&vy_log.dir.index, vclock)) {
		if (vclock_sum(vclock) < lsn) {
			ret = vclock_sum(vclock);
			break;
		}
	}
	return ret;
}

/** An snprint-style function to print a log record. */
static int
vy_log_record_snprint(char *buf, int size, const struct vy_log_record *record)
{
	int total = 0;
	assert(record->type < vy_log_record_type_MAX);
	SNPRINT(total, snprintf, buf, size, "%s{",
		vy_log_type_name[record->type]);
	if (record->index_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_INDEX_LSN],
			record->index_lsn);
	if (record->range_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RANGE_ID],
			record->range_id);
	if (record->run_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_RUN_ID],
			record->run_id);
	if (record->begin != NULL) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_BEGIN]);
		SNPRINT(total, mp_snprint, buf, size, record->begin);
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (record->end != NULL) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_END]);
		SNPRINT(total, mp_snprint, buf, size, record->end);
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (record->index_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_INDEX_ID], record->index_id);
	if (record->space_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIu32", ",
			vy_log_key_name[VY_LOG_KEY_SPACE_ID],
			record->space_id);
	if (record->key_parts != NULL) {
		SNPRINT(total, snprintf, buf, size, "%s=",
			vy_log_key_name[VY_LOG_KEY_DEF]);
		SNPRINT(total, key_def_snprint_parts, buf, size,
			record->key_parts, record->key_part_count);
		SNPRINT(total, snprintf, buf, size, ", ");
	}
	if (record->slice_id > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_SLICE_ID],
			record->slice_id);
	if (record->dump_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_DUMP_LSN],
			record->dump_lsn);
	if (record->gc_lsn > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_GC_LSN],
			record->gc_lsn);
	if (record->truncate_count > 0)
		SNPRINT(total, snprintf, buf, size, "%s=%"PRIi64", ",
			vy_log_key_name[VY_LOG_KEY_TRUNCATE_COUNT],
			record->truncate_count);
	SNPRINT(total, snprintf, buf, size, "}");
	return total;
}

/**
 * Return a string containing a human readable representation
 * of a log record.
 */
static const char *
vy_log_record_str(const struct vy_log_record *record)
{
	char *buf = tt_static_buf();
	if (vy_log_record_snprint(buf, TT_STATIC_BUF_LEN, record) < 0)
		return "<failed to format vy_log log record>";
	return buf;
}

/**
 * Encode a log record into an xrow to be further written to an xlog.
 * Return 0 on success, -1 on failure.
 *
 * When stored in xlog, a vinyl metadata log has the following MsgPack
 * representation:
 *
 * [ type, { key: value, ... } ]
 *
 * 'type': see vy_log_record_type enum
 * 'key': see vy_log_key enum
 * 'value': depends on 'key'
 */
static int
vy_log_record_encode(const struct vy_log_record *record,
		     struct xrow_header *row)
{
	assert(record->type < vy_log_record_type_MAX);

	/*
	 * Calculate record size.
	 */
	size_t size = 0;
	size += mp_sizeof_array(2);
	size += mp_sizeof_uint(record->type);
	size_t n_keys = 0;
	if (record->index_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_INDEX_LSN);
		size += mp_sizeof_uint(record->index_lsn);
		n_keys++;
	}
	if (record->range_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_RANGE_ID);
		size += mp_sizeof_uint(record->range_id);
		n_keys++;
	}
	if (record->run_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_RUN_ID);
		size += mp_sizeof_uint(record->run_id);
		n_keys++;
	}
	if (record->begin != NULL) {
		size += mp_sizeof_uint(VY_LOG_KEY_BEGIN);
		const char *p = record->begin;
		assert(mp_typeof(*p) == MP_ARRAY);
		mp_next(&p);
		size += p - record->begin;
		n_keys++;
	}
	if (record->end != NULL) {
		size += mp_sizeof_uint(VY_LOG_KEY_END);
		const char *p = record->end;
		assert(mp_typeof(*p) == MP_ARRAY);
		mp_next(&p);
		size += p - record->end;
		n_keys++;
	}
	if (record->index_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_INDEX_ID);
		size += mp_sizeof_uint(record->index_id);
		n_keys++;
	}
	if (record->space_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_SPACE_ID);
		size += mp_sizeof_uint(record->space_id);
		n_keys++;
	}
	if (record->key_parts != NULL) {
		size += mp_sizeof_uint(VY_LOG_KEY_DEF);
		size += mp_sizeof_array(record->key_part_count);
		size += key_def_sizeof_parts(record->key_parts,
					     record->key_part_count);
		n_keys++;
	}
	if (record->slice_id > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_SLICE_ID);
		size += mp_sizeof_uint(record->slice_id);
		n_keys++;
	}
	if (record->dump_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_DUMP_LSN);
		size += mp_sizeof_uint(record->dump_lsn);
		n_keys++;
	}
	if (record->gc_lsn > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_GC_LSN);
		size += mp_sizeof_uint(record->gc_lsn);
		n_keys++;
	}
	if (record->truncate_count > 0) {
		size += mp_sizeof_uint(VY_LOG_KEY_TRUNCATE_COUNT);
		size += mp_sizeof_uint(record->truncate_count);
		n_keys++;
	}
	size += mp_sizeof_map(n_keys);

	/*
	 * Encode record.
	 */
	char *tuple = region_alloc(&fiber()->gc, size);
	if (tuple == NULL) {
		diag_set(OutOfMemory, size, "region", "vy_log record");
		return -1;
	}
	char *pos = tuple;
	pos = mp_encode_array(pos, 2);
	pos = mp_encode_uint(pos, record->type);
	pos = mp_encode_map(pos, n_keys);
	if (record->index_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_INDEX_LSN);
		pos = mp_encode_uint(pos, record->index_lsn);
	}
	if (record->range_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RANGE_ID);
		pos = mp_encode_uint(pos, record->range_id);
	}
	if (record->run_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_RUN_ID);
		pos = mp_encode_uint(pos, record->run_id);
	}
	if (record->begin != NULL) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_BEGIN);
		const char *p = record->begin;
		mp_next(&p);
		memcpy(pos, record->begin, p - record->begin);
		pos += p - record->begin;
	}
	if (record->end != NULL) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_END);
		const char *p = record->end;
		mp_next(&p);
		memcpy(pos, record->end, p - record->end);
		pos += p - record->end;
	}
	if (record->index_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_INDEX_ID);
		pos = mp_encode_uint(pos, record->index_id);
	}
	if (record->space_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_SPACE_ID);
		pos = mp_encode_uint(pos, record->space_id);
	}
	if (record->key_parts != NULL) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_DEF);
		pos = mp_encode_array(pos, record->key_part_count);
		pos = key_def_encode_parts(pos, record->key_parts,
					   record->key_part_count);
	}
	if (record->slice_id > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_SLICE_ID);
		pos = mp_encode_uint(pos, record->slice_id);
	}
	if (record->dump_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_DUMP_LSN);
		pos = mp_encode_uint(pos, record->dump_lsn);
	}
	if (record->gc_lsn > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_GC_LSN);
		pos = mp_encode_uint(pos, record->gc_lsn);
	}
	if (record->truncate_count > 0) {
		pos = mp_encode_uint(pos, VY_LOG_KEY_TRUNCATE_COUNT);
		pos = mp_encode_uint(pos, record->truncate_count);
	}
	assert(pos == tuple + size);

	/*
	 * Store record in xrow.
	 */
	struct request req;
	memset(&req, 0, sizeof(req));
	req.type = IPROTO_INSERT;
	req.tuple = tuple;
	req.tuple_end = pos;
	memset(row, 0, sizeof(*row));
	row->type = req.type;
	row->bodycnt = xrow_encode_dml(&req, row->body);
	return 0;
}

/**
 * Decode a log record from an xrow.
 * Return 0 on success, -1 on failure.
 */
static int
vy_log_record_decode(struct vy_log_record *record,
		     struct xrow_header *row)
{
	char *buf;

	memset(record, 0, sizeof(*record));

	struct request req;
	if (xrow_decode_dml(row, &req, 1ULL << IPROTO_TUPLE) != 0) {
		diag_log();
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 "Bad record: failed to decode request");
		return -1;
	}

	const char *tmp, *pos = req.tuple;

	uint32_t array_size = mp_decode_array(&pos);
	if (array_size != 2) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Bad record: wrong array size "
				    "(expected %d, got %u)",
				    2, (unsigned)array_size));
		goto fail;
	}

	record->type = mp_decode_uint(&pos);
	if (record->type >= vy_log_record_type_MAX) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Bad record: unknown record type %d",
				    record->type));
		goto fail;
	}

	uint32_t n_keys = mp_decode_map(&pos);
	for (uint32_t i = 0; i < n_keys; i++) {
		uint32_t key = mp_decode_uint(&pos);
		switch (key) {
		case VY_LOG_KEY_INDEX_LSN:
			record->index_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RANGE_ID:
			record->range_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_RUN_ID:
			record->run_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_BEGIN:
			tmp = pos;
			record->begin = mp_decode_array(&tmp) > 0 ? pos : NULL;
			mp_next(&pos);
			break;
		case VY_LOG_KEY_END:
			tmp = pos;
			record->end = mp_decode_array(&tmp) > 0 ? pos : NULL;
			mp_next(&pos);
			break;
		case VY_LOG_KEY_INDEX_ID:
			record->index_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_SPACE_ID:
			record->space_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_DEF: {
			uint32_t part_count = mp_decode_array(&pos);
			struct key_part_def *parts = region_alloc(&fiber()->gc,
						sizeof(*parts) * part_count);
			if (parts == NULL) {
				diag_set(OutOfMemory,
					 sizeof(*parts) * part_count,
					 "region", "struct key_part_def");
				return -1;
			}
			if (key_def_decode_parts(parts, part_count, &pos,
						 NULL, 0) != 0) {
				diag_log();
				diag_set(ClientError, ER_INVALID_VYLOG_FILE,
					 "Bad record: failed to decode "
					 "index key definition");
				goto fail;
			}
			record->key_parts = parts;
			record->key_part_count = part_count;
			break;
		}
		case VY_LOG_KEY_SLICE_ID:
			record->slice_id = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_DUMP_LSN:
			record->dump_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_GC_LSN:
			record->gc_lsn = mp_decode_uint(&pos);
			break;
		case VY_LOG_KEY_TRUNCATE_COUNT:
			record->truncate_count = mp_decode_uint(&pos);
			break;
		default:
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Bad record: unknown key %u",
					    (unsigned)key));
			goto fail;
		}
	}
	return 0;
fail:
	buf = tt_static_buf();
	mp_snprint(buf, TT_STATIC_BUF_LEN, req.tuple);
	say_error("failed to decode vylog record: %s", buf);
	return -1;
}

/**
 * Duplicate a log record. All objects refered to by the record
 * are duplicated as well.
 */
static struct vy_log_record *
vy_log_record_dup(struct region *pool, const struct vy_log_record *src)
{
	size_t used = region_used(pool);

	struct vy_log_record *dst = region_alloc(pool, sizeof(*dst));
	if (dst == NULL) {
		diag_set(OutOfMemory, sizeof(*dst),
			 "region", "struct vy_log_record");
		goto err;
	}
	*dst = *src;
	if (src->begin != NULL) {
		const char *data = src->begin;
		mp_next(&data);
		size_t size = data - src->begin;
		dst->begin = region_alloc(pool, size);
		if (dst->begin == NULL) {
			diag_set(OutOfMemory, size, "region",
				 "vy_log_record::begin");
			goto err;
		}
		memcpy((char *)dst->begin, src->begin, size);
	}
	if (src->end != NULL) {
		const char *data = src->end;
		mp_next(&data);
		size_t size = data - src->end;
		dst->end = region_alloc(pool, size);
		if (dst->end == NULL) {
			diag_set(OutOfMemory, size, "region",
				 "struct vy_log_record");
			goto err;
		}
		memcpy((char *)dst->end, src->end, size);
	}
	if (src->key_def != NULL) {
		size_t size = src->key_def->part_count *
				sizeof(struct key_part_def);
		dst->key_parts = region_alloc(pool, size);
		if (dst->key_parts == NULL) {
			diag_set(OutOfMemory, size, "region",
				 "struct key_part_def");
			goto err;
		}
		key_def_dump_parts(src->key_def, dst->key_parts);
		dst->key_part_count = src->key_def->part_count;
		dst->key_def = NULL;
	}
	return dst;

err:
	region_truncate(pool, used);
	return NULL;
}

void
vy_log_init(const char *dir)
{
	xdir_create(&vy_log.dir, dir, VYLOG, &INSTANCE_UUID);
	latch_create(&vy_log.latch);
	region_create(&vy_log.pool, cord_slab_cache());
	stailq_create(&vy_log.tx);
	diag_create(&vy_log.tx_diag);
	wal_init_vy_log();
}

/**
 * Try to flush the log buffer to disk.
 *
 * We always flush the entire vy_log buffer as a single xlog
 * transaction, since we do not track boundaries of @no_discard
 * buffered transactions, and want to avoid a partial write.
 */
static int
vy_log_flush(void)
{
	if (vy_log.tx_size == 0)
		return 0; /* nothing to do */

	ERROR_INJECT(ERRINJ_VY_LOG_FLUSH, {
		diag_set(ClientError, ER_INJECTION, "vinyl log flush");
		return -1;
	});

	struct journal_entry *entry = journal_entry_new(vy_log.tx_size);
	if (entry == NULL)
		return -1;

	struct xrow_header *rows;
	rows = region_aligned_alloc(&fiber()->gc,
				    vy_log.tx_size * sizeof(struct xrow_header),
				    alignof(struct xrow_header));
	if (rows == NULL)
		return -1;

	/*
	 * Encode buffered records.
	 */
	int i = 0;
	struct vy_log_record *record;
	stailq_foreach_entry(record, &vy_log.tx, in_tx) {
		assert(i < vy_log.tx_size);
		struct xrow_header *row = &rows[i];
		if (vy_log_record_encode(record, row) < 0)
			return -1;
		entry->rows[i] = row;
		i++;
	}
	assert(i == vy_log.tx_size);

	/*
	 * Do actual disk writes on behalf of the WAL
	 * so as not to block the tx thread.
	 */
	if (wal_write_vy_log(entry) != 0)
		return -1;

	/* Success. Free flushed records. */
	region_reset(&vy_log.pool);
	stailq_create(&vy_log.tx);
	vy_log.tx_size = 0;
	return 0;
}

void
vy_log_free(void)
{
	xdir_destroy(&vy_log.dir);
	latch_destroy(&vy_log.latch);
	region_destroy(&vy_log.pool);
	diag_destroy(&vy_log.tx_diag);
}

int
vy_log_open(struct xlog *xlog)
{
	/*
	 * Open the current log file or create a new one
	 * if it doesn't exist.
	 */
	const char *path = vy_log_filename(vclock_sum(&vy_log.last_checkpoint));
	if (access(path, F_OK) == 0)
		return xlog_open(xlog, path);

	if (errno != ENOENT) {
		diag_set(SystemError, "failed to access file '%s'", path);
		goto fail;
	}

	if (xdir_create_xlog(&vy_log.dir, xlog,
			     &vy_log.last_checkpoint) < 0)
		goto fail;

	struct xrow_header row;
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_SNAPSHOT;
	if (vy_log_record_encode(&record, &row) < 0 ||
	    xlog_write_row(xlog, &row) < 0)
		goto fail_close_xlog;

	if (xlog_rename(xlog) < 0)
		goto fail_close_xlog;

	return 0;

fail_close_xlog:
	if (unlink(xlog->filename) < 0)
		say_syserror("failed to delete file '%s'", xlog->filename);
	xlog_close(xlog, false);
fail:
	return -1;
}

int64_t
vy_log_next_id(void)
{
	return vy_log.next_id++;
}

int
vy_log_bootstrap(void)
{
	/*
	 * Scan the directory to make sure there is no
	 * vylog files left from previous setups.
	 */
	if (xdir_scan(&vy_log.dir) < 0 && errno != ENOENT)
		return -1;
	if (xdir_last_vclock(&vy_log.dir, NULL) >= 0)
		panic("vinyl directory is not empty");

	/* Add initial vclock to the xdir. */
	struct vclock *vclock = malloc(sizeof(*vclock));
	if (vclock == NULL) {
		diag_set(OutOfMemory, sizeof(*vclock),
			 "malloc", "struct vclock");
		return -1;
	}
	vclock_create(vclock);
	xdir_add_vclock(&vy_log.dir, vclock);
	return 0;
}

struct vy_recovery *
vy_log_begin_recovery(const struct vclock *vclock)
{
	assert(vy_log.recovery == NULL);

	/*
	 * Do not fail recovery if vinyl directory does not exist,
	 * because vinyl might not be even in use. Complain only
	 * on an attempt to write a vylog.
	 */
	if (xdir_scan(&vy_log.dir) < 0 && errno != ENOENT)
		return NULL;

	struct vclock vy_log_vclock;
	vclock_create(&vy_log_vclock);
	if (xdir_last_vclock(&vy_log.dir, &vy_log_vclock) >= 0 &&
	    vclock_compare(&vy_log_vclock, vclock) > 0) {
		/*
		 * Last vy_log log is newer than the last snapshot.
		 * This can't normally happen, as vy_log is rotated
		 * after snapshot is created. Looks like somebody
		 * deleted snap file, but forgot to delete vy_log.
		 */
		diag_set(ClientError, ER_MISSING_SNAPSHOT);
		return NULL;
	}

	struct vy_recovery *recovery;
	recovery = vy_recovery_new(vclock_sum(&vy_log_vclock), false);
	if (recovery == NULL)
		return NULL;

	vy_log.next_id = recovery->max_id + 1;
	vy_log.recovery = recovery;
	vclock_copy(&vy_log.last_checkpoint, vclock);
	return recovery;
}

static int
vy_log_create(const struct vclock *vclock, struct vy_recovery *recovery);

int
vy_log_end_recovery(void)
{
	assert(vy_log.recovery != NULL);

	/* Flush all pending records. */
	if (vy_log_flush() < 0) {
		diag_log();
		say_error("failed to flush vylog after recovery");
		return -1;
	}

	/*
	 * On backup we copy files corresponding to the most recent
	 * checkpoint. Since vy_log does not create snapshots of its log
	 * files, but instead appends records written after checkpoint
	 * to the most recent log file, the signature of the vy_log file
	 * corresponding to the last checkpoint equals the signature
	 * of the previous checkpoint. So upon successful recovery
	 * from a backup we need to rotate the log to keep checkpoint
	 * and vy_log signatures in sync.
	 */
	struct vclock *vclock = vclockset_last(&vy_log.dir.index);
	if (vclock == NULL ||
	    vclock_compare(vclock, &vy_log.last_checkpoint) != 0) {
		vclock = malloc(sizeof(*vclock));
		if (vclock == NULL) {
			diag_set(OutOfMemory, sizeof(*vclock),
				 "malloc", "struct vclock");
			return -1;
		}
		vclock_copy(vclock, &vy_log.last_checkpoint);
		xdir_add_vclock(&vy_log.dir, vclock);
		if (vy_log_create(vclock, vy_log.recovery) < 0) {
			diag_log();
			say_error("failed to write `%s'",
				  vy_log_filename(vclock_sum(vclock)));
			return -1;
		}
	}

	vy_log.recovery = NULL;
	return 0;
}

/** Argument passed to vy_log_rotate_cb_func(). */
struct vy_log_rotate_cb_arg {
	struct xdir *dir;
	struct xlog *xlog;
	const struct vclock *vclock;
};

/** Callback passed to vy_recovery_iterate() for log rotation. */
static int
vy_log_rotate_cb_func(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_log_rotate_cb_arg *arg = cb_arg;
	struct xlog *xlog = arg->xlog;
	struct xrow_header row;

	say_verbose("save vylog record: %s", vy_log_record_str(record));

	/* Create the log file on the first write. */
	if (!xlog_is_open(xlog) &&
	    xdir_create_xlog(arg->dir, xlog, arg->vclock) < 0)
		return -1;

	if (vy_log_record_encode(record, &row) < 0 ||
	    xlog_write_row(xlog, &row) < 0)
		return -1;
	return 0;
}

/**
 * Create an vy_log file from a recovery context.
 */
static int
vy_log_create(const struct vclock *vclock, struct vy_recovery *recovery)
{
	/*
	 * Only create the log file if we have something
	 * to write to it.
	 */
	struct xlog xlog;
	xlog_clear(&xlog);

	say_verbose("saving vylog %lld", (long long)vclock_sum(vclock));

	struct vy_log_rotate_cb_arg arg = {
		.xlog = &xlog,
		.dir = &vy_log.dir,
		.vclock = vclock,
	};
	if (vy_recovery_iterate(recovery, vy_log_rotate_cb_func, &arg) < 0)
		goto err_write_xlog;

	if (!xlog_is_open(&xlog))
		goto done; /* nothing written */

	/* Mark the end of the snapshot. */
	struct xrow_header row;
	struct vy_log_record record;
	vy_log_record_init(&record);
	record.type = VY_LOG_SNAPSHOT;
	if (vy_log_record_encode(&record, &row) < 0 ||
	    xlog_write_row(&xlog, &row) < 0)
		goto err_write_xlog;

	/* Finalize the new xlog. */
	if (xlog_flush(&xlog) < 0 ||
	    xlog_sync(&xlog) < 0 ||
	    xlog_rename(&xlog) < 0)
		goto err_write_xlog;

	xlog_close(&xlog, false);
done:
	say_verbose("done saving vylog");
	return 0;

err_write_xlog:
	/* Delete the unfinished xlog. */
	if (xlog_is_open(&xlog)) {
		if (unlink(xlog.filename) < 0)
			say_syserror("failed to delete file '%s'",
				     xlog.filename);
		xlog_close(&xlog, false);
	}
	return -1;
}

static ssize_t
vy_log_rotate_f(va_list ap)
{
	struct vy_recovery *recovery = va_arg(ap, struct vy_recovery *);
	const struct vclock *vclock = va_arg(ap, const struct vclock *);
	return vy_log_create(vclock, recovery);
}

int
vy_log_rotate(const struct vclock *vclock)
{
	int64_t signature = vclock_sum(vclock);
	int64_t prev_signature = vclock_sum(&vy_log.last_checkpoint);

	assert(vy_log.recovery == NULL);

	/*
	 * This function is called right after bootstrap (by snapshot),
	 * in which case old and new signatures coincide and there's
	 * nothing we need to do.
	 */
	if (signature == prev_signature)
		return 0;

	assert(signature > prev_signature);

	struct vclock *new_vclock = malloc(sizeof(*new_vclock));
	if (new_vclock == NULL) {
		diag_set(OutOfMemory, sizeof(*new_vclock),
			 "malloc", "struct vclock");
		return -1;
	}
	vclock_copy(new_vclock, vclock);

	say_verbose("rotating vylog %lld => %lld",
		    (long long)prev_signature, (long long)signature);

	/*
	 * Lock out all concurrent log writers while we are rotating it.
	 * This effectively stalls the vinyl scheduler for a while, but
	 * this is acceptable, because (1) the log file is small and
	 * hence can be rotated fairly quickly so the stall isn't going
	 * to take too long and (2) dumps/compactions, which are scheduled
	 * by the scheduler, are rare events so there shouldn't be too
	 * many of them piling up due to log rotation.
	 */
	latch_lock(&vy_log.latch);

	struct vy_recovery *recovery;
	recovery = vy_recovery_new_locked(prev_signature, false);
	if (recovery == NULL)
		goto fail;

	/* Do actual work from coio so as not to stall tx thread. */
	int rc = coio_call(vy_log_rotate_f, recovery, vclock);
	vy_recovery_delete(recovery);
	if (rc < 0) {
		diag_log();
		say_error("failed to write `%s'", vy_log_filename(signature));
		goto fail;
	}

	/*
	 * Success. Close the old log. The new one will be opened
	 * automatically on the first write (see wal_write_vy_log()).
	 */
	wal_rotate_vy_log();
	vclock_copy(&vy_log.last_checkpoint, vclock);

	/* Add the new vclock to the xdir so that we can track it. */
	xdir_add_vclock(&vy_log.dir, new_vclock);

	latch_unlock(&vy_log.latch);
	say_verbose("done rotating vylog");
	return 0;
fail:
	latch_unlock(&vy_log.latch);
	free(new_vclock);
	return -1;
}

void
vy_log_collect_garbage(int64_t signature)
{
	/*
	 * Always keep the previous file, because
	 * it is still needed for backups.
	 */
	signature = vy_log_prev_checkpoint(signature);
	xdir_collect_garbage(&vy_log.dir, signature, true);
}

const char *
vy_log_backup_path(struct vclock *vclock)
{
	/*
	 * Use the previous log file, because the current one
	 * contains records written after the last checkpoint.
	 */
	int64_t lsn = vy_log_prev_checkpoint(vclock_sum(vclock));
	if (lsn < 0)
		return NULL;
	const char *path = vy_log_filename(lsn);
	if (access(path, F_OK) == -1 && errno == ENOENT)
		return NULL; /* vinyl not used */
	return path;
}

void
vy_log_tx_begin(void)
{
	latch_lock(&vy_log.latch);
	vy_log.tx_begin = stailq_last(&vy_log.tx);
	vy_log.tx_svp = region_used(&vy_log.pool);
	vy_log.tx_failed = false;
	say_verbose("begin vylog transaction");
}

/**
 * Commit a transaction started with vy_log_tx_begin().
 *
 * If @no_discard is set, pending records won't be expunged from the
 * buffer on failure, so that the next transaction will retry to write
 * them to disk.
 */
static int
vy_log_tx_do_commit(bool no_discard)
{
	struct stailq rollback;

	assert(latch_owner(&vy_log.latch) == fiber());

	if (vy_log.tx_failed) {
		/*
		 * vy_log_write() failed to append a record to tx.
		 * @no_discard transactions can't handle this.
		 */
		diag_move(&vy_log.tx_diag, diag_get());
		if (no_discard) {
			diag_log();
			panic("non-discardable vylog transaction failed");
		}
		goto rollback;
	}

	/*
	 * During recovery, we may replay records we failed to commit
	 * before restart (e.g. drop index). Since the log isn't open
	 * yet, simply leave them in the tx buffer to be flushed upon
	 * recovery completion.
	 */
	if (vy_log.recovery != NULL)
		goto done;

	if (vy_log_flush() != 0) {
		if (!no_discard)
			goto rollback;
		/*
		 * We were told not to discard the transaction on
		 * failure so just warn and leave it in the buffer.
		 */
		struct error *e = diag_last_error(diag_get());
		say_warn("failed to flush vylog: %s", e->errmsg);
	}

done:
	say_verbose("commit vylog transaction");
	latch_unlock(&vy_log.latch);
	return 0;

rollback:
	stailq_cut_tail(&vy_log.tx, vy_log.tx_begin, &rollback);
	region_truncate(&vy_log.pool, vy_log.tx_svp);
	vy_log.tx_size = 0;
	vy_log.tx_svp = 0;
	say_verbose("rollback vylog transaction");
	latch_unlock(&vy_log.latch);
	return -1;
}

int
vy_log_tx_commit(void)
{
	return vy_log_tx_do_commit(false);
}

void
vy_log_tx_try_commit(void)
{
	if (vy_log_tx_do_commit(true) != 0)
		unreachable();
}

void
vy_log_write(const struct vy_log_record *record)
{
	assert(latch_owner(&vy_log.latch) == fiber());

	struct vy_log_record *tx_record = vy_log_record_dup(&vy_log.pool,
							    record);
	if (tx_record == NULL) {
		diag_move(diag_get(), &vy_log.tx_diag);
		vy_log.tx_failed = true;
		return;
	}

	say_verbose("write vylog record: %s", vy_log_record_str(tx_record));

	stailq_add_tail_entry(&vy_log.tx, tx_record, in_tx);
	vy_log.tx_size++;
}

/**
 * Given space_id and index_id, return the corresponding key in
 * vy_recovery::index_id_hash map.
 */
static inline int64_t
vy_recovery_index_id_hash(uint32_t space_id, uint32_t index_id)
{
	return ((uint64_t)space_id << 32) + index_id;
}

/** Lookup a vinyl index in vy_recovery::index_id_hash map. */
static struct vy_index_recovery_info *
vy_recovery_lookup_index_by_id(struct vy_recovery *recovery,
			       uint32_t space_id, uint32_t index_id)
{
	int64_t key = vy_recovery_index_id_hash(space_id, index_id);
	struct mh_i64ptr_t *h = recovery->index_id_hash;
	mh_int_t k = mh_i64ptr_find(h, key, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl index in vy_recovery::index_lsn_hash map. */
static struct vy_index_recovery_info *
vy_recovery_lookup_index_by_lsn(struct vy_recovery *recovery, int64_t index_lsn)
{
	struct mh_i64ptr_t *h = recovery->index_lsn_hash;
	mh_int_t k = mh_i64ptr_find(h, index_lsn, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl range in vy_recovery::range_hash map. */
static struct vy_range_recovery_info *
vy_recovery_lookup_range(struct vy_recovery *recovery, int64_t range_id)
{
	struct mh_i64ptr_t *h = recovery->range_hash;
	mh_int_t k = mh_i64ptr_find(h, range_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl run in vy_recovery::run_hash map. */
static struct vy_run_recovery_info *
vy_recovery_lookup_run(struct vy_recovery *recovery, int64_t run_id)
{
	struct mh_i64ptr_t *h = recovery->run_hash;
	mh_int_t k = mh_i64ptr_find(h, run_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/** Lookup a vinyl slice in vy_recovery::slice_hash map. */
static struct vy_slice_recovery_info *
vy_recovery_lookup_slice(struct vy_recovery *recovery, int64_t slice_id)
{
	struct mh_i64ptr_t *h = recovery->slice_hash;
	mh_int_t k = mh_i64ptr_find(h, slice_id, NULL);
	if (k == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, k)->val;
}

/**
 * Handle a VY_LOG_CREATE_INDEX log record.
 * This function allocates a new vinyl index with ID @index_lsn
 * and inserts it to the hash.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_create_index(struct vy_recovery *recovery, int64_t index_lsn,
			 uint32_t index_id, uint32_t space_id,
			 const struct key_part_def *key_parts,
			 uint32_t key_part_count)
{
	struct vy_index_recovery_info *index;
	struct key_part_def *key_parts_copy;
	struct mh_i64ptr_node_t node;
	struct mh_i64ptr_t *h;
	mh_int_t k;

	/*
	 * Make a copy of the key definition to be used for
	 * the new index incarnation.
	 */
	if (key_parts == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Missing key definition for index %lld",
				    (long long)index_lsn));
		return -1;
	}
	key_parts_copy = malloc(sizeof(*key_parts) * key_part_count);
	if (key_parts_copy == NULL) {
		diag_set(OutOfMemory, sizeof(*key_parts) * key_part_count,
			 "malloc", "struct key_part_def");
		return -1;
	}
	memcpy(key_parts_copy, key_parts, sizeof(*key_parts) * key_part_count);

	/*
	 * Look up the index in the hash.
	 */
	h = recovery->index_id_hash;
	node.key = vy_recovery_index_id_hash(space_id, index_id);
	k = mh_i64ptr_find(h, node.key, NULL);
	index = (k != mh_end(h)) ? mh_i64ptr_node(h, k)->val : NULL;

	if (index == NULL) {
		/*
		 * This is the first time the index is created
		 * (there's no previous incarnation in the context).
		 * Allocate a node for the index and add it to
		 * the hash.
		 */
		index = malloc(sizeof(*index));
		if (index == NULL) {
			diag_set(OutOfMemory, sizeof(*index),
				 "malloc", "struct vy_index_recovery_info");
			free(key_parts_copy);
			return -1;
		}
		index->index_id = index_id;
		index->space_id = space_id;
		rlist_create(&index->ranges);
		rlist_create(&index->runs);

		node.val = index;
		if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
			diag_set(OutOfMemory, 0, "mh_i64ptr_put",
				 "mh_i64ptr_node_t");
			free(key_parts_copy);
			free(index);
			return -1;
		}
	} else {
		/*
		 * The index was dropped and recreated with the
		 * same ID. Update its key definition (because it
		 * could have changed since the last time it was
		 * used) and reset its state.
		 */
		if (!index->is_dropped) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Index %u/%u created twice",
					    (unsigned)space_id,
					    (unsigned)index_id));
			free(key_parts_copy);
			return -1;
		}
		assert(index->index_id == index_id);
		assert(index->space_id == space_id);
		free(index->key_parts);
	}

	index->index_lsn = index_lsn;
	index->key_parts = key_parts_copy;
	index->key_part_count = key_part_count;
	index->is_dropped = false;
	index->dump_lsn = -1;
	index->truncate_count = 0;

	/*
	 * Add the index to the LSN hash.
	 */
	h = recovery->index_lsn_hash;
	node.key = index_lsn;
	node.val = index;
	if (mh_i64ptr_find(h, index_lsn, NULL) != mh_end(h)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate index id %lld",
				    (long long)index_lsn));
		return -1;
	}
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put",
			 "mh_i64ptr_node_t");
		return -1;
	}
	return 0;
}

/**
 * Handle a VY_LOG_DROP_INDEX log record.
 * This function marks the vinyl index with ID @index_lsn as dropped.
 * All ranges and runs of the index must have been deleted by now.
 * Returns 0 on success, -1 if ID not found or index is already marked.
 */
static int
vy_recovery_drop_index(struct vy_recovery *recovery, int64_t index_lsn)
{
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index_by_lsn(recovery, index_lsn);
	if (index == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Index %lld deleted but not registered",
				    (long long)index_lsn));
		return -1;
	}
	if (index->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Index %lld deleted twice",
				    (long long)index_lsn));
		return -1;
	}
	if (!rlist_empty(&index->ranges)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Dropped index %lld has ranges",
				    (long long)index_lsn));
		return -1;
	}
	struct vy_run_recovery_info *run;
	rlist_foreach_entry(run, &index->runs, in_index) {
		if (!run->is_dropped && !run->is_incomplete) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Dropped index %lld has active "
					    "runs", (long long)index_lsn));
			return -1;
		}
	}
	index->is_dropped = true;
	return 0;
}

/**
 * Handle a VY_LOG_DUMP_INDEX log record.
 * This function updates LSN of the last dump of the vinyl index
 * with ID @index_lsn.
 * Returns 0 on success, -1 if ID not found or index is dropped.
 */
static int
vy_recovery_dump_index(struct vy_recovery *recovery,
		       int64_t index_lsn, int64_t dump_lsn)
{
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index_by_lsn(recovery, index_lsn);
	if (index == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Dump of unregistered index %lld",
				    (long long)index_lsn));
		return -1;
	}
	if (index->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Dump of deleted index %lld",
				    (long long)index_lsn));
		return -1;
	}
	index->dump_lsn = dump_lsn;
	return 0;
}

/**
 * Handle a VY_LOG_TRUNCATE_INDEX log record.
 * This function updates truncate_count of the index with ID @index_lsn.
 * Returns 0 on success, -1 if ID not found or index is dropped.
 */
static int
vy_recovery_truncate_index(struct vy_recovery *recovery,
			   int64_t index_lsn, int64_t truncate_count)
{
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index_by_lsn(recovery, index_lsn);
	if (index == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Truncation of unregistered index %lld",
				    (long long)index_lsn));
		return -1;
	}
	if (index->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Truncation of deleted index %lld",
				    (long long)index_lsn));
		return -1;
	}
	index->truncate_count = truncate_count;
	return 0;
}

/**
 * Allocate a vinyl run with ID @run_id and insert it to the hash.
 * Return the new run on success, NULL on OOM.
 */
static struct vy_run_recovery_info *
vy_recovery_do_create_run(struct vy_recovery *recovery, int64_t run_id)
{
	struct vy_run_recovery_info *run = malloc(sizeof(*run));
	if (run == NULL) {
		diag_set(OutOfMemory, sizeof(*run),
			 "malloc", "struct vy_run_recovery_info");
		return NULL;
	}
	struct mh_i64ptr_t *h = recovery->run_hash;
	struct mh_i64ptr_node_t node = { run_id, run };
	struct mh_i64ptr_node_t *old_node = NULL;
	if (mh_i64ptr_put(h, &node, &old_node, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(run);
		return NULL;
	}
	assert(old_node == NULL);
	run->id = run_id;
	run->dump_lsn = -1;
	run->gc_lsn = -1;
	run->is_incomplete = false;
	run->is_dropped = false;
	rlist_create(&run->in_index);
	if (recovery->max_id < run_id)
		recovery->max_id = run_id;
	return run;
}

/**
 * Handle a VY_LOG_PREPARE_RUN log record.
 * This function creates a new incomplete vinyl run with ID @run_id
 * and adds it to the list of runs of the index with ID @index_lsn.
 * Return 0 on success, -1 if run already exists, index not found,
 * or OOM.
 */
static int
vy_recovery_prepare_run(struct vy_recovery *recovery, int64_t index_lsn,
			int64_t run_id)
{
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index_by_lsn(recovery, index_lsn);
	if (index == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld created for unregistered "
				    "index %lld", (long long)run_id,
				    (long long)index_lsn));
		return -1;
	}
	if (vy_recovery_lookup_run(recovery, run_id) != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate run id %lld",
				    (long long)run_id));
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_do_create_run(recovery, run_id);
	if (run == NULL)
		return -1;
	run->is_incomplete = true;
	rlist_add_entry(&index->runs, run, in_index);
	return 0;
}

/**
 * Handle a VY_LOG_CREATE_RUN log record.
 * This function adds the vinyl run with ID @run_id to the list
 * of runs of the index with ID @index_lsn and marks it committed.
 * If the run does not exist, it will be created.
 * Return 0 on success, -1 if index not found, run or index
 * is dropped, or OOM.
 */
static int
vy_recovery_create_run(struct vy_recovery *recovery, int64_t index_lsn,
		       int64_t run_id, int64_t dump_lsn)
{
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index_by_lsn(recovery, index_lsn);
	if (index == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld created for unregistered "
				    "index %lld", (long long)run_id,
				    (long long)index_lsn));
		return -1;
	}
	if (index->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld created for deleted "
				    "index %lld", (long long)run_id,
				    (long long)index_lsn));
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run != NULL && run->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld committed after deletion",
				    (long long)run_id));
		return -1;
	}
	if (run == NULL) {
		run = vy_recovery_do_create_run(recovery, run_id);
		if (run == NULL)
			return -1;
	}
	run->dump_lsn = dump_lsn;
	run->is_incomplete = false;
	rlist_move_entry(&index->runs, run, in_index);
	return 0;
}

/**
 * Handle a VY_LOG_DROP_RUN log record.
 * This function marks the vinyl run with ID @run_id as deleted.
 * Note, the run is not removed from the recovery context until it is
 * "forgotten", because it is still needed for garbage collection.
 * Return 0 on success, -1 if run not found or already deleted.
 */
static int
vy_recovery_drop_run(struct vy_recovery *recovery, int64_t run_id,
		     int64_t gc_lsn)
{
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld deleted but not registered",
				    (long long)run_id));
		return -1;
	}
	if (run->is_dropped) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld deleted twice",
				    (long long)run_id));
		return -1;
	}
	run->is_dropped = true;
	run->gc_lsn = gc_lsn;
	return 0;
}

/**
 * Handle a VY_LOG_FORGET_RUN log record.
 * This function frees the vinyl run with ID @run_id.
 * Return 0 on success, -1 if run not found.
 */
static int
vy_recovery_forget_run(struct vy_recovery *recovery, int64_t run_id)
{
	struct mh_i64ptr_t *h = recovery->run_hash;
	mh_int_t k = mh_i64ptr_find(h, run_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Run %lld forgotten but not registered",
				    (long long)run_id));
		return -1;
	}
	struct vy_run_recovery_info *run = mh_i64ptr_node(h, k)->val;
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(run, in_index);
	free(run);
	return 0;
}

/**
 * Handle a VY_LOG_INSERT_RANGE log record.
 * This function allocates a new vinyl range with ID @range_id,
 * inserts it to the hash, and adds it to the list of ranges of the
 * index with ID @index_lsn.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_insert_range(struct vy_recovery *recovery, int64_t index_lsn,
			 int64_t range_id, const char *begin, const char *end)
{
	if (vy_recovery_lookup_range(recovery, range_id) != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate range id %lld",
				    (long long)range_id));
		return -1;
	}
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index_by_lsn(recovery, index_lsn);
	if (index == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Range %lld created for unregistered "
				    "index %lld", (long long)range_id,
				    (long long)index_lsn));
		return -1;
	}

	size_t size = sizeof(struct vy_range_recovery_info);
	const char *data;
	data = begin;
	if (data != NULL)
		mp_next(&data);
	size_t begin_size = data - begin;
	size += begin_size;
	data = end;
	if (data != NULL)
		mp_next(&data);
	size_t end_size = data - end;
	size += end_size;

	struct vy_range_recovery_info *range = malloc(size);
	if (range == NULL) {
		diag_set(OutOfMemory, size,
			 "malloc", "struct vy_range_recovery_info");
		return -1;
	}
	struct mh_i64ptr_t *h = recovery->range_hash;
	struct mh_i64ptr_node_t node = { range_id, range };
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(range);
		return -1;
	}
	range->id = range_id;
	if (begin != NULL) {
		range->begin = (void *)range + sizeof(*range);
		memcpy(range->begin, begin, begin_size);
	} else
		range->begin = NULL;
	if (end != NULL) {
		range->end = (void *)range + sizeof(*range) + begin_size;
		memcpy(range->end, end, end_size);
	} else
		range->end = NULL;
	rlist_create(&range->slices);
	rlist_add_entry(&index->ranges, range, in_index);
	if (recovery->max_id < range_id)
		recovery->max_id = range_id;
	return 0;
}

/**
 * Handle a VY_LOG_DELETE_RANGE log record.
 * This function frees the vinyl range with ID @range_id.
 * All slices of the range must have been deleted by now.
 * Return 0 on success, -1 if range not found.
 */
static int
vy_recovery_delete_range(struct vy_recovery *recovery, int64_t range_id)
{
	struct mh_i64ptr_t *h = recovery->range_hash;
	mh_int_t k = mh_i64ptr_find(h, range_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Range %lld deleted but not registered",
				    (long long)range_id));
		return -1;
	}
	struct vy_range_recovery_info *range = mh_i64ptr_node(h, k)->val;
	if (!rlist_empty(&range->slices)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Deleted range %lld has run slices",
				    (long long)range_id));
		return -1;
	}
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(range, in_index);
	free(range);
	return 0;
}

/**
 * Handle a VY_LOG_INSERT_SLICE log record.
 * This function allocates a new slice with ID @slice_id for
 * the run with ID @run_id, inserts it into the hash, and adds
 * it to the list of slices of the range with ID @range_id.
 * Return 0 on success, -1 on failure (ID collision or OOM).
 */
static int
vy_recovery_insert_slice(struct vy_recovery *recovery, int64_t range_id,
			 int64_t run_id, int64_t slice_id,
			 const char *begin, const char *end)
{
	if (vy_recovery_lookup_slice(recovery, slice_id) != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Duplicate slice id %lld",
				    (long long)slice_id));
		return -1;
	}
	struct vy_range_recovery_info *range;
	range = vy_recovery_lookup_range(recovery, range_id);
	if (range == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Slice %lld created for unregistered "
				    "range %lld", (long long)slice_id,
				    (long long)range_id));
		return -1;
	}
	struct vy_run_recovery_info *run;
	run = vy_recovery_lookup_run(recovery, run_id);
	if (run == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Slice %lld created for unregistered "
				    "run %lld", (long long)slice_id,
				    (long long)run_id));
		return -1;
	}

	size_t size = sizeof(struct vy_slice_recovery_info);
	const char *data;
	data = begin;
	if (data != NULL)
		mp_next(&data);
	size_t begin_size = data - begin;
	size += begin_size;
	data = end;
	if (data != NULL)
		mp_next(&data);
	size_t end_size = data - end;
	size += end_size;

	struct vy_slice_recovery_info *slice = malloc(size);
	if (slice == NULL) {
		diag_set(OutOfMemory, size,
			 "malloc", "struct vy_slice_recovery_info");
		return -1;
	}
	struct mh_i64ptr_t *h = recovery->slice_hash;
	struct mh_i64ptr_node_t node = { slice_id, slice };
	if (mh_i64ptr_put(h, &node, NULL, NULL) == mh_end(h)) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_put", "mh_i64ptr_node_t");
		free(slice);
		return -1;
	}
	slice->id = slice_id;
	slice->run = run;
	if (begin != NULL) {
		slice->begin = (void *)slice + sizeof(*slice);
		memcpy(slice->begin, begin, begin_size);
	} else
		slice->begin = NULL;
	if (end != NULL) {
		slice->end = (void *)slice + sizeof(*slice) + begin_size;
		memcpy(slice->end, end, end_size);
	} else
		slice->end = NULL;
	/*
	 * If dump races with compaction, an older slice created by
	 * compaction may be added after a newer slice created by
	 * dump. Make sure that the list stays sorted by LSN in any
	 * case.
	 */
	struct vy_slice_recovery_info *next_slice;
	rlist_foreach_entry(next_slice, &range->slices, in_range) {
		if (next_slice->run->dump_lsn < slice->run->dump_lsn)
			break;
	}
	rlist_add_tail(&next_slice->in_range, &slice->in_range);
	if (recovery->max_id < slice_id)
		recovery->max_id = slice_id;
	return 0;
}

/**
 * Handle a VY_LOG_DELETE_SLICE log record.
 * This function frees the vinyl slice with ID @slice_id.
 * Return 0 on success, -1 if slice not found.
 */
static int
vy_recovery_delete_slice(struct vy_recovery *recovery, int64_t slice_id)
{
	struct mh_i64ptr_t *h = recovery->slice_hash;
	mh_int_t k = mh_i64ptr_find(h, slice_id, NULL);
	if (k == mh_end(h)) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Slice %lld deleted but not registered",
				    (long long)slice_id));
		return -1;
	}
	struct vy_slice_recovery_info *slice = mh_i64ptr_node(h, k)->val;
	mh_i64ptr_del(h, k, NULL);
	rlist_del_entry(slice, in_range);
	free(slice);
	return 0;
}

/**
 * Update a recovery context with a new log record.
 * Return 0 on success, -1 on failure.
 *
 * The purpose of this function is to restore the latest consistent
 * view of the system by replaying the metadata log.
 */
static int
vy_recovery_process_record(struct vy_recovery *recovery,
			   const struct vy_log_record *record)
{
	int rc;
	switch (record->type) {
	case VY_LOG_CREATE_INDEX:
		rc = vy_recovery_create_index(recovery, record->index_lsn,
				record->index_id, record->space_id,
				record->key_parts, record->key_part_count);
		break;
	case VY_LOG_DROP_INDEX:
		rc = vy_recovery_drop_index(recovery, record->index_lsn);
		break;
	case VY_LOG_INSERT_RANGE:
		rc = vy_recovery_insert_range(recovery, record->index_lsn,
				record->range_id, record->begin, record->end);
		break;
	case VY_LOG_DELETE_RANGE:
		rc = vy_recovery_delete_range(recovery, record->range_id);
		break;
	case VY_LOG_PREPARE_RUN:
		rc = vy_recovery_prepare_run(recovery, record->index_lsn,
					     record->run_id);
		break;
	case VY_LOG_CREATE_RUN:
		rc = vy_recovery_create_run(recovery, record->index_lsn,
					    record->run_id, record->dump_lsn);
		break;
	case VY_LOG_DROP_RUN:
		rc = vy_recovery_drop_run(recovery, record->run_id,
					  record->gc_lsn);
		break;
	case VY_LOG_FORGET_RUN:
		rc = vy_recovery_forget_run(recovery, record->run_id);
		break;
	case VY_LOG_INSERT_SLICE:
		rc = vy_recovery_insert_slice(recovery, record->range_id,
					      record->run_id, record->slice_id,
					      record->begin, record->end);
		break;
	case VY_LOG_DELETE_SLICE:
		rc = vy_recovery_delete_slice(recovery, record->slice_id);
		break;
	case VY_LOG_DUMP_INDEX:
		rc = vy_recovery_dump_index(recovery, record->index_lsn,
					    record->dump_lsn);
		break;
	case VY_LOG_TRUNCATE_INDEX:
		rc = vy_recovery_truncate_index(recovery, record->index_lsn,
						record->truncate_count);
		break;
	default:
		unreachable();
	}
	if (rc != 0)
		say_error("failed to process vylog record: %s",
			  vy_log_record_str(record));
	return rc;
}

static ssize_t
vy_recovery_new_f(va_list ap)
{
	int64_t signature = va_arg(ap, int64_t);
	bool only_checkpoint = va_arg(ap, int);
	struct vy_recovery **p_recovery = va_arg(ap, struct vy_recovery **);

	say_verbose("loading vylog %lld", (long long)signature);

	struct vy_recovery *recovery = malloc(sizeof(*recovery));
	if (recovery == NULL) {
		diag_set(OutOfMemory, sizeof(*recovery),
			 "malloc", "struct vy_recovery");
		goto fail;
	}

	recovery->index_id_hash = NULL;
	recovery->index_lsn_hash = NULL;
	recovery->range_hash = NULL;
	recovery->run_hash = NULL;
	recovery->slice_hash = NULL;
	recovery->max_id = -1;

	recovery->index_id_hash = mh_i64ptr_new();
	recovery->index_lsn_hash = mh_i64ptr_new();
	recovery->range_hash = mh_i64ptr_new();
	recovery->run_hash = mh_i64ptr_new();
	recovery->slice_hash = mh_i64ptr_new();
	if (recovery->index_id_hash == NULL ||
	    recovery->index_lsn_hash == NULL ||
	    recovery->range_hash == NULL ||
	    recovery->run_hash == NULL ||
	    recovery->slice_hash == NULL) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_new", "mh_i64ptr_t");
		goto fail_free;
	}

	/*
	 * We don't create a log file if there are no objects to
	 * be stored in it, so if the log doesn't exist, assume
	 * the recovery context is empty.
	 */
	const char *path = vy_log_filename(signature);
	if (access(path, F_OK) < 0 && errno == ENOENT)
		goto out;

	struct xlog_cursor cursor;
	if (xdir_open_cursor(&vy_log.dir, signature, &cursor) < 0)
		goto fail_free;

	int rc;
	struct xrow_header row;
	while ((rc = xlog_cursor_next(&cursor, &row, false)) == 0) {
		struct vy_log_record record;
		rc = vy_log_record_decode(&record, &row);
		if (rc < 0)
			break;
		say_verbose("load vylog record: %s",
			    vy_log_record_str(&record));
		if (record.type == VY_LOG_SNAPSHOT) {
			if (only_checkpoint)
				break;
			continue;
		}
		rc = vy_recovery_process_record(recovery, &record);
		if (rc < 0)
			break;
		fiber_gc();
	}
	fiber_gc();
	if (rc < 0)
		goto fail_close;

	xlog_cursor_close(&cursor, false);
out:
	say_verbose("done loading vylog");
	*p_recovery = recovery;
	return 0;

fail_close:
	xlog_cursor_close(&cursor, false);
fail_free:
	vy_recovery_delete(recovery);
fail:
	return -1;
}

/**
 * Load the metadata log and return a recovery context.
 * Must be called with the log latch held.
 */
static struct vy_recovery *
vy_recovery_new_locked(int64_t signature, bool only_checkpoint)
{
	int rc;
	struct vy_recovery *recovery;

	assert(latch_owner(&vy_log.latch) == fiber());
	/*
	 * Before proceeding to log recovery, make sure that all
	 * pending records have been flushed out.
	 */
	rc = vy_log_flush();
	if (rc != 0) {
		diag_log();
		say_error("failed to flush vylog for recovery");
		return NULL;
	}

	/* Load the log from coio so as not to stall tx thread. */
	rc = coio_call(vy_recovery_new_f, signature,
		       (int)only_checkpoint, &recovery);
	if (rc != 0) {
		diag_log();
		say_error("failed to load `%s'", vy_log_filename(signature));
		return NULL;
	}
	return recovery;
}

struct vy_recovery *
vy_recovery_new(int64_t signature, bool only_checkpoint)
{
	/* Lock out concurrent writers while we are loading the log. */
	latch_lock(&vy_log.latch);
	struct vy_recovery *recovery;
	recovery = vy_recovery_new_locked(signature, only_checkpoint);
	latch_unlock(&vy_log.latch);
	return recovery;
}

/** Helper to delete mh_i64ptr_t along with all its records. */
static void
vy_recovery_delete_hash(struct mh_i64ptr_t *h)
{
	mh_int_t i;
	mh_foreach(h, i)
		free(mh_i64ptr_node(h, i)->val);
	mh_i64ptr_delete(h);
}

void
vy_recovery_delete(struct vy_recovery *recovery)
{
	if (recovery->index_id_hash != NULL) {
		mh_int_t i;
		mh_foreach(recovery->index_id_hash, i) {
			struct vy_index_recovery_info *index;
			index = mh_i64ptr_node(recovery->index_id_hash, i)->val;
			free(index->key_parts);
			free(index);
		}
		mh_i64ptr_delete(recovery->index_id_hash);
	}
	if (recovery->index_lsn_hash != NULL) {
		/* Hash entries were deleted along with index_id_hash. */
		mh_i64ptr_delete(recovery->index_lsn_hash);
	}
	if (recovery->range_hash != NULL)
		vy_recovery_delete_hash(recovery->range_hash);
	if (recovery->run_hash != NULL)
		vy_recovery_delete_hash(recovery->run_hash);
	if (recovery->slice_hash != NULL)
		vy_recovery_delete_hash(recovery->slice_hash);
	TRASH(recovery);
	free(recovery);
}

static int
vy_recovery_iterate_index(struct vy_index_recovery_info *index,
			  vy_recovery_cb cb, void *cb_arg)
{
	struct vy_range_recovery_info *range;
	struct vy_slice_recovery_info *slice;
	struct vy_run_recovery_info *run;
	struct vy_log_record record;

	vy_log_record_init(&record);
	record.type = VY_LOG_CREATE_INDEX;
	record.index_lsn = index->index_lsn;
	record.index_id = index->index_id;
	record.space_id = index->space_id;
	record.key_parts = index->key_parts;
	record.key_part_count = index->key_part_count;
	if (cb(&record, cb_arg) != 0)
		return -1;

	if (index->truncate_count > 0) {
		vy_log_record_init(&record);
		record.type = VY_LOG_TRUNCATE_INDEX;
		record.index_lsn = index->index_lsn;
		record.truncate_count = index->truncate_count;
		if (cb(&record, cb_arg) != 0)
			return -1;
	}

	if (index->dump_lsn >= 0) {
		vy_log_record_init(&record);
		record.type = VY_LOG_DUMP_INDEX;
		record.index_lsn = index->index_lsn;
		record.dump_lsn = index->dump_lsn;
		if (cb(&record, cb_arg) != 0)
			return -1;
	}

	rlist_foreach_entry(run, &index->runs, in_index) {
		vy_log_record_init(&record);
		if (run->is_incomplete) {
			record.type = VY_LOG_PREPARE_RUN;
		} else {
			record.type = VY_LOG_CREATE_RUN;
			record.dump_lsn = run->dump_lsn;
		}
		record.index_lsn = index->index_lsn;
		record.run_id = run->id;
		record.is_dropped = run->is_dropped;
		if (cb(&record, cb_arg) != 0)
			return -1;

		if (!run->is_dropped)
			continue;

		vy_log_record_init(&record);
		record.type = VY_LOG_DROP_RUN;
		record.run_id = run->id;
		record.gc_lsn = run->gc_lsn;
		if (cb(&record, cb_arg) != 0)
			return -1;
	}

	rlist_foreach_entry(range, &index->ranges, in_index) {
		vy_log_record_init(&record);
		record.type = VY_LOG_INSERT_RANGE;
		record.index_lsn = index->index_lsn;
		record.range_id = range->id;
		record.begin = range->begin;
		record.end = range->end;
		if (cb(&record, cb_arg) != 0)
			return -1;
		/*
		 * Newer slices are stored closer to the head of the list,
		 * while we are supposed to return slices in chronological
		 * order, so use reverse iterator.
		 */
		rlist_foreach_entry_reverse(slice, &range->slices, in_range) {
			vy_log_record_init(&record);
			record.type = VY_LOG_INSERT_SLICE;
			record.range_id = range->id;
			record.slice_id = slice->id;
			record.run_id = slice->run->id;
			record.begin = slice->begin;
			record.end = slice->end;
			if (cb(&record, cb_arg) != 0)
				return -1;
		}
	}

	if (index->is_dropped) {
		vy_log_record_init(&record);
		record.type = VY_LOG_DROP_INDEX;
		record.index_lsn = index->index_lsn;
		if (cb(&record, cb_arg) != 0)
			return -1;
	}
	return 0;
}

int
vy_recovery_iterate(struct vy_recovery *recovery,
		    vy_recovery_cb cb, void *cb_arg)
{
	mh_int_t i;
	mh_foreach(recovery->index_id_hash, i) {
		struct vy_index_recovery_info *index;
		index = mh_i64ptr_node(recovery->index_id_hash, i)->val;
		/*
		 * Purge dropped indexes that are not referenced by runs
		 * (and thus not needed for garbage collection) from the
		 * log on rotation.
		 */
		if (index->is_dropped && rlist_empty(&index->runs))
			continue;
		if (vy_recovery_iterate_index(index, cb, cb_arg) < 0)
			return -1;
	}
	return 0;
}

int
vy_recovery_load_index(struct vy_recovery *recovery,
		       uint32_t space_id, uint32_t index_id,
		       int64_t index_lsn, bool is_checkpoint_recovery,
		       vy_recovery_cb cb, void *cb_arg)
{
	struct vy_index_recovery_info *index;
	index = vy_recovery_lookup_index_by_id(recovery, space_id, index_id);
	if (index == NULL)
		return 0;
	/* See the comment to the function declaration. */
	if (index_lsn < index->index_lsn) {
		/*
		 * Loading a past incarnation of the index.
		 * Emit create/drop records to indicate that
		 * it is going to be dropped by a WAL statement
		 * and hence doesn't need to be recovered.
		 */
		struct vy_log_record record;
		vy_log_record_init(&record);
		record.type = VY_LOG_CREATE_INDEX;
		record.index_id = index->index_id;
		record.space_id = index->space_id;
		record.index_lsn = index_lsn;
		if (cb(&record, cb_arg) != 0)
			return -1;
		vy_log_record_init(&record);
		record.type = VY_LOG_DROP_INDEX;
		record.index_lsn = index_lsn;
		if (cb(&record, cb_arg) != 0)
			return -1;
		return 0;
	} else if (is_checkpoint_recovery || index_lsn == index->index_lsn) {
		/*
		 * Loading the last incarnation of the index.
		 * Replay all records we have recovered from
		 * the log for this index.
		 */
		return vy_recovery_iterate_index(index, cb, cb_arg);
	} else {
		/*
		 * The requested incarnation is missing in the recovery
		 * context, because we failed to log it before restart.
		 * Do nothing and let the caller retry logging.
		 */
		assert(!is_checkpoint_recovery);
		assert(index_lsn > index->index_lsn);
		return 0;
	}
}
