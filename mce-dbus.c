/**
 * @file mce-dbus.c
 * D-Bus handling code for the Mode Control Entity
 * <p>
 * Copyright © 2004-2009 Nokia Corporation and/or its subsidiary(-ies).
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 * @author Ismo Laitinen <ismo.laitinen@nokia.com>
 *
 * mce is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * mce is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mce.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <glib.h>

#include <stdarg.h>			/* va_start(), va_end() */
#include <stdlib.h>			/* exit(), EXIT_FAILURE */
#include <string.h>			/* strcmp() */
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>	/* dbus_connection_setup_with_g_main */

#include "mce.h"
#include "mce-dbus.h"

#include "mce-log.h"			/* mce_log(), LL_* */

#include "mce-gconf.h"

/** List of all D-Bus handlers */
static GSList *dbus_handlers = NULL;
/** List iterator for msg_handler */
static GSList *msg_handler_iter = NULL;

/** D-Bus handler structure */
typedef struct {
	gboolean (*callback)(DBusMessage *const msg);	/**< Handler callback */
	gchar *interface;		/**< The interface to listen on */
	gchar *rules;			/**< Additional matching rules */
	gchar *name;			/**< Method call or signal name */
	guint type;			/**< DBUS_MESSAGE_TYPE */
} handler_struct;

/** Pointer to the DBusConnection */
static DBusConnection *dbus_connection = NULL;



/** Return reference to dbus connection cached at mce-dbus module
 *
 * For use in situations where the abstraction provided by mce-dbus
 * makes things too complicated.
 *
 * Caller must release non-null return values with dbus_connection_unref().
 *
 * @return DBusConnection, or NULL if mce has no dbus connection
 */
DBusConnection *dbus_connection_get(void)
{
	if( !dbus_connection ) {
		mce_log(LL_WARN, "no dbus connection");
		return NULL;
	}
	return dbus_connection_ref(dbus_connection);
}

/**
 * Create a new D-Bus signal, with proper error checking
 * will exit the mainloop if an error occurs
 *
 * @param path The signal path
 * @param interface The signal interface
 * @param name The name of the signal to send
 * @return A new DBusMessage
 */
DBusMessage *dbus_new_signal(const gchar *const path,
			     const gchar *const interface,
			     const gchar *const name)
{
	DBusMessage *msg;

	if ((msg = dbus_message_new_signal(path, interface, name)) == NULL) {
		mce_log(LL_CRIT, "No memory for new signal!");
		// FIXME: this is not how one should exit from mainloop
		mce_quit_mainloop();
		exit(EXIT_FAILURE);
	}

	return msg;
}

#if 0
/**
 * Create a new D-Bus error message, with proper error checking
 * will exit the mainloop if an error occurs
 *
 * @param message The DBusMessage that caused the error message to be sent
 * @param error The message to send
 * @return A new DBusMessage
 */
static DBusMessage *dbus_new_error(DBusMessage *const message,
				   const gchar *const error)
{
	DBusMessage *error_msg;

	if ((error_msg = dbus_message_new_error(message, error,
						NULL)) == NULL) {
		mce_log(LL_CRIT, "No memory for new D-Bus error message!");
		// FIXME: this is not how one should exit from mainloop
		mce_quit_mainloop();
		exit(EXIT_FAILURE);
	}

	return error_msg;
}
#endif

/**
 * Create a new D-Bus method call, with proper error checking
 * will exit the mainloop if an error occurs
 *
 * @param service The method call service
 * @param path The method call path
 * @param interface The method call interface
 * @param name The name of the method to call
 * @return A new DBusMessage
 */
DBusMessage *dbus_new_method_call(const gchar *const service,
				  const gchar *const path,
				  const gchar *const interface,
				  const gchar *const name)
{
	DBusMessage *msg;

	if ((msg = dbus_message_new_method_call(service, path,
						interface, name)) == NULL) {
		mce_log(LL_CRIT,
			"Cannot allocate memory for D-Bus method call!");
		// FIXME: this is not how one should exit from mainloop
		mce_quit_mainloop();
		exit(EXIT_FAILURE);
	}

	return msg;
}

/**
 * Create a new D-Bus method call reply, with proper error checking
 * will exit the mainloop if an error occurs
 *
 * @param message The DBusMessage to reply to
 * @return A new DBusMessage
 */
DBusMessage *dbus_new_method_reply(DBusMessage *const message)
{
	DBusMessage *msg;

	if ((msg = dbus_message_new_method_return(message)) == NULL) {
		mce_log(LL_CRIT, "No memory for new reply!");
		// FIXME: this is not how one should exit from mainloop
		mce_quit_mainloop();
		exit(EXIT_FAILURE);
	}

	return msg;
}

/**
 * Send a D-Bus message
 * Side-effects: frees msg
 *
 * @param msg The D-Bus message to send
 * @return TRUE on success, FALSE on out of memory
 */
gboolean dbus_send_message(DBusMessage *const msg)
{
	gboolean status = FALSE;

	if (dbus_connection_send(dbus_connection, msg, NULL) == FALSE) {
		mce_log(LL_CRIT,
			"Out of memory when sending D-Bus message");
		goto EXIT;
	}

	dbus_connection_flush(dbus_connection);
	status = TRUE;

EXIT:
	dbus_message_unref(msg);

	return status;
}

/**
 * Send a D-Bus message and setup a reply callback
 * Side-effects: frees msg
 *
 * @param msg The D-Bus message to send
 * @param callback The reply callback
 * @return TRUE on success, FALSE on failure
 */
gboolean dbus_send_message_with_reply_handler(DBusMessage *const msg,
					      DBusPendingCallNotifyFunction callback)
{
	DBusPendingCall *pending_call;
	gboolean status = FALSE;

	if (dbus_connection_send_with_reply(dbus_connection, msg,
					    &pending_call, -1) == FALSE) {
		mce_log(LL_CRIT,
			"Out of memory when sending D-Bus message");
		goto EXIT;
	} else if (pending_call == NULL) {
		mce_log(LL_ERR,
			"D-Bus connection disconnected");
		goto EXIT;
	}

	dbus_connection_flush(dbus_connection);

	if (dbus_pending_call_set_notify(pending_call, callback, NULL, NULL) == FALSE) {
		mce_log(LL_CRIT,
			"Out of memory when sending D-Bus message");
		goto EXIT;
	}

	status = TRUE;

EXIT:
	dbus_message_unref(msg);

	return status;
}

