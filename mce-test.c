#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include "mce-log.h"
#include "mce-dbus.h"

static GMainLoop *mainloop;

void enable_sensors()
{
	// stub
}

void disable_sensors()
{
	// stub
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

int main(int argc, char **argv)
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

	g_main_loop_run(mainloop);

	disable_sensors();

	mce_dbus_exit();
	g_main_loop_unref(mainloop);
	mainloop = 0;

        mce_log(LL_INFO, "Exiting...");
	return 0;
}
