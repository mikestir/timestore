/* TODO: Review use of xint_fast32_t */

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
#include "profile.h"

int tsdb_create(uint64_t node_id, unsigned int interval, unsigned int nmetrics, 
	tsdb_pad_mode_t *pad_mode, tsdb_downsample_mode_t *ds_mode, unsigned int *decimation)
{
	tsdb_metadata_t md;
	char path[TSDB_MAX_PATH];
	int n, fd;
	
	FUNCTION_TRACE;
	
	/* Create metadata only if it does not already exist */
	snprintf(path, TSDB_MAX_PATH, TSDB_METADATA_FORMAT, node_id);
	DEBUG("Node %016" PRIX64 " metadata path: %s\n", node_id, path);
	fd = open(path, O_RDWR | O_EXCL | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		ERROR("Error creating metadata %s: %s\n", path, strerror(errno));
		return -errno;
	}
	
	/* Populate fresh metadata */
	INFO("Empty metadata - populating new dataset\n");
	memset(&md, 0, sizeof(md));
	md.magic = TSDB_MAGIC_META;
	md.version = TSDB_VERSION;
	md.node_id = node_id;
	md.nmetrics = (uint32_t)nmetrics;
	md.npoints = 0;
	md.start_time = 0;
	md.interval = (uint32_t)interval;
	for (n = 0; n < nmetrics; n++) {
		md.flags[n] = (
			((uint32_t)pad_mode[n] << TSDB_PAD_SHIFT) |
			((uint32_t)ds_mode[n] << TSDB_DOWNSAMPLE_SHIFT));
	}
	for (n = 0; n < TSDB_MAX_LAYERS; n++) {
		if (*decimation == 0)
			break;
		md.decimation[n] = (uint32_t)*decimation++;
	}
	write(fd, &md, sizeof(tsdb_metadata_t));
	close(fd);
	
	return 0;
}

int tsdb_delete(uint64_t node_id)
{
	unsigned int layer;
	char path[TSDB_MAX_PATH];
	
	FUNCTION_TRACE;
	
	/* Delete metadata file */
	snprintf(path, TSDB_MAX_PATH, TSDB_METADATA_FORMAT, node_id);
	DEBUG("Node %016" PRIX64 " metadata path: %s\n", node_id, path);
	if (unlink(path) < 0) {
		ERROR("Failed to unlink %s\n", path);
		return -errno;
	}
	
	/* Delete layer data - stop on first failure */
	for (layer = 0; layer < TSDB_MAX_LAYERS; layer++) {		
		snprintf(path, TSDB_MAX_PATH, TSDB_TABLE_FORMAT, node_id, layer);
		DEBUG("Node %016" PRIX64 " layer %u table path: %s\n", node_id, layer, path);	
		if (unlink(path) < 0)
			break;
	}
	return 0;
}