/**
 * Generic function to send D-Bus messages and signals
 * to send a signal, call dbus_send with service == NULL
 *
 * @todo Make it possible to send D-Bus replies as well
 *
 * @param service D-Bus service; for signals, set to NULL
 * @param path D-Bus path
 * @param interface D-Bus interface
 * @param name The D-Bus method or signal name to send to
 * @param callback A reply callback, or NULL to set no reply;
 *                 for signals, this is unused, but please use NULL
 *                 for consistency
 * @param first_arg_type The DBUS_TYPE of the first argument in the list
 * @param ... The arguments to append to the D-Bus message;
 *            terminate with DBUS_TYPE_INVALID
 *            Note: the arguments MUST be passed by reference
 * @return TRUE on success, FALSE on failure
 */
gboolean dbus_send(const gchar *const service, const gchar *const path,
		   const gchar *const interface, const gchar *const name,
		   DBusPendingCallNotifyFunction callback,
		   int first_arg_type, ...)
{
	DBusMessage *msg;
	gboolean status = FALSE;
	va_list var_args;

	if (service != NULL) {
		msg = dbus_new_method_call(service, path, interface, name);

		if (callback == NULL)
			dbus_message_set_no_reply(msg, TRUE);
	} else {
		if (callback != NULL) {
			mce_log(LL_ERR,
				"Programmer snafu! "
				"dbus_send() called with a DBusPending "
				"callback for a signal.  Whoopsie!");
			callback = NULL;
		}

		msg = dbus_new_signal(path, interface, name);
	}

	/* Append the arguments, if any */
	va_start(var_args, first_arg_type);

	if (first_arg_type != DBUS_TYPE_INVALID) {
		if (dbus_message_append_args_valist(msg,
						    first_arg_type,
						    var_args) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to append arguments to D-Bus message "
				"for %s.%s",
				interface, name);
			dbus_message_unref(msg);
			goto EXIT;
		}
	}

	/* Send the signal / call the method */
	if (callback == NULL) {
		status = dbus_send_message(msg);
	} else {
		status = dbus_send_message_with_reply_handler(msg, callback);
	}

EXIT:
	va_end(var_args);

	return status;
}

/**
 * Generic function to send D-Bus messages, blocking version
 *
 * @param service D-Bus service
 * @param path D-Bus path
 * @param interface D-Bus interface
 * @param name The D-Bus method to send to
 * @param timeout The reply timeout in milliseconds to use
 * @param first_arg_type The DBUS_TYPE of the first argument in the list
 * @param ... The arguments to append to the D-Bus message;
 *            terminate with DBUS_TYPE_INVALID
 *            Note: the arguments MUST be passed by reference
 * @return A new DBusMessage with the reply on success, NULL on failure
 */
DBusMessage *dbus_send_with_block(const gchar *const service,
				  const gchar *const path,
				  const gchar *const interface,
				  const gchar *const name,
				  gint timeout, int first_arg_type, ...)
{
	DBusMessage *reply = NULL;
	DBusMessage *msg = NULL;
	va_list var_args;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	msg = dbus_new_method_call(service, path, interface, name);

	/* Append the arguments, if any */
	va_start(var_args, first_arg_type);

	if (first_arg_type != DBUS_TYPE_INVALID) {
		if (dbus_message_append_args_valist(msg,
						    first_arg_type,
						    var_args) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to append arguments to D-Bus message "
				"for %s.%s",
				interface, name);
			dbus_message_unref(msg);
			goto EXIT;
		}
	}

	/* Call the method */
	reply = dbus_connection_send_with_reply_and_block(dbus_connection, msg,
							  timeout, &error);

	dbus_message_unref(msg);

	if (dbus_error_is_set(&error) == TRUE) {
		mce_log(LL_ERR,
			"Error sending with reply to %s.%s: %s",
			interface, name, error.message);
		dbus_error_free(&error);
		reply = NULL;
	}

EXIT:
	va_end(var_args);

	return reply;
}

/**
 * Translate a D-Bus bus name into a pid
 *
 * @param bus_name A string with the bus name
 * @return The pid of the process, or -1 if no process could be identified
 */
pid_t dbus_get_pid_from_bus_name(const gchar *const bus_name)
{
	dbus_uint32_t pid = -1;
	DBusMessage *reply;

	if ((reply = dbus_send_with_block("org.freedesktop.DBus",
				          "/org/freedesktop/DBus/Bus",
				          "org.freedesktop.DBus",
				          "GetConnectionUnixProcessID", -1,
				          DBUS_TYPE_STRING, &bus_name,
				          DBUS_TYPE_INVALID)) != NULL) {
		dbus_message_get_args(reply, NULL,
				      DBUS_TYPE_UINT32, &pid,
				      DBUS_TYPE_INVALID);
		dbus_message_unref(reply);
	}

	return (pid_t)pid;
}

/**
 * D-Bus callback for the version get method call
 *
 * @param msg The D-Bus message to reply to
 * @return TRUE on success, FALSE on failure
 */
static gboolean version_get_dbus_cb(DBusMessage *const msg)
{
	static const gchar *const versionstring = G_STRINGIFY(PRG_VERSION);
	DBusMessage *reply = NULL;
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "Received version information request");

	/* Create a reply */
	reply = dbus_new_method_reply(msg);

	/* Append the version information */
	if (dbus_message_append_args(reply,
				     DBUS_TYPE_STRING, &versionstring,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append reply argument to D-Bus message "
			"for %s.%s",
			MCE_REQUEST_IF, MCE_VERSION_GET);
		dbus_message_unref(reply);
		goto EXIT;
	}

	/* Send the message */
	status = dbus_send_message(reply);

EXIT:
	return status;
}

/** Helper for appending gconf string list to dbus message
 *
 * @param conf GConfValue of string list type
 * @param pcount number of items in the returned array is stored here
 * @return array of string pointers that can be easily added to DBusMessage
 */
static const char **string_array_from_gconf_value(GConfValue *conf, int *pcount)
{
	const char **array = 0;
	int    count = 0;

	GSList *list, *item;

	if( conf->type != GCONF_VALUE_LIST )
		goto EXIT;

	if( gconf_value_get_list_type(conf) != GCONF_VALUE_STRING )
		goto EXIT;

	list = gconf_value_get_list(conf);

	for( item = list; item; item = item->next )
		++count;

	array = g_malloc_n(count, sizeof *array);
	count = 0;

	for( item = list; item; item = item->next )
		array[count++] = gconf_value_get_string(item->data);

EXIT:
	return *pcount = count, array;
}

