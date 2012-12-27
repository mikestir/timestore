/* FIXME!!!! Timestamps need to be signed 64-bit integers to allow for representing points
 * before 1970!
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include "tsdb.h"
#include "logging.h"

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof(a[0]))

#define PROFILE_STORE	\
	struct timeval pf_start; \
	struct timeval pf_stop; \
	struct timeval pf_duration;
#define PROFILE_START	{ \
	gettimeofday(&pf_start, NULL); \
	}
#define PROFILE_END(a)	{ \
	gettimeofday(&pf_stop, NULL); \
	timersub(&pf_stop, &pf_start, &pf_duration); \
	DEBUG("%s took %lu.%06lu s\n", a, (unsigned long)pf_duration.tv_sec, (unsigned long)pf_duration.tv_usec); \
	}

// FIXME: This should be programmable in some way
static uint32_t tsdb_default_interval = 30;
static uint32_t tsdb_default_decimation[] = {
	20, /* 10 minute intervals for daily views */
	6, /* hourly */
	6, /* 6 hourly */
	4, /* daily */
	7, /* weekly */
};

tsdb_ctx_t* tsdb_open(uint64_t node_id, unsigned int nmetrics, int flags)
{
	tsdb_ctx_t *ctx;
	char path[TSDB_PATH_MAX];
	struct stat st;
	unsigned int n, layer;
	uint_fast32_t max_decimation = 0;
	PROFILE_STORE;
	
	FUNCTION_TRACE;
	PROFILE_START;
	
	ctx = (tsdb_ctx_t*)calloc(1, sizeof(tsdb_ctx_t));
	if (ctx == NULL) {
		CRITICAL("Out of memory\n");
		return NULL;
	}
	
	/* Open and map dataset metadata, creating if non-existent */
	snprintf(path, TSDB_PATH_MAX, TSDB_METADATA_FORMAT, node_id);
	DEBUG("Node %016" PRIX64 " metadata path: %s\n", node_id, path);
	ctx->meta_fd = open(path, O_RDWR | ((flags & TSDB_CREATE) ? O_CREAT : 0), 0644);
	if (ctx->meta_fd < 0) {
		ERROR("Error opening metadata %s: %s\n", path, strerror(errno));
		goto fail;
	}
	fstat(ctx->meta_fd, &st);
	if (st.st_size && st.st_size != sizeof(tsdb_metadata_t)) {
		ERROR("Metadata file length is invalid - truncating\n");
		ftruncate(ctx->meta_fd, 0);
		fstat(ctx->meta_fd, &st);
	}
	if (st.st_size == 0) {
		tsdb_metadata_t md;
		
		if (!(flags & TSDB_CREATE)) {
			ERROR("Corrupt metadata\n");
			goto fail;
		}
		
		/* Populate fresh metadata */
		INFO("Empty metadata - populating new dataset\n");
		memset(&md, 0, sizeof(md));
		md.magic = TSDB_MAGIC_META;
		md.version = TSDB_VERSION;
		md.node_id = node_id;
		md.nmetrics = nmetrics;
		md.npoints = 0;
		md.start_time = 0;
		md.interval = tsdb_default_interval; // FIXME
		for (n = 0; n < ARRAY_SIZE(tsdb_default_decimation); n++) {
			DEBUG("%d %" PRIu32 "\n", n, tsdb_default_decimation[n]);
			md.decimation[n] = tsdb_default_decimation[n];
		}
		md.flags[0] = TSDB_PAD_UNKNOWN | TSDB_DOWNSAMPLE_MEAN; // FIXME
		write(ctx->meta_fd, &md, sizeof(tsdb_metadata_t));
	}
	ctx->meta = mmap(NULL, sizeof(tsdb_metadata_t), PROT_READ | PROT_WRITE,
		MAP_SHARED, ctx->meta_fd, 0);
	if (ctx->meta == MAP_FAILED) {
		ERROR("mmap failed on file %s: %s\n", path, strerror(errno));
		goto fail;
	}
	
	DEBUG("magic = 0x%08" PRIX32 "\n", ctx->meta->magic);
	DEBUG("version = %" PRIu32 "\n", ctx->meta->version);
	DEBUG("node_id = 0x%016" PRIX64 "\n", ctx->meta->node_id);
	DEBUG("nmetrics = %" PRIu32 "\n", ctx->meta->nmetrics);
	DEBUG("npoints = %" PRIu32 "\n", ctx->meta->npoints);
	DEBUG("start_time = %" PRIi64 "\n", ctx->meta->start_time);
	DEBUG("interval = %" PRIu32 "\n", ctx->meta->interval);
	for (n = 0; n < TSDB_MAX_LAYERS; n++)
		DEBUG("decimation[%d] = %" PRIu32 "\n", n, ctx->meta->decimation[n]);
	for (n = 0; n < TSDB_MAX_METRICS; n++)
		DEBUG("flags[%d] = 0x%08" PRIX32 "\n", n, ctx->meta->flags[n]);
	
	/* Check metadata for consistency */
	if (ctx->meta->magic != TSDB_MAGIC_META) {
		ERROR("Bad magic number\n");
		goto fail;
	}
	if (ctx->meta->version != TSDB_VERSION) {
		ERROR("Bad database version\n");
		goto fail;
	}
	if (ctx->meta->node_id != node_id) {
		ERROR("Incorrect node_id - possible data corruption\n");
		goto fail;
	}
	
	/* Allocate padding buffer */
	ctx->padding = malloc(TSDB_MAX_PADDING_BLOCK);
	if (ctx->padding == NULL) {
		CRITICAL("Out of memory\n");
		goto fail;
	}
	
	/* Open table files */
	for (layer = 0; layer < TSDB_MAX_LAYERS; layer++) {		
		snprintf(path, TSDB_PATH_MAX, TSDB_TABLE_FORMAT, node_id, layer);
		DEBUG("Node %016" PRIX64 " layer %u table path: %s\n", node_id, layer, path);
		ctx->table_fd[layer] = open(path, O_RDWR | O_CREAT, 0644);
		if (ctx->table_fd[layer] < 0) {
			ERROR("Error opening table for node %016" PRIX64 " layer %d: %s\n", 
				ctx->meta->node_id, layer, strerror(errno));
			goto fail;
		}
		
		/* Determine largest decimation step */
		if (ctx->meta->decimation[layer] > 0) {
			if (ctx->meta->decimation[layer] > max_decimation) {
				max_decimation = ctx->meta->decimation[layer];
			}
		} else {
			/* No more layers */
			break;
		}
	}
	
	/* Allocate decimation buffer */
	if (max_decimation > 0) {
		DEBUG("Largest decimation step %" PRIuFAST32 "\n", max_decimation);
		ctx->dec_buffer = malloc(sizeof(tsdb_data_t) * ctx->meta->nmetrics * max_decimation);
		if (ctx->dec_buffer == NULL) {
			CRITICAL("Out of memory\n");
			goto fail;
		}
	}
	
	PROFILE_END("open");
	return ctx;
fail:
	PROFILE_END("open (failed)");
	tsdb_close(ctx);
	return NULL;
}