tsdb_ctx_t* tsdb_open(uint64_t node_id)
{
	tsdb_ctx_t *ctx;
	char path[TSDB_MAX_PATH];
	struct stat st;
	unsigned int n, layer;
	uint_fast32_t max_decimation = 0;
	
	FUNCTION_TRACE;
	
	ctx = (tsdb_ctx_t*)calloc(1, sizeof(tsdb_ctx_t));
	if (ctx == NULL) {
		CRITICAL("Out of memory\n");
		return NULL;
	}
	
	/* Open and map dataset metadata */
	snprintf(path, TSDB_MAX_PATH, TSDB_METADATA_FORMAT, node_id);
	DEBUG("Node %016" PRIX64 " metadata path: %s\n", node_id, path);
	ctx->meta_fd = open(path, O_RDWR);
	if (ctx->meta_fd < 0) {
		ERROR("Error opening metadata %s: %s\n", path, strerror(errno));
		goto fail;
	}
	fstat(ctx->meta_fd, &st);
	if (st.st_size && st.st_size != sizeof(tsdb_metadata_t)) {
		ERROR("Corrupt metadata\n");
		goto fail;
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
		snprintf(path, TSDB_MAX_PATH, TSDB_TABLE_FORMAT, node_id, layer);
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
		ctx->work_buffer = malloc(sizeof(tsdb_data_t) * ctx->meta->nmetrics * max_decimation);
		if (ctx->work_buffer == NULL) {
			CRITICAL("Out of memory\n");
			goto fail;
		}
	}
	
	return ctx;
fail:
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
	if (ctx->work_buffer != NULL) {
		free(ctx->work_buffer);
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

/* FIXME: Timestamp is passed in to allow for integrity checking the lower layers. Not yet implemented */
static int tsdb_update_layer(tsdb_ctx_t *ctx, unsigned int layer, uint_fast32_t point, uint_fast32_t npoints,
	int64_t timestamp, tsdb_data_t *values)
{
	unsigned int metric;
	tsdb_data_t new_values[TSDB_MAX_METRICS];
	tsdb_data_t *ptr;
	
	FUNCTION_TRACE;
	
	DEBUG("Values for %u metrics at %" PRIi64 " at point %" PRIuFAST32 " in layer %d\n", ctx->meta->nmetrics,
	      timestamp, point, layer);
	
	/* Pad missing values */
	if (point > npoints) {
		uint_fast32_t npadding = point - npoints;
		unsigned int pointsperblock = TSDB_MAX_PADDING_BLOCK / (sizeof(tsdb_data_t) * ctx->meta->nmetrics);
		unsigned int n;

		DEBUG("Padding %" PRIuFAST32 " points\n", npadding);
		
		/* Fill padding block buffer */
		if (npadding < pointsperblock)
			pointsperblock = npadding;
		ptr = ctx->padding;
		for (n = 0; n < pointsperblock; n++) {
			for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++, ptr++) {
				switch ((tsdb_pad_mode_t)((ctx->meta->flags[metric] >> TSDB_PAD_SHIFT) & TSDB_PAD_MASK)) {
					case tsdbPad_Unknown:
						*ptr = NAN;
						break;
 					case tsdbPad_Last:
						DEBUG("FIXME: tsdbPad_Last not implemented\n"); 
						*ptr = NAN; // FIXME:
						break;
					default:
						ERROR("Bad padding mode\n");
				}
			}
		}
		
		/* Write blocks to table file */
		if (lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * npoints * ctx->meta->nmetrics, SEEK_SET) < 0) {
			ERROR("Padding seek error\n");
			return -errno;
		}
		do {
			if (npadding < pointsperblock)
				pointsperblock = npadding;
			DEBUG("%u points of %" PRIuFAST32 "\n", pointsperblock, npadding);
			if (write(ctx->table_fd[layer], ctx->padding, 
				sizeof(tsdb_data_t) * pointsperblock * ctx->meta->nmetrics) < 0) {
				ERROR("Padding write error\n");
				return -errno;
			}
			npadding -= pointsperblock;
		} while (npadding);
		
	}
	
	/* Default unknown points to NAN */
	for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++) {
		new_values[metric] = NAN;
	}
	
	if (point < ctx->meta->npoints) {
		/* Updating existing point - read current values */
		if (lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * point * ctx->meta->nmetrics, SEEK_SET) < 0) {
			ERROR("Table seek error reading values for point %" PRIuFAST32 "\n", point);
			return -errno;
		}
		if (read(ctx->table_fd[layer], new_values, sizeof(tsdb_data_t) * ctx->meta->nmetrics) < 0) {
			ERROR("Table read error reading values for point %" PRIuFAST32 "\n", point);
			return -errno;
		}
	}
	
	/* Fill in any non-NAN new values */
	ptr = values;
	for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++, ptr++) {
		if (!isnan(*ptr)) {
			new_values[metric] = *ptr;
		}
	}
	
	/* Write point back to file */
	if (lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * point * ctx->meta->nmetrics, SEEK_SET) < 0) {
		ERROR("Table seek error writing values for point %" PRIuFAST32 "\n", point);
		return -errno;
	}
	if (write(ctx->table_fd[layer], new_values, sizeof(tsdb_data_t) * ctx->meta->nmetrics) < 0) {
		ERROR("Table write error writing values for point %" PRIuFAST32 "\n", point);
		return -errno;
	}
	
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
		if (lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * first_point * ctx->meta->nmetrics, SEEK_SET) < 0) {
			ERROR("Table seek error while decimating\n");
			return -errno;
		}
		count = read(ctx->table_fd[layer], ctx->work_buffer, sizeof(tsdb_data_t) * ctx->meta->decimation[layer] * ctx->meta->nmetrics);
		if (count < 0) {
			ERROR("Table read error while decimating\n");
			return -errno;
		}
		
		/* Calculate decimated values */
		for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++) {
			valid_count[metric] = 0;
			switch ((tsdb_downsample_mode_t)((ctx->meta->flags[metric] >> TSDB_DOWNSAMPLE_SHIFT) & TSDB_DOWNSAMPLE_MASK)) {
				case tsdbDownsample_Min:
					next_values[metric] = INFINITY;
					break;
				case tsdbDownsample_Max:
					next_values[metric] = -INFINITY;
				default:
					next_values[metric] = 0.0;
			}
		}
		count = count / sizeof(tsdb_data_t) / ctx->meta->nmetrics;
		ptr = ctx->work_buffer;
		while (count--) {
			for (metric = 0; metric < (unsigned int)ctx->meta->nmetrics; metric++, ptr++) {
				if (isnan(*ptr)) {
					/* Skip unknown values */
					continue;
				}
				/* Perform decimation according to the option selected in the
				* flags for this metric */
				switch ((tsdb_downsample_mode_t)((ctx->meta->flags[metric] >> TSDB_DOWNSAMPLE_SHIFT) & TSDB_DOWNSAMPLE_MASK)) {
					case tsdbDownsample_Mean:
					case tsdbDownsample_Sum:
						next_values[metric] += *ptr;
						break;
					case tsdbDownsample_Median:
						ERROR("FIXME: MEDIAN not implemented\n"); // FIXME:
						break;
					case tsdbDownsample_Mode:
						ERROR("FIXME: MODE not implemented\n"); // FIXME:
						break;
					case tsdbDownsample_Min:
						if (*ptr < next_values[metric])
							next_values[metric] = *ptr;
						break;
					case tsdbDownsample_Max:
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
				switch ((tsdb_downsample_mode_t)((ctx->meta->flags[metric] >> TSDB_DOWNSAMPLE_SHIFT) & TSDB_DOWNSAMPLE_MASK)) {
					case tsdbDownsample_Mean:
						next_values[metric] /= (double)valid_count[metric];
						break;
					case tsdbDownsample_Median:
					case tsdbDownsample_Mode:
					case tsdbDownsample_Sum:
					case tsdbDownsample_Min:
					case tsdbDownsample_Max:
						break;
					default:
						ERROR("Bad downsampling mode\n");
				}
			} else {
				/* Next value is unknown */
				next_values[metric] = NAN;
			}
			DEBUG("Metric %u found %u usable points (agg = %f)\n", metric, valid_count[metric],
				next_values[metric]);
		}
		
		/* Recurse down */
		tsdb_update_layer(ctx, layer + 1, 
			point / ctx->meta->decimation[layer],
			/* npoints needs to be rounded up */
			(npoints + ctx->meta->decimation[layer] - 1) / ctx->meta->decimation[layer], 
			timestamp, next_values);
	}
	
	return 0;
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
	
	FUNCTION_TRACE;

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
	if (rc == 0) {
		/* Update metadata with new number of top-level points */		
		if (point >= ctx->meta->npoints)
			ctx->meta->npoints = point + 1;

		/* Flush metadata */
		msync(ctx->meta, sizeof(tsdb_metadata_t), MS_ASYNC);
	}
	
	return rc;
}