/** Helper for appending gconf int list to dbus message
 *
 * @param conf GConfValue of int list type
 * @param pcount number of items in the returned array is stored here
 * @return array of integers that can be easily added to DBusMessage
 */
static dbus_int32_t *int_array_from_gconf_value(GConfValue *conf, int *pcount)
{
	dbus_int32_t *array = 0;
	int           count = 0;

	GSList *list, *item;

	if( conf->type != GCONF_VALUE_LIST )
		goto EXIT;

	if( gconf_value_get_list_type(conf) != GCONF_VALUE_INT )
		goto EXIT;

	list = gconf_value_get_list(conf);

	for( item = list; item; item = item->next )
		++count;

	array = g_malloc_n(count, sizeof *array);
	count = 0;

	for( item = list; item; item = item->next )
		array[count++] = gconf_value_get_int(item->data);

EXIT:
	return *pcount = count, array;
}

/** Helper for appending gconf bool list to dbus message
 *
 * @param conf GConfValue of bool list type
 * @param pcount number of items in the returned array is stored here
 * @return array of booleans that can be easily added to DBusMessage
 */
static dbus_bool_t *bool_array_from_gconf_value(GConfValue *conf, int *pcount)
{
	dbus_bool_t *array = 0;
	int          count = 0;

	GSList *list, *item;

	if( conf->type != GCONF_VALUE_LIST )
		goto EXIT;

	if( gconf_value_get_list_type(conf) != GCONF_VALUE_BOOL )
		goto EXIT;

	list = gconf_value_get_list(conf);

	for( item = list; item; item = item->next )
		++count;

	array = g_malloc_n(count, sizeof *array);
	count = 0;

	for( item = list; item; item = item->next )
		array[count++] = gconf_value_get_bool(item->data);

EXIT:
	return *pcount = count, array;
}

/** Helper for appending gconf float list to dbus message
 *
 * @param conf GConfValue of float list type
 * @param pcount number of items in the returned array is stored here
 * @return array of doubles that can be easily added to DBusMessage
 */
static double *float_array_from_gconf_value(GConfValue *conf, int *pcount)
{
	double *array = 0;
	int     count = 0;

	GSList *list, *item;

	if( conf->type != GCONF_VALUE_LIST )
		goto EXIT;

	if( gconf_value_get_list_type(conf) != GCONF_VALUE_FLOAT )
		goto EXIT;

	list = gconf_value_get_list(conf);

	for( item = list; item; item = item->next )
		++count;

	array = g_malloc_n(count, sizeof *array);
	count = 0;

	for( item = list; item; item = item->next )
		array[count++] = gconf_value_get_float(item->data);

EXIT:
	return *pcount = count, array;
}

/** Helper for deducing what kind of array signature we need for a list value
 *
 * @param type Non-complex gconf value type
 *
 * @return D-Bus signature needed for adding given type to a container
 */
static const char *type_signature(GConfValueType type)
{
	switch( type ) {
	case GCONF_VALUE_STRING: return DBUS_TYPE_STRING_AS_STRING;
	case GCONF_VALUE_INT:    return DBUS_TYPE_INT32_AS_STRING;
	case GCONF_VALUE_FLOAT:  return DBUS_TYPE_DOUBLE_AS_STRING;
	case GCONF_VALUE_BOOL:   return DBUS_TYPE_BOOLEAN_AS_STRING;
	default: break;
	}
	return 0;
}

/** Helper for deducing what kind of variant signature we need for a value
 *
 * @param conf GConf value
 *
 * @return D-Bus signature needed for adding given value to a container
 */
static const char *value_signature(GConfValue *conf)
{
	if( conf->type != GCONF_VALUE_LIST ) {
		return type_signature(conf->type);
	}

	switch( gconf_value_get_list_type(conf) ) {
	case GCONF_VALUE_STRING:
		return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_STRING_AS_STRING;
	case GCONF_VALUE_INT:
		return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING;
	case GCONF_VALUE_FLOAT:
		return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_DOUBLE_AS_STRING;
	case GCONF_VALUE_BOOL:
		return DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BOOLEAN_AS_STRING;
	default: break;
	}

	return 0;
}

/** Helper for appending GConfValue to dbus message
 *
 * @param reply DBusMessage under construction
 * @param conf GConfValue to be added to the reply
 *
 * @return TRUE if the value was succesfully appended, or FALSE on failure
 */
static gboolean append_gconf_value_to_dbus_message(DBusMessage *reply, GConfValue *conf)
{
	const char *sig = 0;

	DBusMessageIter body, variant, array;

	if( !(sig = value_signature(conf)) ) {
		goto bailout_message;
	}

	dbus_message_iter_init_append(reply, &body);

	if( !dbus_message_iter_open_container(&body, DBUS_TYPE_VARIANT,
					      sig, &variant) ) {
		goto bailout_message;
	}

	switch( conf->type ) {
	case GCONF_VALUE_STRING:
		{
			const char *arg = gconf_value_get_string(conf) ?: "";
			dbus_message_iter_append_basic(&variant,
						       DBUS_TYPE_STRING,
						       &arg);
		}
		break;

	case GCONF_VALUE_INT:
		{
			dbus_int32_t arg = gconf_value_get_int(conf);
			dbus_message_iter_append_basic(&variant,
						       DBUS_TYPE_INT32,
						       &arg);
		}
		break;

	case GCONF_VALUE_FLOAT:
		{
			double arg = gconf_value_get_float(conf);
			dbus_message_iter_append_basic(&variant,
						       DBUS_TYPE_DOUBLE,
						       &arg);
		}
		break;

	case GCONF_VALUE_BOOL:
		{
			dbus_bool_t arg = gconf_value_get_bool(conf);
			dbus_message_iter_append_basic(&variant,
						       DBUS_TYPE_BOOLEAN,
						       &arg);
		}
		break;

	case GCONF_VALUE_LIST:
		if( !(sig = type_signature(gconf_value_get_list_type(conf))) ) {
			goto bailout_variant;
		}

		if( !dbus_message_iter_open_container(&variant,
						      DBUS_TYPE_ARRAY,
						      sig, &array) ) {
			goto bailout_variant;
		}

		switch( gconf_value_get_list_type(conf) ) {
		case GCONF_VALUE_STRING:
			{
				int          cnt = 0;
				const char **arg = string_array_from_gconf_value(conf, &cnt);
				for( int i = 0; i < cnt; ++i ) {
					const char *str = arg[i];
					dbus_message_iter_append_basic(&array,
								       DBUS_TYPE_STRING,
								       &str);
				}
				g_free(arg);
			}
			break;
		case GCONF_VALUE_INT:
			{
				int           cnt = 0;
				dbus_int32_t *arg = int_array_from_gconf_value(conf, &cnt);
				dbus_message_iter_append_fixed_array(&array,
								     DBUS_TYPE_INT32,
								     &arg, cnt);
				g_free(arg);
			}
			break;
		case GCONF_VALUE_FLOAT:
			{
				int     cnt = 0;
				double *arg = float_array_from_gconf_value(conf, &cnt);
				dbus_message_iter_append_fixed_array(&array,
								     DBUS_TYPE_DOUBLE,
								     &arg, cnt);
				g_free(arg);
			}
			break;
		case GCONF_VALUE_BOOL:
			{
				int          cnt = 0;
				dbus_bool_t *arg = bool_array_from_gconf_value(conf, &cnt);
				dbus_message_iter_append_fixed_array(&array,
								     DBUS_TYPE_BOOLEAN,
								     &arg, cnt);
				g_free(arg);
			}
			break;

		default:
			goto bailout_array;
		}

		if( !dbus_message_iter_close_container(&variant, &array) ) {
			goto bailout_variant;
		}
		break;

	default:
		goto bailout_variant;
	}

	if( !dbus_message_iter_close_container(&body, &variant) ) {
		goto bailout_message;
	}
	return TRUE;

bailout_array:
	dbus_message_iter_abandon_container(&variant, &array);

bailout_variant:
	dbus_message_iter_abandon_container(&body, &variant);

bailout_message:
	return FALSE;
}