void tsdb_close(tsdb_ctx_t *ctx)
{
	unsigned int layer;
	
	FUNCTION_TRACE;
	
	/* Close any open table files */
	for (layer = 0; layer < TSDB_MAX_LAYERS; layer++) {
		 if (ctx->table_fd[layer] > 0) {
			 close(ctx->table_fd[layer]);
		 }
	}
	
	/* Free decimation block */
	if (ctx->dec_buffer != NULL) {
		free(ctx->dec_buffer);
	}
	
	/* Free padding block */
	if (ctx->padding != NULL) {
		free(ctx->padding);
	}
	
	/* Close metadata */
	if (ctx->meta != NULL) {
		munmap(ctx->meta, sizeof(tsdb_metadata_t));
	}
	if (ctx->meta_fd > 0) {
		close(ctx->meta_fd);
	}
	
	/* Release context */
	free(ctx);
}

/* FIXME: Don't really need to pass in timestamp */
static int tsdb_update_layer(tsdb_ctx_t *ctx, unsigned int layer, uint_fast32_t point, uint_fast32_t npoints,
	int64_t timestamp, tsdb_data_t *values)
{
	int rc = 0;
	unsigned int metric;
	tsdb_data_t *ptr;
	PROFILE_STORE;
	
	FUNCTION_TRACE;
	PROFILE_START;
	
	DEBUG("Values at %" PRIi64 " at point %" PRIuFAST32 " in layer %d\n", timestamp, point, layer);
	
	/* Pad missing values */
	if (point > npoints) {
		uint_fast32_t npadding = point - npoints;
		unsigned int pointsperblock = TSDB_MAX_PADDING_BLOCK / (sizeof(tsdb_data_t) * ctx->meta->nmetrics);
		unsigned int n;
		
		DEBUG("Padding %" PRIuFAST32 " points\n", npadding);
		
		/* Allocate and fill padding block buffer */
		if (npadding < pointsperblock)
			pointsperblock = npadding;
		ptr = ctx->padding;
		for (n = 0; n < pointsperblock; n++) {
			for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++, ptr++) {
				switch (ctx->meta->flags[metric] & TSDB_PAD_MASK) {
					case TSDB_PAD_UNKNOWN:
						*ptr = NAN;
						break;
					case TSDB_PAD_LAST:
						DEBUG("FIXME: PAD_LAST not implemented\n"); 
						*ptr = NAN; // FIXME:
						break;
					default:
						ERROR("Bad padding mode\n");
				}
			}
		}
		
		/* Write blocks to table file */
		rc = lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * npoints * ctx->meta->nmetrics, SEEK_SET);
		if (rc < 0) {
			goto fail;
		}
		do {
			if (npadding < pointsperblock)
				pointsperblock = npadding;
			DEBUG("%u points of %" PRIuFAST32 "\n", pointsperblock, npadding);
			rc = write(ctx->table_fd[layer], ctx->padding, sizeof(tsdb_data_t) * pointsperblock * ctx->meta->nmetrics);
			if (rc < 0) {
				goto fail;
			}
			npadding -= pointsperblock;
		} while (npadding);
		
	}
	
	/* Update/insert new values for all metrics that aren't set to NaN */
	rc = lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * point * ctx->meta->nmetrics, SEEK_SET);
	if (rc < 0) {
		goto fail;
	}
	ptr = values;
	for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++, ptr++) {
		/* TODO: Might get a speed-up here by writing adjacent valid metrics in one go */
		if (isnan(*ptr)) {
			/* Skip invalid values */
			rc = lseek(ctx->table_fd[layer], sizeof(tsdb_data_t), SEEK_CUR);
		} else {
			/* Write valid values */
			rc = write(ctx->table_fd[layer], ptr, sizeof(tsdb_data_t));
		}
		if (rc < 0) {
			goto fail;
		}
	}

	PROFILE_END("update layer");

	PROFILE_START;
	
	/* Decimate */
	if (ctx->meta->decimation[layer] > 0) {
		uint_fast32_t first_point;
		tsdb_data_t next_values[TSDB_MAX_METRICS];
		int count;
		unsigned int valid_count[TSDB_MAX_METRICS];
		
		/* Read contributing points to decimation buffer */
		first_point = (point / ctx->meta->decimation[layer]) * ctx->meta->decimation[layer];
		DEBUG("Decimate %" PRIu32 " points starting at %" PRIuFAST32 "\n",
		      ctx->meta->decimation[layer], first_point);
		rc = lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * first_point * ctx->meta->nmetrics, SEEK_SET);
		if (rc < 0) {
			goto fail;
		}
		count = read(ctx->table_fd[layer], ctx->dec_buffer, sizeof(tsdb_data_t) * ctx->meta->decimation[layer] * ctx->meta->nmetrics);
		if (count < 0) {
			rc = count;
			goto fail;
		}
		
		/* Calculate decimated values */
		for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++) {
			valid_count[metric] = 0;
			switch (ctx->meta->flags[metric] & TSDB_DOWNSAMPLE_MASK) {
				case TSDB_DOWNSAMPLE_MIN:
					next_values[metric] = INFINITY;
					break;
				case TSDB_DOWNSAMPLE_MAX:
					next_values[metric] = -INFINITY;
				default:
					next_values[metric] = 0.0;
			}
		}
		count = count / sizeof(tsdb_data_t) / ctx->meta->nmetrics;
		ptr = ctx->dec_buffer;
		while (count--) {
			for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++, ptr++) {
				if (isnan(*ptr)) {
					/* Skip unknown values */
					continue;
				}
				/* Perform decimation according to the option selected in the
				* flags for this metric */
				switch (ctx->meta->flags[metric] & TSDB_DOWNSAMPLE_MASK) {
					case TSDB_DOWNSAMPLE_MEAN:
					case TSDB_DOWNSAMPLE_SUM:
						next_values[metric] += *ptr;
						break;
					case TSDB_DOWNSAMPLE_MEDIAN:
						ERROR("FIXME: MEDIAN not implemented\n"); // FIXME:
						break;
					case TSDB_DOWNSAMPLE_MODE:
						ERROR("FIXME: MODE not implemented\n"); // FIXME:
						break;
					case TSDB_DOWNSAMPLE_MIN:
						if (*ptr < next_values[metric])
							next_values[metric] = *ptr;
						break;
					case TSDB_DOWNSAMPLE_MAX:
						if (*ptr > next_values[metric])
							next_values[metric] = *ptr;
						break;
					default:
						ERROR("Bad downsampling mode\n");
				}
				valid_count[metric]++;
			}
		}
		for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++) {
			if (valid_count[metric]) {
				/* Complete the decimation function */
				switch (ctx->meta->flags[metric] & TSDB_DOWNSAMPLE_MASK) {
					case TSDB_DOWNSAMPLE_MEAN:
						next_values[metric] /= (double)valid_count[metric];
						break;
					case TSDB_DOWNSAMPLE_MEDIAN:
					case TSDB_DOWNSAMPLE_MODE:
					case TSDB_DOWNSAMPLE_SUM:
					case TSDB_DOWNSAMPLE_MIN:
					case TSDB_DOWNSAMPLE_MAX:
						break;
					default:
						ERROR("Bad downsampling mode\n");
				}
			} else {
				/* Next value is unknown */
				next_values[metric] = NAN;
			}
			DEBUG("Metric %u found %u usable points (agg = %f)\n", metric, valid_count[metric], next_values[metric]);
		}
		
		/* Recurse down */
		point /= ctx->meta->decimation[layer];
		npoints /= ctx->meta->decimation[layer];
		layer++;
		tsdb_update_layer(ctx, layer, point, npoints, timestamp, next_values);
	}
	
	PROFILE_END("decimate");