int tsdb_get_values(tsdb_ctx_t *ctx, int64_t *timestamp, tsdb_data_t *values)
{
	uint_fast32_t point;
	
	FUNCTION_TRACE;
	
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
	if (lseek(ctx->table_fd[0], sizeof(tsdb_data_t) * point * ctx->meta->nmetrics, SEEK_SET) < 0) {
		ERROR("Table seek error for point %" PRIuFAST32 ": %s\n", point, strerror(errno));		
		return -errno;
	}
	if (read(ctx->table_fd[0], values, sizeof(tsdb_data_t) * ctx->meta->nmetrics) < 0) {
		ERROR("Table read error for point %" PRIuFAST32 ": %s\n", point, strerror(errno));		
		return -errno;
	}

	return 0;
}

/* TODO: There is room for improvement here.  Where the desired timepoint lies between samples
 * it would be nice to attempt some interpolation */
int tsdb_get_series(tsdb_ctx_t *ctx, unsigned int metric_id, int64_t start, int64_t end, 
	unsigned int npoints, int flags, tsdb_series_point_t *points)
{
	uint_fast32_t layer_interval, out_interval;
	uint_fast32_t point;
	tsdb_data_t *layer_values, *ptr;
	unsigned int layer;
	unsigned int n, naverage, actual_naverage, actual_npoints;
	
	FUNCTION_TRACE;
	
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
	DEBUG("Requested %u points on interval %" PRIuFAST32 "\n", npoints, out_interval);
	if ((end - start) < npoints) {
		/* Minimum interval for output points is 1 second */
		npoints = end - start;
		out_interval = 1;
		INFO("Reduced requested points to %u for minimum 1 second interval\n", npoints);
	}
	{
		struct tm *tmp;
		char timestr[100];
		time_t t = (time_t)start;
		tmp = localtime(&t);
		strftime(timestr, sizeof(timestr), "%F %T", tmp);
		DEBUG("Start time is: %s\n", timestr);
	}
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
	naverage = (out_interval > layer_interval) ? out_interval / layer_interval : 1;
	DEBUG("Using layer %u with interval %" PRIuFAST32 " decimation ratio = %u\n", layer, layer_interval, 
		naverage);
	
	/* Allocate storage for values loaded from input layer */
	layer_values = (tsdb_data_t*)malloc(sizeof(tsdb_data_t) * ctx->meta->nmetrics * naverage);
	if (layer_values == NULL) {
		CRITICAL("Out of memory\n");
		return -ENOMEM;
	}
	
	/* Generate output points by averaging all available input points between the start
	 * and end times for each output step.  Output timestamps are rounded down onto the
	 * input interval - there is no interpolation. */
	for (actual_npoints = 0; npoints; npoints--, start += out_interval) {
		/* Determine if this point is in-range of the input table */
		if (start < ctx->meta->start_time || 
			start >= ctx->meta->start_time + ctx->meta->npoints * ctx->meta->interval) {
			/* No - there is no data at this time point */
			continue;
		}
		
		/* There may be data for this point in the table.  Calculate the range of input points
		 * covered by the output period and read them for averaging */
		point = (start - ctx->meta->start_time) / layer_interval;
		if (lseek(ctx->table_fd[layer], sizeof(tsdb_data_t) * point * ctx->meta->nmetrics, SEEK_SET) < 0) {
			ERROR("Table seek error for point %" PRIuFAST32 ": %s\n", point, strerror(errno));
			return -errno;
		}
		if (read(ctx->table_fd[layer], layer_values, sizeof(tsdb_data_t) * naverage * ctx->meta->nmetrics) < 0) {
			ERROR("Table read error for point %" PRIuFAST32 ": %s\n", point, strerror(errno));
			return -errno;
		}
		
		/* Generate average ignoring any NAN points */
		points->timestamp = start;
		points->value = 0.0;
		ptr = layer_values + metric_id;
		actual_naverage = 0;
		for (n = naverage; n; n--, ptr += ctx->meta->nmetrics) {
			if (!isnan(*ptr)) {
				points->value += *ptr;
				actual_naverage++;
			}
		}
		DEBUG("averaged %u points\n", actual_naverage);
		if (actual_naverage) {
			/* A valid point was generated */
			points->value /= (double)actual_naverage;
			points++;
			actual_npoints++;
		}
	}
	
	free(layer_values);
	DEBUG("generated %u points\n", actual_npoints);
	
	return actual_npoints;
}