/* FIXME: Once the constants are in mce-dev these can be removed */
#ifndef MCE_CONFIG_GET
# define MCE_CONFIG_GET         "get_config"
# define MCE_CONFIG_SET         "set_config"
# define MCE_CONFIG_CHANGE_SIG  "config_change_ind"
#endif

/**
 * D-Bus callback for the config get method call
 *
 * @param msg The D-Bus message to reply to
 *
 * @return TRUE if reply message was successfully sent, FALSE on failure
 */
static gboolean config_get_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;
	DBusMessage *reply = NULL;
	const char *key = NULL;
	GError *err = NULL;
	GConfValue *conf = 0;

	DBusMessageIter body;

	mce_log(LL_DEBUG, "Received configuration query request");

	dbus_message_iter_init(msg, &body);

	/* HACK: The key used to be object path, not string.
	 *       Allow clients to use either one. */
	switch( dbus_message_iter_get_arg_type(&body) ) {
	case DBUS_TYPE_OBJECT_PATH:
	case DBUS_TYPE_STRING:
		dbus_message_iter_get_basic(&body, &key);
		dbus_message_iter_next(&body);
		break;

	default:
		reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
					       "expected string/object path");
		goto EXIT;
	}

	if( !(conf = gconf_client_get(gconf_client_get_default(), key, &err)) ) {
		reply = dbus_message_new_error(msg,
					       "com.nokia.mce.GConf.Error",
					       err->message ?: "unknown");
		goto EXIT;
	}

	if( !(reply = dbus_new_method_reply(msg)) )
		goto EXIT;

	if( !append_gconf_value_to_dbus_message(reply, conf) ) {
		dbus_message_unref(reply);
		reply = dbus_message_new_error(msg,
					       "com.nokia.mce.GConf.Error",
					       "constructing reply failed");
	}

EXIT:
	/* Send a reply if we have one */
	if( reply ) {
		if( dbus_message_get_no_reply(msg) ) {
			dbus_message_unref(reply), reply = 0;
			status = TRUE;
		}
		else {
			/* dbus_send_message unrefs the reply message */
			status = dbus_send_message(reply), reply = 0;
		}
	}

	if( conf )
		gconf_value_free(conf);

	g_clear_error(&err);

	return status;
}

/** Send configuration changed notification signal
 *
 * @param entry changed setting
 */
void mce_dbus_send_config_notification(GConfEntry *entry)
{
	const char  *key = 0;
	GConfValue  *val = 0;
	DBusMessage *sig = 0;

	if( !entry )
		goto EXIT;

	if( !(key = gconf_entry_get_key(entry)) )
		goto EXIT;

	if( !(val = gconf_entry_get_value(entry)) )
		goto EXIT;

	mce_log(LL_DEBUG, "%s: changed", key);

	sig = dbus_message_new_signal(MCE_SIGNAL_PATH,
				      MCE_SIGNAL_IF,
				      MCE_CONFIG_CHANGE_SIG);

	if( !sig ) goto EXIT;

	dbus_message_append_args(sig,
				 DBUS_TYPE_STRING, &key,
				 DBUS_TYPE_INVALID);

	append_gconf_value_to_dbus_message(sig, val);

	dbus_send_message(sig), sig = 0;

EXIT:

	if( sig ) dbus_message_unref(sig);

	return;
}

/** Release GSList of GConfValue objects
 *
 * @param list GSList where item->data members are pointers to GConfValue
 */
static void value_list_free(GSList *list)
{
  g_slist_free_full(list, (GDestroyNotify)gconf_value_free);
}

/** Convert D-Bus string array into GSList of GConfValue objects
 *
 * @param iter D-Bus message iterator at DBUS_TYPE_ARRAY
 * @return GSList where item->data members are pointers to GConfValue
 */
static GSList *value_list_from_string_array(DBusMessageIter *iter)
{
	GSList *res = 0;

	DBusMessageIter subiter;

	dbus_message_iter_recurse(iter, &subiter);

	int i = 0;
	while ( dbus_message_iter_get_arg_type(&subiter) == DBUS_TYPE_STRING ) {
		const char *tmp = 0;
		dbus_message_iter_get_basic(&subiter, &tmp);
		dbus_message_iter_next(&subiter);

		mce_log(LL_INFO, "arr[%d] = string:%s", i++, tmp);

		GConfValue *value = gconf_value_new(GCONF_VALUE_STRING);
		gconf_value_set_string(value, tmp);
		res = g_slist_prepend(res, value);
	}

	res = g_slist_reverse(res);

	return res;
}

/** Convert D-Bus int32 array into GSList of GConfValue objects
 *
 * @param iter D-Bus message iterator at DBUS_TYPE_ARRAY
 * @return GSList where item->data members are pointers to GConfValue
 */