fail:
	if (rc < 0) {
		ERROR("Update failed: %s\n", strerror(errno));
	}
	return rc;
}

int64_t tsdb_get_latest(tsdb_ctx_t *ctx)
{
	uint_fast32_t point;
	int64_t timestamp;
	
	FUNCTION_TRACE;
	
	/* Determine timestamp for last slot */
	if (ctx->meta->npoints == 0) {
		ERROR("Database is empty!\n");
		return TSDB_NO_TIMESTAMP;
	}
	point = ctx->meta->npoints - 1;
	timestamp = ctx->meta->start_time + (point * ctx->meta->interval);
	DEBUG("Latest values at point %" PRIuFAST32 " (%" PRIi64 " s)\n", point, timestamp);
	return timestamp;
}

int tsdb_update_values(tsdb_ctx_t *ctx, int64_t *timestamp, tsdb_data_t *values)
{
	uint_fast32_t point;
	int rc = 0;
	PROFILE_STORE;
	
	FUNCTION_TRACE;
	PROFILE_START;	

	/* FIXME: Locking! */
	
	/* For a new file this point represents the start of the database */
	*timestamp = (*timestamp / ctx->meta->interval) * ctx->meta->interval; /* round down */
	if (ctx->meta->npoints == 0) {
		ctx->meta->start_time = *timestamp;
	}
	
	/* Sanity checks */
	if (*timestamp < ctx->meta->start_time) {
		ERROR("Timestamp in the past\n");
		return -ENOENT;
	}	
	
	/* Determine position of point in the top-level */
	point = (*timestamp - ctx->meta->start_time) / ctx->meta->interval;
	
	/* Update layers */
	rc = tsdb_update_layer(ctx, 0, point, ctx->meta->npoints, *timestamp, values);
	if (rc >= 0) {
		/* Update metadata with new number of top-level points */		
		if (point >= ctx->meta->npoints)
			ctx->meta->npoints = point + 1;

		/* Flush metadata */
		msync(ctx->meta, sizeof(tsdb_metadata_t), MS_ASYNC);
	}
	
	PROFILE_END("update");
	return rc;
}

