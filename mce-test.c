#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include "mce-log.h"
#include "mce-dbus.h"

static GMainLoop *mainloop;

#define SENSORFW_SERVICE "com.nokia.SensorService"
#define SENSORFW_PATH "/SensorManager"

static gint32 als_sessionid;
static gint32 prox_sessionid;

gboolean load_sensor(const char *id)
{
	DBusMessage *msg;
	DBusError error;
	gboolean success = FALSE;

	dbus_error_init(&error);
	mce_log(LL_DEBUG, "Requesting plugin for %s", id);
	msg = dbus_send_with_block(SENSORFW_SERVICE, SENSORFW_PATH,
		"local.SensorManager", "loadPlugin",
		DBUS_TIMEOUT_USE_DEFAULT,
		DBUS_TYPE_STRING, &id,
		DBUS_TYPE_INVALID);
	if (!msg) {
		mce_log(LL_ERR, "could not request plugin for %s", id);
		return FALSE;
	}
	if (!dbus_message_get_args(msg, &error,
		DBUS_TYPE_BOOLEAN, &success,
		DBUS_TYPE_INVALID)) {
		mce_log(LL_ERR, "could not parse reply");
		return FALSE;
	}
	if (!success) {
		mce_log(LL_WARN, "request to load plugin for %s denied", id);
	}
	return success;
}

gboolean request_sensor(const char *id, gint32 *sessionid)
{
	DBusMessage *msg;
	DBusError error;
	gint64 pid = getpid();

	dbus_error_init(&error);
	mce_log(LL_DEBUG, "Requesting sensor %s", id);
	msg = dbus_send_with_block(SENSORFW_SERVICE, SENSORFW_PATH,
		"local.SensorManager", "requestSensor",
		DBUS_TIMEOUT_USE_DEFAULT,
		DBUS_TYPE_STRING, &id, // DBUS_TYPE_STRING takes a char **
		DBUS_TYPE_INT64, &pid,
		DBUS_TYPE_INVALID);
	if (!msg) {
		mce_log(LL_ERR, "could not request session id for %s", id);
		return FALSE;
	}
	// NOTE: sessionid is an 'int' so we should use DBUS_TYPE_INT64
	// on a 64-bit platform.
	if (!dbus_message_get_args(msg, &error, 
		DBUS_TYPE_INT32, sessionid,
		DBUS_TYPE_INVALID)) {
		mce_log(LL_ERR, "could not parse reply");
		return FALSE;
	}
	if (*sessionid == -1) {
		mce_log(LL_ERR, "could not open session for %s", id);
		return FALSE;
	}
	mce_log(LL_DEBUG, "Got session id %d for %s", *sessionid, id);
	return TRUE;
}

gboolean release_sensor(const char *id, gint32 sessionid)
{
	DBusMessage *msg;
	DBusError error;
	gint64 pid = getpid();
	gboolean success = FALSE;

	dbus_error_init(&error);
	mce_log(LL_DEBUG, "Releasing %s (session %d)", id, sessionid);
	msg = dbus_send_with_block(SENSORFW_SERVICE, SENSORFW_PATH,
		"local.SensorManager", "releaseSensor",
		DBUS_TIMEOUT_USE_DEFAULT,
		DBUS_TYPE_STRING, &id,
		DBUS_TYPE_INT32, &sessionid,
		DBUS_TYPE_INT64, &pid,
		DBUS_TYPE_INVALID);
	if (!msg) {
		mce_log(LL_ERR, "request to release %s (session %d) failed", id, sessionid);
		return FALSE;
	}
	if (!dbus_message_get_args(msg, &error, 
		DBUS_TYPE_BOOLEAN, &success,
		DBUS_TYPE_INVALID)) {
		mce_log(LL_ERR, "could not parse reply");
		return FALSE;
	}
	if (!success) {
		mce_log(LL_WARN, "could not release %s (session %d)", id, sessionid);
	}
	return success;
}

void start_sensor(const char *id, const char *name, gint32 sessionid)
{
	DBusMessage *msg;
	DBusError error;
	char *path = malloc(strlen(SENSORFW_PATH) + 1 + strlen(id) + 1);

	dbus_error_init(&error);
	sprintf(path, "%s/%s", SENSORFW_PATH, id);
	mce_log(LL_DEBUG, "Starting sensor session %d", sessionid);
	msg = dbus_send_with_block(SENSORFW_SERVICE, path,
		name, "start", DBUS_TIMEOUT_USE_DEFAULT,
		DBUS_TYPE_INT32, &sessionid,
		DBUS_TYPE_INVALID);
	if (!msg)
		mce_log(LL_ERR, "request to start sensor session %d failed", sessionid);
	free(path);
}

void enable_sensors(void)
{
	if (load_sensor("alssensor")
		&& request_sensor("alssensor", &als_sessionid)) {
		start_sensor("alssensor", "local.ALSSensor", als_sessionid);
	}
	if (load_sensor("proximitysensor")
		&& request_sensor("proximitysensor", &prox_sessionid)) {
		start_sensor("proximitysensor", "local.ProximitySensor", prox_sessionid);
	}
}

void disable_sensors(void)
{
	if (als_sessionid >= 0)
		release_sensor("alssensor", als_sessionid);
	if (prox_sessionid >= 0)
		release_sensor("proximitysensor", prox_sessionid);
}

void mce_abort(void)
{
	abort();
}

void mce_quit_mainloop(void)
{
        if (!mainloop)
                exit(1);
        g_main_loop_quit(mainloop);
}

static void signal_handler(const gint signr)
{
	mce_quit_mainloop();
}

int main(void)
{
	/* boilerplate */
	mce_log_open("sensorfwtest", LOG_USER, MCE_LOG_STDERR);
	mce_log_set_verbosity(LL_DEBUG);
        g_type_init();
	mainloop = g_main_loop_new(NULL, FALSE);

        if (mce_dbus_init(TRUE) == FALSE) {
		mce_log(LL_CRIT, "Failed to initialise D-Bus");
		exit(1);
	}

	enable_sensors();

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	g_main_loop_run(mainloop);

	disable_sensors();

	mce_dbus_exit();
	g_main_loop_unref(mainloop);
	mainloop = 0;

        mce_log(LL_INFO, "Exiting...");
	return 0;
}