static GSList *value_list_from_int_array(DBusMessageIter *iter)
{
	GSList *res = 0;

	DBusMessageIter subiter;

	dbus_message_iter_recurse(iter, &subiter);

	int i = 0;
	while ( dbus_message_iter_get_arg_type(&subiter) == DBUS_TYPE_INT32 ) {
		dbus_int32_t tmp = 0;
		dbus_message_iter_get_basic(&subiter, &tmp);
		dbus_message_iter_next(&subiter);

		mce_log(LL_INFO, "arr[%d] = int:%d", i++, tmp);

		GConfValue *value = gconf_value_new(GCONF_VALUE_INT);
		gconf_value_set_int(value, tmp);
		res = g_slist_prepend(res, value);
	}

	res = g_slist_reverse(res);

	return res;
}

/** Convert D-Bus bool array into GSList of GConfValue objects
 *
 * @param iter D-Bus message iterator at DBUS_TYPE_ARRAY
 * @return GSList where item->data members are pointers to GConfValue
 */
static GSList *value_list_from_bool_array(DBusMessageIter *iter)
{
	GSList *res = 0;

	DBusMessageIter subiter;

	dbus_message_iter_recurse(iter, &subiter);

	int i = 0;
	while ( dbus_message_iter_get_arg_type(&subiter) == DBUS_TYPE_BOOLEAN ) {
		dbus_bool_t tmp = 0;
		dbus_message_iter_get_basic(&subiter, &tmp);
		dbus_message_iter_next(&subiter);

		mce_log(LL_INFO, "arr[%d] = bool:%s", i++, tmp ? "true" : "false");

		GConfValue *value = gconf_value_new(GCONF_VALUE_BOOL);
		gconf_value_set_bool(value, tmp);
		res = g_slist_prepend(res, value);
	}

	res = g_slist_reverse(res);

	return res;
}

/** Convert D-Bus double array into GSList of GConfValue objects
 *
 * @param iter D-Bus message iterator at DBUS_TYPE_ARRAY
 * @return GSList where item->data members are pointers to GConfValue
 */
static GSList *value_list_from_float_array(DBusMessageIter *iter)
{
	GSList *res = 0;

	DBusMessageIter subiter;

	dbus_message_iter_recurse(iter, &subiter);

	int i = 0;
	while ( dbus_message_iter_get_arg_type(&subiter) == DBUS_TYPE_DOUBLE ) {
		double tmp = 0;
		dbus_message_iter_get_basic(&subiter, &tmp);
		dbus_message_iter_next(&subiter);

		mce_log(LL_INFO, "arr[%d] = float:%g", i++, tmp);

		GConfValue *value = gconf_value_new(GCONF_VALUE_FLOAT);
		gconf_value_set_float(value, tmp);
		res = g_slist_prepend(res, value);
	}

	res = g_slist_reverse(res);

	return res;
}

/**
 * D-Bus callback for the config set method call
 *
 * @param msg The D-Bus message to reply to
 *
 * @return TRUE if reply message was successfully sent, FALSE on failure
 */
static gboolean config_set_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;
	DBusMessage *reply = NULL;
	const char *key = NULL;
	GError *err = NULL;
	GConfClient *client = 0;
	GSList *list = 0;

	DBusError error = DBUS_ERROR_INIT;
	DBusMessageIter body, iter;

	mce_log(LL_DEBUG, "Received configuration change request");

	if( !(client = gconf_client_get_default()) )
		goto EXIT;

	dbus_message_iter_init(msg, &body);

	/* HACK: The key used to be object path, not string.
	 *       Allow clients to use either one. */
	switch( dbus_message_iter_get_arg_type(&body) ) {
	case DBUS_TYPE_OBJECT_PATH:
	case DBUS_TYPE_STRING:
		dbus_message_iter_get_basic(&body, &key);
		dbus_message_iter_next(&body);
		break;

	default:
		reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
					       "expected string/object path");
		goto EXIT;
	}

	if( dbus_message_iter_get_arg_type(&body) == DBUS_TYPE_VARIANT ) {
		dbus_message_iter_recurse(&body, &iter);
		dbus_message_iter_next(&body);
	}
	else if( dbus_message_iter_get_arg_type(&body) == DBUS_TYPE_ARRAY ) {
		/* HACK: dbus-send does not know how to handle nested
		 * containers,  so it can't be used to send variant
		 * arrays 'variant:array:int32:1,2,3', so we allow array
		 * requrest without variant too ... */
		iter = body;
	}
	else {
		reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
					       "expected variant");
		goto EXIT;
	}

	switch( dbus_message_iter_get_arg_type(&iter) ) {
	case DBUS_TYPE_BOOLEAN:
		{
			dbus_bool_t arg = 0;
			dbus_message_iter_get_basic(&iter, &arg);
			gconf_client_set_bool(client, key, arg, &err);
		}
		break;
	case DBUS_TYPE_INT32:
		{
			dbus_int32_t arg = 0;
			dbus_message_iter_get_basic(&iter, &arg);
			gconf_client_set_int(client, key, arg, &err);
		}
		break;
	case DBUS_TYPE_DOUBLE:
		{
			double arg = 0;
			dbus_message_iter_get_basic(&iter, &arg);
			gconf_client_set_float(client, key, arg, &err);
		}
		break;
	case DBUS_TYPE_STRING:
		{
			const char *arg = 0;
			dbus_message_iter_get_basic(&iter, &arg);
			gconf_client_set_string(client, key, arg, &err);
		}
		break;

	case DBUS_TYPE_ARRAY:
		switch( dbus_message_iter_get_element_type(&iter) ) {
		case DBUS_TYPE_BOOLEAN:
			list = value_list_from_bool_array(&iter);
			gconf_client_set_list(client, key, GCONF_VALUE_BOOL, list, &err);
			break;
		case DBUS_TYPE_INT32:
			list = value_list_from_int_array(&iter);
			gconf_client_set_list(client, key, GCONF_VALUE_INT, list, &err);
			break;
		case DBUS_TYPE_DOUBLE:
			list = value_list_from_float_array(&iter);
			gconf_client_set_list(client, key, GCONF_VALUE_FLOAT, list, &err);
			break;
		case DBUS_TYPE_STRING:
			list = value_list_from_string_array(&iter);
			gconf_client_set_list(client, key, GCONF_VALUE_STRING, list, &err);
			break;
		default:
			reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
						       "unexpected value array type");
			goto EXIT;

		}
		break;

	default:
		reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
					       "unexpected value type");
		goto EXIT;
	}

	if( err )
	{
		/* some of the above gconf_client_set_xxx() calls failed */
		reply = dbus_message_new_error(msg,
					       "com.nokia.mce.GConf.Error",
					       err->message ?: "unknown");
		goto EXIT;
	}

	/* we changed something */
	gconf_client_suggest_sync(client, &err);
	if( err ) {
		mce_log(LL_ERR, "gconf_client_suggest_sync: %s", err->message);
	}

	if( !(reply = dbus_new_method_reply(msg)) )
		goto EXIT;

	/* it is either error reply or true, and we got here... */
	{
		dbus_bool_t arg = TRUE;
		dbus_message_append_args(reply,
					 DBUS_TYPE_BOOLEAN, &arg,
					 DBUS_TYPE_INVALID);
	}

