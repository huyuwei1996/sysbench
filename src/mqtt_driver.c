#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef STDC_HEADERS
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <pthread.h>

#include "mqtt_driver.h"
#include "sb_list.h"
#include "sb_histogram.h"
#include "sb_ck_pr.h"

/* Global variables */
mqtt_globals_t mqtt_globals CK_CC_CACHELINE;

static sb_list_t drivers; /* list of available DB drivers */

static uint8_t stats_enabled;

static bool mqtt_global_initialized;
static pthread_once_t mqtt_global_once = PTHREAD_ONCE_INIT;

/* Static functions prototypes */

static int mqtt_parse_arguments(void);
static void mqtt_reset_stats(void);

/* DB layer arguments */

static sb_arg_t mqtt_args[] =
    {
        SB_OPT("mqtt-driver", "specifies mqtt driver to use "
                              "('help' to get list of available drivers)",
#ifdef USE_MOSQ
               "mosquitto",
#else
               NULL,
#endif
               STRING),
        SB_OPT("mqtt-debug", "print mqtt-specific debug information", "off", BOOL),

        SB_OPT_END};

/* Register available mqtt drivers and command line arguments */

int mqtt_register(void)
{
  sb_list_item_t *pos;
  mqtt_driver_t *drv;

  /* Register mqtt drivers */
  SB_LIST_INIT(&drivers);
#ifdef USE_MOSQ
  register_driver_mosquitto(&drivers);
#endif
#ifdef USE_PAHO
  register_driver_paho(&drivers);
#endif

  /* Register command line options for each driver */
  SB_LIST_FOR_EACH(pos, &drivers)
  {
    drv = SB_LIST_ENTRY(pos, mqtt_driver_t, listitem);
    if (drv->args != NULL)
      sb_register_arg_set(drv->args);
    drv->initialized = false;
    pthread_mutex_init(&drv->mutex, NULL);
  }
  /* Register general command line arguments for MQTT API */
  sb_register_arg_set(db_args);

  return 0;
}

/* Print list of available drivers and their options */

void mqtt_print_help(void)
{
  sb_list_item_t *pos;
  mqtt_driver_t *drv;

  log_text(LOG_NOTICE, "General mqtt options:\n");
  sb_print_options(db_args);
  log_text(LOG_NOTICE, "");

  log_text(LOG_NOTICE, "Compiled-in mqtt drivers:");
  SB_LIST_FOR_EACH(pos, &drivers)
  {
    drv = SB_LIST_ENTRY(pos, mqtt_driver_t, listitem);
    log_text(LOG_NOTICE, "  %s - %s", drv->sname, drv->lname);
  }
  log_text(LOG_NOTICE, "");
  SB_LIST_FOR_EACH(pos, &drivers)
  {
    drv = SB_LIST_ENTRY(pos, mqtt_driver_t, listitem);
    log_text(LOG_NOTICE, "%s options:", drv->sname);
    sb_print_options(drv->args);
  }
}

static void enable_print_stats(void)
{
  ck_pr_fence_store();
  ck_pr_store_8(&stats_enabled, 1);
}

static void disable_print_stats(void)
{
  ck_pr_store_8(&stats_enabled, 0);
  ck_pr_fence_store();
}

static bool check_print_stats(void)
{
  bool rc = ck_pr_load_8(&stats_enabled) == 1;
  ck_pr_fence_load();

  return rc;
}

static void mqtt_init(void)
{
  if (SB_LIST_IS_EMPTY(&drivers))
  {
    log_text(LOG_FATAL, "No MQTT drivers available");
    return;
  }

  if (mqtt_parse_arguments())
    return;

  /* Initialize timers if in debug mode */
  if (mqtt_globals.debug)
  {
  }

  mqtt_reset_stats();

  enable_print_stats();

  mqtt_global_initialized = true;
}

/* MQTT driver operations */

/*
  Initialize a driver specified by 'name' and return a handle to it
  If NULL is passed as a name, then use the driver passed in --mqtt-driver
  command line option
*/
mqtt_driver_t *mqtt_create(const char *name)
{
  mqtt_driver_t *drv = NULL;
  mqtt_driver_t *tmp;
  sb_list_item_t *pos;

  pthread_once(&mqtt_global_once, mqtt_init);

  if (!mqtt_global_initialized)
    goto err;

  if (name == NULL && db_globals.driver == NULL)
  {
    drv = SB_LIST_ENTRY(SB_LIST_ITEM_NEXT(&drivers), mqtt_driver_t, listitem);
    /* Is it the only driver available? */
    if (SB_LIST_ITEM_NEXT(&(drv->listitem)) ==
        SB_LIST_ITEM_PREV(&(drv->listitem)))
      log_text(LOG_INFO, "No MQTT drivers specified, using %s", drv->sname);
    else
    {
      log_text(LOG_FATAL, "Multiple MQTT drivers are available. "
                          "Use --mqtt-driver=name to specify which one to use");
      goto err;
    }
  }
  else
  {
    if (name == NULL)
      name = mqtt_globals.driver;

    SB_LIST_FOR_EACH(pos, &drivers)
    {
      tmp = SB_LIST_ENTRY(pos, mqtt_driver_t, listitem);
      if (!strcmp(tmp->sname, name))
      {
        drv = tmp;
        break;
      }
    }
  }

  if (drv == NULL)
  {
    log_text(LOG_FATAL, "invalid mqtt driver name: '%s'", name);
    goto err;
  }

  /* Initialize mqtt driver only once */
  pthread_mutex_lock(&drv->mutex);
  if (!drv->initialized)
  {
    if (drv->ops.init())
    {
      pthread_mutex_unlock(&drv->mutex);
      goto err;
    }
    drv->initialized = true;
  }
  pthread_mutex_unlock(&drv->mutex);

  if (drv->ops.thread_init != NULL && drv->ops.thread_init(sb_tls_thread_id))
  {
    log_text(LOG_FATAL, "thread-local driver initialization failed.");
    return NULL;
  }

  return drv;

err:
  return NULL;
}
