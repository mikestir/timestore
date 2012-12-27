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
#define TSDB_PAD_UNKNOWN	(0 << 0)
#define TSDB_PAD_LAST		(1 << 0)
#define TSDB_PAD_MASK		(3 << 0)

/* Flags to specify how to combine data points for decimation */
#define TSDB_DOWNSAMPLE_MEAN	(0 << 8)
#define TSDB_DOWNSAMPLE_MEDIAN	(1 << 8)
#define TSDB_DOWNSAMPLE_MODE	(2 << 8)
#define TSDB_DOWNSAMPLE_SUM	(3 << 8)
#define TSDB_DOWNSAMPLE_MIN	(4 << 8)
#define TSDB_DOWNSAMPLE_MAX	(5 << 8)
#define TSDB_DOWNSAMPLE_MASK	(15 << 8)

/* Format for metadata filename ((uint64_t)node id) */
#define TSDB_METADATA_FORMAT	"%016" PRIX64 ".tsdb"
/* Format for table data filename ((uint64_t)node id, (unsigned int)layer) */
#define TSDB_TABLE_FORMAT	"%016" PRIX64 "_%u_.dat"

/* Max size for generated paths */
#define TSDB_PATH_MAX		256

/* Maximum number of metrics per data set */
#define TSDB_MAX_METRICS	32
/* Maximum number of layers per data set */
#define TSDB_MAX_LAYERS		8

/* Maximum size of padding buffer */
#define TSDB_MAX_PADDING_BLOCK	(1024 * 1024)

/* Open flags */
#define TSDB_CREATE		(1 << 0)

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
	uint32_t	npoints;			/*< Number of points we expect to find in the undecimated table */
	int64_t		start_time;			/*< Timestamp of first entry in table (relative to epoch) */
	uint32_t	interval;			/*< Interval in seconds between entries in the undecimated table */
	uint32_t	decimation[TSDB_MAX_LAYERS];	/*< Number of points to combine when generating the lower layer */
	uint32_t	flags[TSDB_MAX_METRICS];	/*< Flags (for each metric) */
} tsdb_metadata_t;

/* Type for data points */
typedef double tsdb_data_t;

typedef struct {
	int 		meta_fd;			/*< File descriptor for metadata */
	int 		table_fd[TSDB_MAX_LAYERS];	/*< File descriptors for each data layer */
	tsdb_metadata_t	*meta;				/*< Pointer to mmapped metadata */
	tsdb_data_t	*padding;			/*< Pre-allocated padding buffer */
	tsdb_data_t	*dec_buffer;			/*< Pre-allocated work buffer */
	
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
 * \brief 		Opens an existing time series database or create a new one
 * \param node_id	Node to open
 * \param flags		Flags (TSDB_CREATE)
 * \param nmetrics	Ignored unless TSDB_CREATE flag specified.  Number of metrics to allocate.
 * \return		Pointer to context structure or null on error
 */
tsdb_ctx_t* tsdb_open(uint64_t node_id, unsigned int nmetrics, int flags);

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
 * \param start		UNIX timestamp for the start of the period of interest
 * \param end		UNIX timestamp for the end of the period of interest
 * \param npoints	Number of data points to output
 * \param flags		Flags (reserved)
 * \param values	Pointer to an array to be updated with the result set.  It
 * 			must be large enough to hold npoints.
 * \return		Number of points returned on success or a negative error code
 */
int tsdb_get_series(tsdb_ctx_t *ctx, unsigned int metric_id, int64_t start, int64_t end, 
	unsigned int npoints, int flags, tsdb_series_point_t *points);

#endif