EXIT:
	value_list_free(list);

	/* Send a reply if we have one */
	if( reply ) {
		if( dbus_message_get_no_reply(msg) ) {
			dbus_message_unref(reply), reply = 0;
			status = TRUE;
		}
		else {
			/* dbus_send_message unrefs the reply message */
			status = dbus_send_message(reply), reply = 0;
		}
	}

	g_clear_error(&err);
	dbus_error_free(&error);


	return status;
}

/**
 * D-Bus rule checker
 *
 * @param msg The D-Bus message being checked
 * @param rules The rule string to check against
 * @return TRUE if message matches the rules,
	   FALSE if not
 */
static gboolean check_rules(DBusMessage *const msg,
			    const char *rules)
{
	if (rules == NULL)
		return TRUE;
	rules += strspn(rules, " ");;

	while (*rules != '\0') {
		const char *eq;
		const char *value;
		const char *value_end;
		const char *val = NULL;
		gboolean quot = FALSE;

		if ((eq = strchr(rules, '=')) == NULL)
			return FALSE;
		eq += strspn(eq, " ");

		if (eq[1] == '\'') {
			value = eq + 2;
			value_end = strchr(value, '\'');
			quot = TRUE;
		} else {
			value = eq + 1;
			value_end = strchrnul(value, ',');
		}

		if (value_end == NULL)
			return FALSE;

		if (strncmp(rules, "arg", 3) == 0) {
			int fld = atoi(rules + 3);

			DBusMessageIter iter;

			if (dbus_message_iter_init(msg, &iter) == FALSE)
				return FALSE;

			for (; fld; fld--) {
				if (dbus_message_iter_has_next(&iter) == FALSE)
					return FALSE;
				dbus_message_iter_next(&iter);
			}

			if (dbus_message_iter_get_arg_type(&iter) !=
			    DBUS_TYPE_STRING)
				return FALSE;
			dbus_message_iter_get_basic(&iter, &val);

		} else if (strncmp(rules, "path", 4) == 0) {
			val = dbus_message_get_path(msg);
		}

		if (((value_end != NULL) &&
		     ((strncmp(value, val, value_end - value) != 0) ||
		      (val[value_end - value] != '\0'))) ||
		    ((value_end == NULL) &&
		     (strcmp(value, val) != 0)))
			return FALSE;

		if (value_end == NULL)
			break;

		rules = value_end + (quot == TRUE ? 1 : 0);
		rules += strspn(rules, " ");;

		if (*rules == ',')
			rules++;
		rules += strspn(rules, " ");;
	}

	return TRUE;
}

/**
 * D-Bus message handler
 *
 * @param connection Unused
 * @param msg The D-Bus message received
 * @param user_data Unused
 * @return DBUS_HANDLER_RESULT_HANDLED for handled messages
 *         DBUS_HANDLER_RESULT_NOT_HANDLED for unhandled messages
 */
static DBusHandlerResult msg_handler(DBusConnection *const connection,
				     DBusMessage *const msg,
				     gpointer const user_data)
{
	guint status = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	(void)connection;
	(void)user_data;

	for (msg_handler_iter = dbus_handlers;
	     msg_handler_iter != NULL;
	     msg_handler_iter = g_slist_next(msg_handler_iter)) {
		handler_struct *handler = msg_handler_iter->data;

		switch (handler->type) {
		case DBUS_MESSAGE_TYPE_METHOD_CALL:
			if (dbus_message_is_method_call(msg,
							handler->interface,
							handler->name) == TRUE) {
				handler->callback(msg);
				status = DBUS_HANDLER_RESULT_HANDLED;
				goto EXIT;
			}

			break;

		case DBUS_MESSAGE_TYPE_ERROR:
			if (dbus_message_is_error(msg,
						  handler->name) == TRUE) {
				handler->callback(msg);
			}

			break;

		case DBUS_MESSAGE_TYPE_SIGNAL:
			if ((dbus_message_is_signal(msg,
						   handler->interface,
						   handler->name) == TRUE) &&
			    (check_rules(msg, handler->rules) == TRUE)) {
				handler->callback(msg);
			}

			break;

		default:
			mce_log(LL_ERR,
				"There's a bug somewhere in MCE; something "
				"has registered an invalid D-Bus handler");
			break;
		}
	}

EXIT:
	return status;
}

/**
 * Register a D-Bus signal or method handler
 *
 * @param interface The interface to listen on
 * @param name The signal/method call to listen for
 * @param rules Additional matching rules
 * @param type DBUS_MESSAGE_TYPE
 * @param callback The callback function
 * @return A D-Bus handler cookie on success, NULL on failure
 */