int tsdb_get_values(tsdb_ctx_t *ctx, int64_t *timestamp, tsdb_data_t *values)
{
	uint_fast32_t point;
	int rc = 0;
	PROFILE_STORE;
	
	FUNCTION_TRACE;
	PROFILE_START;
	
	/* Sanity check */
	if (*timestamp < ctx->meta->start_time) {
		ERROR("Timestamp in the past\n");
		return -ENOENT;
	}

	/* Determine position of point in the top-level */
	*timestamp = (*timestamp / ctx->meta->interval) * ctx->meta->interval; /* round down */
	point = (*timestamp - ctx->meta->start_time) / ctx->meta->interval;
	if (point >= ctx->meta->npoints) {
		ERROR("Timestamp in the future\n");
		return -ENOENT;
	}

	/* Read values */
	rc = lseek(ctx->table_fd[0], sizeof(tsdb_data_t) * point * ctx->meta->nmetrics, SEEK_SET);
	if (rc >= 0) {
		rc = read(ctx->table_fd[0], values, sizeof(tsdb_data_t) * ctx->meta->nmetrics);	
	}
	
	PROFILE_END("get_current");
	return rc;
}

int tsdb_get_series(tsdb_ctx_t *ctx, unsigned int metric_id, int64_t start, int64_t end, 
	unsigned int npoints, int flags, tsdb_series_point_t *points)
{
	uint_fast32_t layer_interval, out_interval;
	uint_fast32_t point;
	unsigned int layer;
	int outpoints;
	int rc = 0;
	PROFILE_STORE;
	
	FUNCTION_TRACE;
	PROFILE_START;
	
	/* Apply automatic limits where start/end not specified */
	if (start == TSDB_NO_TIMESTAMP) {
		start = ctx->meta->start_time;
	}
	if (end == TSDB_NO_TIMESTAMP) {
		end = start + (ctx->meta->npoints * ctx->meta->interval);
	}
	
	/* Sanity check */
	if (end < start) {
		ERROR("End time must be later than start\n");
		return -EINVAL;
	}
	if (metric_id >= ctx->meta->nmetrics) {
		ERROR("Requested metric is out of range\n");
		return -ENOENT;
	}
	
	/* Determine best layer to use for sourcing the result */
	out_interval = (end - start) / npoints;
	DEBUG("Requested %u points at interval %" PRIuFAST32 "\n", npoints, out_interval);
	layer_interval = ctx->meta->interval;
	for (layer = 0; layer < TSDB_MAX_LAYERS; layer++) {
		if (ctx->meta->decimation[layer] == 0) {
			/* This is the last layer - we have to use it */
			break;
		}
		if (layer_interval * ctx->meta->decimation[layer] > out_interval) {
			/* Next layer is downsampled too much, so use this one */
			break;
		}
		layer_interval *= ctx->meta->decimation[layer];
	}
	DEBUG("Using layer %u with interval %" PRIuFAST32 "\n", layer, layer_interval);
	
	/* Generate output points by rounding down the timestamp down to the nearest
	 * point in the input layer.  This is the fast, not so accurate method.
	 * FIXME: Add an interpolate/average method to combine multiple input points */
	for (outpoints = 0; npoints; npoints--, start += out_interval) {
		/* Determine if this point is in-range of the input table */
		if (start < ctx->meta->start_time || 
			start >= ctx->meta->start_time + ctx->meta->npoints * ctx->meta->interval) {
			/* No - there is no data at this time point */
			continue;
		}
		
		/* Point may be available in the table.  Naively round down the timestamp onto
		 * the table interval and return that point */
		point = (start - ctx->meta->start_time) / layer_interval;
		points->timestamp = start;
		rc = lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * (point * ctx->meta->nmetrics + metric_id), SEEK_SET);
		if (rc >= 0) {
			rc = read(ctx->table_fd[layer], &points->value, sizeof(tsdb_data_t));
		}
		if (rc < 0) {
			ERROR("Table read error for point %" PRIuFAST32 ": %s\n", point, strerror(errno));
			return rc;
		}
		
		/* Commit this point only if it contains data */
		if (!isnan(points->value)) {
			points++;
			outpoints++;
		}
	}
	
	PROFILE_END("get_series");
	return outpoints;
}

