/* ------------------------------------------------------------------------- *
 * Copyright (C) 2012 Jolla Ltd.
 * Contact: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * License: LGPLv2
 * ------------------------------------------------------------------------- */

#include "libwakelock.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

/** Whether to write debug logging to stderr
 *
 * If not enabled, no diagnostics of any kind gets written.
 */
#ifndef LWL_ENABLE_LOGGING
# define LWL_ENABLE_LOGGING 01
#endif

/** Prefix used for log messages
 */
#define LWL_LOG_PFIX "LWL: "

/** Logging functionality that can be configured out at compile time
 */
#if LWL_ENABLE_LOGGING
# define lwl_debugf(FMT, ARGS...) fprintf(stderr, LWL_LOG_PFIX FMT, ##ARGS)
#else
# define lwl_debugf(FMT, ARGS...) do { } while( 0 )
#endif

/** Paths to wakelock interfaces in sysfs
 */
static const char lwl_lock_path[]   = "/sys/power/wake_lock";
static const char lwl_unlock_path[] = "/sys/power/wake_unlock";

/** Helper for writing to sysfs files
 */
static void lwl_write_file(const char *path, const char *data)
{
	int file;

	lwl_debugf("%s << %s", path, data);

	if( (file = TEMP_FAILURE_RETRY(open(path, O_WRONLY))) == -1 ) {
		lwl_debugf("%s: open: %m\n", path);
	} else {
		int size = strlen(data);
		errno = 0;
		if( TEMP_FAILURE_RETRY(write(file, data, size)) != size ) {
			lwl_debugf("%s: write: %m\n", path);
		}
		TEMP_FAILURE_RETRY(close(file));
	}
}

/** Helper for checking if wakelock interface is supported
 */
static int lwl_enabled(void)
{
	static int checked = 0, enabled = 0;

	if( !checked ) {
		checked = 1, enabled = !access(lwl_lock_path, F_OK);
		lwl_debugf("%s\n", enabled ? "enabled" : "disabled");
	}

	return enabled;
}

/** Use sysfs interface to create and enable a wakelock.
 *
 * @param name The name of the wakelock to obtain
 * @param ns   Time in nanoseconds before the wakelock gets released
 *             automatically, or negative value for no timeout.
 */
void wakelock_lock(const char *name, long long ns)
{
	if( lwl_enabled() ) {
		char tmp[64];
		if( ns < 0 ) {
			snprintf(tmp, sizeof tmp, "%s\n", name);
		} else {
			snprintf(tmp, sizeof tmp, "%s %lld\n", name, ns);
		}
		lwl_write_file(lwl_lock_path, tmp);
	}
}

/** Use sysfs interface to disable a wakelock.
 *
 * @param name The name of the wakelock to release
 *
 * Note: This will not delete the wakelocks.
 */
void wakelock_unlock(const char *name)
{
	if( lwl_enabled() ) {
		char tmp[64];
		snprintf(tmp, sizeof tmp, "%s\n", name);
		lwl_write_file(lwl_unlock_path, tmp);
	}
}