gconstpointer mce_dbus_handler_add(const gchar *const interface,
				    const gchar *const name,
				    const gchar *const rules,
				    const guint type,
				    gboolean (*callback)(DBusMessage *const msg))
{
	handler_struct *h = NULL;
	gchar *match = NULL;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	if (type == DBUS_MESSAGE_TYPE_SIGNAL) {
		if ((match = g_strdup_printf("type='signal'"
					     "%s%s%s"
					     ", member='%s'"
					     "%s%s",
					     interface ? ", interface='" : "",
					     interface ? interface : "",
					     interface ? "'" : "",
					     name,
					     rules ? ", " : "",
					     rules ? rules : "")) == NULL) {
			mce_log(LL_CRIT,
				"Failed to allocate memory for match");
			goto EXIT;
		}
	} else if (type != DBUS_MESSAGE_TYPE_METHOD_CALL) {
		mce_log(LL_CRIT,
			"There's definitely a programming error somewhere; "
			"MCE is trying to register an invalid message type");
		goto EXIT;
	}

	if ((h = g_try_malloc(sizeof (*h))) == NULL) {
		mce_log(LL_CRIT, "Failed to allocate memory for h");
		goto EXIT;
	}

	h->interface = NULL;

	if (interface && (h->interface = g_strdup(interface)) == NULL) {
		mce_log(LL_CRIT, "Failed to allocate memory for h->interface");
		g_free(h);
		h = NULL;
		goto EXIT;
	}

	h->rules = NULL;

	if (rules && (h->rules = g_strdup(rules)) == NULL) {
		mce_log(LL_CRIT, "Failed to allocate memory for h->rules");
		g_free(h->interface);
		g_free(h);
		h = NULL;
		goto EXIT;
	}

	if ((h->name = g_strdup(name)) == NULL) {
		mce_log(LL_CRIT, "Failed to allocate memory for h->name");
		g_free(h->interface);
		g_free(h->rules);
		g_free(h);
		h = NULL;
		goto EXIT;
	}

	h->type = type;
	h->callback = callback;

	/* Only register D-Bus matches for signals */
	if (match != NULL) {
		dbus_bus_add_match(dbus_connection, match, &error);

		if (dbus_error_is_set(&error) == TRUE) {
			mce_log(LL_CRIT,
				"Failed to add D-Bus match '%s' for '%s'; %s",
				match, h->interface, error.message);
			dbus_error_free(&error);
			g_free(h->interface);
			g_free(h->rules);
			g_free(h);
			h = NULL;
			goto EXIT;
		}
	}

	dbus_handlers = g_slist_prepend(dbus_handlers, h);

EXIT:
	g_free(match);

	return h;
}

/**
 * Unregister a D-Bus signal or method handler
 *
 * @param cookie A D-Bus handler cookie for
 *               the handler that should be removed
 */
void mce_dbus_handler_remove(gconstpointer cookie)
{
	handler_struct *h = (handler_struct *)cookie;
	gchar *match = NULL;
	DBusError error;
	GSList *iter;

	/* Register error channel */
	dbus_error_init(&error);

	if (h->type == DBUS_MESSAGE_TYPE_SIGNAL) {
		match = g_strdup_printf("type='signal'"
					"%s%s%s"
					", member='%s'"
					"%s%s",
					h->interface ? ", interface='" : "",
					h->interface ? h->interface : "",
					h->interface ? "'" : "",
					h->name,
					h->rules ? ", " : "",
					h->rules ? h->rules : "");

		if (match != NULL) {
			dbus_bus_remove_match(dbus_connection, match, &error);

			if (dbus_error_is_set(&error) == TRUE) {
				mce_log(LL_CRIT,
					"Failed to remove D-Bus match "
					"'%s' for '%s': %s",
					match, h->interface, error.message);
				dbus_error_free(&error);
			}
		} else {
			mce_log(LL_CRIT,
				"Failed to allocate memory for match");
		}
	} else if (h->type != DBUS_MESSAGE_TYPE_METHOD_CALL) {
		mce_log(LL_ERR,
			"There's definitely a programming error somewhere; "
			"MCE is trying to unregister an invalid message type");
		/* Don't abort here, since we want to unregister it anyway */
	}

	if ((iter = g_slist_find(dbus_handlers, h))) {
		if (iter == msg_handler_iter)
			msg_handler_iter = iter->next;
		dbus_handlers = g_slist_remove_link(dbus_handlers, iter);
	}

	g_free(h->interface);
	g_free(h->rules);
	g_free(h->name);
	g_free(h);
	g_free(match);
}

/**
 * Unregister a D-Bus signal or method handler;
 * to be used with g_slist_foreach()
 *
 * @param handler A pointer to the handler struct that should be removed
 * @param user_data Unused
 */
static void mce_dbus_handler_remove_foreach(gpointer handler,
					    gpointer user_data)
{
	(void)user_data;

	mce_dbus_handler_remove(handler);
}

/**
 * Custom compare function used to find owner monitor entries
 *
 * @param owner_id An owner monitor cookie
 * @param name The name to search for
 * @return Less than, equal to, or greater than zero depending
 *         whether the name of the rules with the id owner_id
 *         is less than, equal to, or greater than name
 */
static gint monitor_compare(gconstpointer owner_id, gconstpointer name)
{
	handler_struct *hs = (handler_struct *)owner_id;

	return strcmp(hs->rules, name);
}

/**
 * Locate the specified D-Bus service in the monitor list
 *
 * @param service The service to check for
 * @param monitor_list The monitor list check
 * @return A pointer to the entry if the entry is in the list,
 *         NULL if the entry is not in the list
 */
static GSList *find_monitored_service(const gchar *service,
				      GSList *monitor_list)
{
	gchar *rule = NULL;
	GSList *tmp = NULL;

	if (service == NULL)
		goto EXIT;

	if ((rule = g_strdup_printf("arg1='%s'", service)) == NULL)
		goto EXIT;

	tmp = g_slist_find_custom(monitor_list, rule, monitor_compare);

	g_free(rule);

EXIT:
	return tmp;
}

/**
 * Check whether the D-Bus service in question is in the monitor list or not
 *
 * @param service The service to check for
 * @param monitor_list The monitor list check
 * @return TRUE if the entry is in the list,
 *         FALSE if the entry is not in the list
 */
gboolean mce_dbus_is_owner_monitored(const gchar *service,
				     GSList *monitor_list)
{
	return (find_monitored_service(service, monitor_list) != NULL);
}

/**
 * Generate and handle fake owner gone message
 *
 * @param data Name of owner that is gone
 * @return Always FALSE
 */
static gboolean fake_owner_gone(gpointer data)
{
	DBusMessage *msg;
	const char *empty = "";

	msg = dbus_message_new_signal("/org/freedesktop/DBus",
				      "org.freedesktop.DBus",
				      "NameOwnerChanged");
	dbus_message_append_args(msg, DBUS_TYPE_STRING, &data,
				 DBUS_TYPE_STRING, &data,
				 DBUS_TYPE_STRING, &empty,
				 DBUS_TYPE_INVALID);

	msg_handler(NULL, msg, NULL);

	return FALSE;
}

/**
 * Add a service to a D-Bus owner monitor list
 *
 * @param service The service to monitor
 * @param callback A D-Bus monitor callback
 * @param monitor_list The list of monitored services
 * @param max_num The maximum number of monitored services;
 *                keep this number low, for performance
 *                and memory usage reasons
 * @return -1 if the amount of monitored services would be exceeded;
 *            if either of service or monitor_list is NULL,
 *            or if adding a D-Bus monitor fails
 *          0 if the service is already monitored
 *         >0 represents the number of monitored services after adding
 *            this service
 */
