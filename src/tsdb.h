/*
 * File-based time series database
 *
 * Copyright (C) 2012, 2013 Mike Stirling
 *
 * This file is part of TimeStore (http://www.livesense.co.uk/timestore)
 *
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TSDB_H
#define TSDB_H

#include <stdint.h>
#include <inttypes.h>

#define TSDB_PTHREAD_LOCKING

#ifdef TSDB_PTHREAD_LOCKING
#include <pthread.h>
#endif

#define TSDB_MAGIC_META		0x42445354 // TSDB (little-endian)

#define TSDB_VERSION		0

/* Flags to specify what to do when padding unavailable data points */
typedef enum {
	tsdbPad_Unknown = 0,
	tsdbPad_Last,
	tsdbPad_Reserved2,
} tsdb_pad_mode_t;
#define TSDB_PAD_SHIFT		0
#define TSDB_PAD_MASK		7

/* Flags to specify how to combine data points for downsampling */
typedef enum {
	tsdbDownsample_Mean = 0,
	tsdbDownsample_Median,
	tsdbDownsample_Mode,
	tsdbDownsample_Sum,
	tsdbDownsample_Min,
	tsdbDownsample_Max,
} tsdb_downsample_mode_t;
#define TSDB_DOWNSAMPLE_SHIFT	8
#define TSDB_DOWNSAMPLE_MASK	15

/* Key flags */
#define TSDB_KEY_IN_USE			(1 << 0)

/* Length of HMAC keys (bytes).  Only database key management is handled here as
 * part of the metadata.  API layers are expected to handle the actual access control
 * process in a way that is appropriate for the particular API */
#define TSDB_KEY_LENGTH		32

/* Typedef for key data */
typedef uint8_t tsdb_key_t[TSDB_KEY_LENGTH];

/* Typedef for a key slot */
typedef struct {
	uint32_t flags;
	tsdb_key_t key;
} tsdb_key_info_t;

/* Keys */
typedef enum {
	tsdbKey_Read = 0,
	tsdbKey_Write,
	tsdbKey_Max
} tsdb_key_id_t;

/* Size of key store */
#define TSDB_MAX_KEYS		((int)tsdbKey_Max)

/* Format for metadata filename ((uint64_t)node id) */
#define TSDB_METADATA_FORMAT	"%016" PRIX64 ".tsdb"
/* Format for table data filename ((uint64_t)node id, (unsigned int)layer) */
#define TSDB_TABLE_FORMAT	"%016" PRIX64 "_%u_.dat"

/* Max size for generated paths */
#define TSDB_MAX_PATH		256

/* Maximum number of metrics per data set */
#define TSDB_MAX_METRICS	32
/* Maximum number of layers per data set */
#define TSDB_MAX_LAYERS		8

/* Maximum size of padding buffer */
#define TSDB_MAX_PADDING_BLOCK	(1024 * 1024)

/* Special value for passing a "don't care" timestamp by value */
#define TSDB_NO_TIMESTAMP	INT64_MAX

/* NOTE: A 32-bit value for npoints is considered sufficient since this would
 * allow for over 135 years worth of 1 second data! */

/* Data set metadata */
typedef struct {
	uint32_t	magic;				/*< Magic number - indicates TSDB metadata file */
	uint32_t	version;			/*< Version identifier */
	uint64_t	node_id;			/*< Node ID (should match filename) */
	uint32_t	nmetrics;			/*< Number of metrics in this data set */
	uint32_t	npoints;			/*< Number of points we expect to find in the top-level table */
	int64_t		start_time;			/*< Timestamp of first entry in table (relative to epoch) */
	uint32_t	interval;			/*< Interval in seconds between entries at the top-level */
	uint32_t	decimation[TSDB_MAX_LAYERS];	/*< Number of points to combine when downsampling to each lower layer */
	uint32_t	flags[TSDB_MAX_METRICS];	/*< Flags (for each metric) */
	tsdb_key_info_t	key[TSDB_MAX_KEYS];	/*< MAC keystore */
} tsdb_metadata_t;

/* Type for data points */
#ifdef TSDB_FLOAT_TYPE
typedef float tsdb_data_t;
#else
typedef double tsdb_data_t;
#endif

typedef struct {
	int 		meta_fd;			/*< File descriptor for metadata */
	int 		table_fd[TSDB_MAX_LAYERS];	/*< File descriptors for each data layer */
	tsdb_metadata_t	*meta;				/*< Pointer to mmapped metadata */
	tsdb_data_t	*padding;			/*< Pre-allocated padding buffer */
	tsdb_data_t	*work_buffer;			/*< Pre-allocated work buffer */
	
#ifdef TSDB_PTHREAD_LOCKING
	pthread_mutex_t	mutex;				/*< Mutex for locking in multi-threaded applications */
#endif
} tsdb_ctx_t;

/* Name/value pairs for returning series */
typedef struct {
	int64_t		timestamp;
	tsdb_data_t	value;
} tsdb_series_point_t;

