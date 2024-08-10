#ifndef MQTT_DRIVER_H
#define MQTT_DRIVER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sysbench.h"
#include "sb_list.h"
#include "sb_histogram.h"
#include "sb_counter.h"

/* MQTT Quality of Service levels */

typedef enum
{
  MQTT_QOS_0,
  MQTT_QOS_1,
  MQTT_QOS_2
} mqtt_qos_t;

/* Global MQTT API options */

typedef struct
{
  char *driver; /* Requested database driver */
  // char *broker_address; /* MQTT broker address */
  // int port;             /* MQTT broker port */
  // char *client_id;      /* Client identifier */
  unsigned char debug; /* Debug flag */
} mqtt_globals_t;

/* MQTT message structure */

typedef struct
{
  char *topic;
  char *payload;
  size_t payloadlen;
  mqtt_qos_t qos;
  int retain;
} mqtt_message_t;

/* MQTT subscription structure */

typedef struct
{
  char *topic;
  mqtt_qos_t qos;
} mqtt_subscription_t;

/* Forward declarations */

struct mqtt_client;
struct mqtt_message;
struct mqtt_subscription;

/* MQTT client operations definition */

typedef int mqtt_op_init(void);
typedef int mqtt_op_thread_init(int);
typedef int mqtt_op_connect(struct mqtt_client *);
typedef int mqtt_op_disconnect(struct mqtt_client *);
typedef int mqtt_op_publish(struct mqtt_client *, mqtt_message_t *);
typedef int mqtt_op_subscribe(struct mqtt_client *, mqtt_subscription_t *, size_t);
typedef int mqtt_op_unsubscribe(struct mqtt_client *, const char **, size_t);
typedef int mqtt_op_thread_done(int);
typedef int mqtt_op_done(void);

typedef struct
{
  mqtt_op_init *init;               /* initializate driver */
  mqtt_op_thread_init *thread_init; /* thread-local driver initialization */
  mqtt_op_connect *connect;         /* Connect to MQTT broker */
  mqtt_op_disconnect *disconnect;   /* Disconnect from MQTT broker */
  mqtt_op_publish *publish;         /* Publish a message */
  mqtt_op_subscribe *subscribe;     /* Subscribe to topics */
  mqtt_op_unsubscribe *unsubscribe; /* Unsubscribe from topics */
  mqtt_op_thread_done *thread_done; /* thread-local driver deinitialization */
  mqtt_op_done *done;               /* uninitialize driver */
} mqtt_ops_t;

/* MQTT driver definition */

typedef struct
{
  const char *sname;       /* short name */
  const char *lname;       /* long name */
  sb_arg_t *args;          /* driver command line arguments */
  mqtt_ops_t ops;          /* MQTT driver operations */
  sb_list_item_t listitem; /* can be linked in a list */
  bool initialized;
  pthread_mutex_t mutex;
} mqtt_driver_t;

/* MQTT connection structure */

typedef struct mqtt_conn
{
  const char *client_id; /* Client identifier */
  mqtt_driver_t *driver; /* MQTT driver for this connection */
  int thread_id;         /* Thread this connection belongs to */

} mqtt_conn_t;

extern mqtt_globals_t mqtt_globals;

/* MQTT abstraction layer calls */

int mqtt_register(void);
void mqtt_print_help(void);
mqtt_driver_t *mqtt_create(const char *);
int mqtt_destroy(mqtt_driver_t *);
int mqtt_connect(mqtt_driver_t *);
int mqtt_disconnect(mqtt_driver_t *);
int mqtt_publish(mqtt_driver_t *, mqtt_message_t *);
int mqtt_subscribe(mqtt_driver_t *, mqtt_subscription_t *, size_t);
int mqtt_unsubscribe(mqtt_driver_t *, const char **, size_t);
void mqtt_done(void);

/* MQTT drivers registrars */

#ifdef USE_MOSQ
int register_driver_mosquitto(sb_list_t *);
#endif

#ifdef USE_PAHO
int register_driver_paho(sb_list_t *);
#endif

#endif /* MQTT_DRIVER_H */