gssize mce_dbus_owner_monitor_add(const gchar *service,
				  gboolean (*callback)(DBusMessage *const msg),
				  GSList **monitor_list,
				  gssize max_num)
{
	gconstpointer cookie;
	gchar *rule = NULL;
	gssize retval = -1;
	gssize num;

	/* If service or monitor_list is NULL, fail */
	if (service == NULL) {
		mce_log(LL_CRIT,
			"A programming error occured; "
			"mce_dbus_owner_monitor_add() called with "
			"service == NULL");
		goto EXIT;
	} else if (monitor_list == NULL) {
		mce_log(LL_CRIT,
			"A programming error occured; "
			"mce_dbus_owner_monitor_add() called with "
			"monitor_list == NULL");
		goto EXIT;
	}

	/* If the service is already in the list, we're done */
	if (find_monitored_service(service, *monitor_list) != NULL) {
		retval = 0;
		goto EXIT;
	}

	/* If the service isn't in the list, and the list already
	 * contains max_num elements, bail out
	 */
	if ((num = g_slist_length(*monitor_list)) == max_num)
		goto EXIT;

	if ((rule = g_strdup_printf("arg1='%s'", service)) == NULL)
		goto EXIT;

	/* Add ownership monitoring for the service */
	cookie = mce_dbus_handler_add("org.freedesktop.DBus",
				      "NameOwnerChanged",
				      rule,
				      DBUS_MESSAGE_TYPE_SIGNAL,
				      callback);

	if (cookie == NULL)
		goto EXIT;

	*monitor_list = g_slist_prepend(*monitor_list, (gpointer)cookie);
	retval = num + 1;

	if (dbus_bus_name_has_owner(dbus_connection, service, NULL) == FALSE)
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
				fake_owner_gone, g_strdup(service),
				g_free);

EXIT:
	g_free(rule);

	return retval;
}

/**
 * Remove a service from a D-Bus owner monitor list
 *
 * @param service The service to remove from the monitor list
 * @param monitor_list The monitor list to remove the service from
 * @return The new number of monitored connections;
 *         -1 if the service was not monitored,
 *            if removing monitoring failed,
 *            or if either of service or monitor_list is NULL
 */
gssize mce_dbus_owner_monitor_remove(const gchar *service,
				     GSList **monitor_list)
{
	gssize retval = -1;
	GSList *tmp;

	/* If service or monitor_list is NULL, fail */
	if ((service == NULL) || (monitor_list == NULL))
		goto EXIT;

	/* If the service is not in the list, fail */
	if ((tmp = find_monitored_service(service, *monitor_list)) == NULL)
		goto EXIT;

	/* Remove ownership monitoring for the service */
	mce_dbus_handler_remove(tmp->data);
	*monitor_list = g_slist_remove(*monitor_list, tmp->data);
	retval = g_slist_length(*monitor_list);

EXIT:
	return retval;
}

/**
 * Remove all monitored service from a D-Bus owner monitor list
 *
 * @param monitor_list The monitor list to remove the service from
 */
void mce_dbus_owner_monitor_remove_all(GSList **monitor_list)
{
	if ((monitor_list != NULL) && (*monitor_list != NULL)) {
		g_slist_foreach(*monitor_list,
				(GFunc)mce_dbus_handler_remove_foreach, NULL);
		g_slist_free(*monitor_list);
		*monitor_list = NULL;
	}
}

/**
 * Acquire D-Bus services
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean dbus_acquire_services(void)
{
	gboolean status = FALSE;
	int ret;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	ret = dbus_bus_request_name(dbus_connection, MCE_SERVICE, 0, &error);

	if (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		mce_log(LL_DEBUG, "Service %s acquired", MCE_SERVICE);
	} else {
		mce_log(LL_CRIT, "Cannot acquire service: %s", error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * Initialise the message handler used by MCE
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean dbus_init_message_handler(void)
{
	gboolean status = FALSE;

	if (dbus_connection_add_filter(dbus_connection, msg_handler,
				       NULL, NULL) == FALSE) {
		mce_log(LL_CRIT, "Failed to add D-Bus filter");
		goto EXIT;
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * Init function for the mce-dbus component
 * Pre-requisites: glib mainloop registered
 *
 * @param systembus TRUE to use system bus, FALSE to use session bus
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_dbus_init(const gboolean systembus)
{
	DBusBusType bus_type = DBUS_BUS_SYSTEM;
	gboolean status = FALSE;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	if (systembus == FALSE)
		bus_type = DBUS_BUS_SESSION;

	mce_log(LL_DEBUG, "Establishing D-Bus connection");

	/* Establish D-Bus connection */
	if ((dbus_connection = dbus_bus_get(bus_type,
					    &error)) == NULL) {
		mce_log(LL_CRIT, "Failed to open connection to message bus; %s",
			  error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	mce_log(LL_DEBUG, "Connecting D-Bus to the mainloop");

	/* Connect D-Bus to the mainloop */
	dbus_connection_setup_with_g_main(dbus_connection, NULL);

	mce_log(LL_DEBUG, "Acquiring D-Bus service");

	/* Acquire D-Bus service */
	if (dbus_acquire_services() == FALSE)
		goto EXIT;

	/* Initialise message handlers */
	if (dbus_init_message_handler() == FALSE)
		goto EXIT;

	/* Register callbacks that are handled inside mce-dbus.c */

	/* get_version */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_VERSION_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 version_get_dbus_cb) == NULL)
		goto EXIT;

	/* get_config */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_CONFIG_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 config_get_dbus_cb) == NULL)
		goto EXIT;

	/* set_config */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_CONFIG_SET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 config_set_dbus_cb) == NULL)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * Exit function for the mce-dbus component
 */
void mce_dbus_exit(void)
{
	/* Unregister D-Bus handlers */
	if (dbus_handlers != NULL) {
		g_slist_foreach(dbus_handlers,
				(GFunc)mce_dbus_handler_remove_foreach, NULL);
		g_slist_free(dbus_handlers);
		dbus_handlers = NULL;
	}

	/* If there is an established D-Bus connection, unreference it */
	if (dbus_connection != NULL) {
		mce_log(LL_DEBUG, "Unreferencing D-Bus connection");
		dbus_connection_unref(dbus_connection);
		dbus_connection = NULL;
	}

	return;
}