/*!
 * \brief		Creates a new time series database
 * \param node_id	Node to create
 * \param interval	Time interval (in seconds) between samples at the highest resolution
 * \param nmetrics	Number of metrics to allocate for this node
 * \param pad_mode	Array per metric \see tsdb_pad_mode_t
 * \param ds_mode	Array per metric \see tsdb_downsample_mode_t
 * \param decimation	Pointer to an array containing number of points to combine for each lower layer
 * \return		0 or negative error code
 */
int tsdb_create(uint64_t node_id, unsigned int interval, unsigned int nmetrics,
	tsdb_pad_mode_t *pad_mode, tsdb_downsample_mode_t *ds_mode, unsigned int *decimation);

/*!
 * \brief		Deletes an existing time series database
 * \param node_id	Node to delete
 * \return		0 or negative error code
 */
int tsdb_delete(uint64_t node_id);

/*!
 * \brief 		Opens an existing time series database
 * \param node_id	Node to open
 * \return		Pointer to context structure or null on error
 */
tsdb_ctx_t* tsdb_open(uint64_t node_id);

/*!
 * \brief		Closes a time series database opened by a call to tsdb_open
 * \param ctx		Pointer to context structure returned by tsdb_open
 */
void tsdb_close(tsdb_ctx_t *ctx);

/*!
 * \brief		Returns the UNIX timestamp of the latest time point
 * \param ctx		Pointer to context structure returned by tsdb_open
 * \return		UNIX timestamp of the most recent time point or TSDB_NO_TIMESTAMP if no data
 */
int64_t tsdb_get_latest(tsdb_ctx_t *ctx);

/*!
 * \brief		Update a time point with values for one or more of its metrics
 * 
 * Time points are designed to be updated with values for all assigned metrics simultaneously,
 * as this is the most efficient means of updating the interleaved data store.  However, it is
 * possible to omit metrics by passing the value NaN.  In this case the update function will leave
 * any pre-existing value in place.
 * 
 * Downsampling to lower resolution layers is handled automatically.
 * 
 * \param ctx		Pointer to context structure returned by tsdb_open
 * \param timestamp	Pointer to variable with the UNIX timestamp of the time point being
 * 			updated.  The variable may be updated with a value rounded down to
 * 			the nearest interval.
 * \param values	Pointer to an array of values in metric order.  It must be
 * 			large enough for the number of metrics in the data set
 * 			(ctx->meta->nmetrics).
 * \return		0 on success or a negative error code
 */
int tsdb_update_values(tsdb_ctx_t *ctx, int64_t *timestamp, tsdb_data_t *values);

/*!
 * \brief		Returns the latest values for all metrics in the data set
 * 
 * \param ctx		Pointer to context structure returned by tsdb_open
 * \param timestamp	Pointer to variable with the UNIX timestamp of the required time point.
 * 			May be modified with a value rounded down to the nearest interval.
 * \param values	Pointer to an array to be updated with the time point's values.  It
 * 			must be large enough for the number of metrics in the data set
 * 			(ctx->meta->nmetrics).
 * \return		0 on success or a negative error code
 */
int tsdb_get_values(tsdb_ctx_t *ctx, int64_t *timestamp, tsdb_data_t *values);

/*!
 * \brief		Returns an array of values for a single data series (one metric from one data set)
 *
 * The function will return the requested number of data points covering the specified
 * time range.
 *
 * \param ctx		Pointer to context structure returned by tsdb_open
 * \param metric_id	ID of metric to return
 * \param start		UNIX timestamp for the start of the period of interest (or TSDB_NO_TIMESTAMP)
 * \param end		UNIX timestamp for the end of the period of interest (or TSDB_NO_TIMESTAMP)
 * \param npoints	Number of data points to output
 * \param flags		Flags (reserved)
 * \param values	Pointer to an array to be updated with the result set.  It
 * 			must be large enough to hold npoints.
 * \return		Number of points returned on success or a negative error code
 */
int tsdb_get_series(tsdb_ctx_t *ctx, unsigned int metric_id, int64_t start, int64_t end, 
	unsigned int npoints, int flags, tsdb_series_point_t *points);

/*!
 * \brief			Returns a key from the database metadata
 *
 * \param ctx		Pointer to context structure returned by tsdb_open
 * \param key_id	ID of key to return
 * \param key		Pointer to key to be populated
 *
 * \return			0 on success, -EINVAL on bad key ID, -ENOENT if no key defined
 */
int tsdb_get_key(tsdb_ctx_t *ctx, tsdb_key_id_t key_id, tsdb_key_t *key);

/*!
 * \brief			Writes a key to the database metadata
 *
 * \param ctx		Pointer to context structure returned by tsdb_open
 * \param key_id	ID of key to set
 * \param key		Pointer to key data to be written to database or NULL to remove
 *
 * \return			0 on success, -EINVAL on bad key ID
 */
int tsdb_set_key(tsdb_ctx_t *ctx, tsdb_key_id_t key_id, tsdb_key_t *key);

#endif
