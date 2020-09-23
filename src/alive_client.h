/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef ALIVE_CLIENT_H
#define ALIVE_CLIENT_H 1


#include <time.h>
#include <stdint.h>

enum alive_os_type { GENERIC, VXWORKS, LINUX, DARWIN, WINDOWS};

struct alive_iocinfo_extra_vxworks
{
  char *bootdev;
  uint32_t unitnum;
  uint32_t procnum;
  char *boothost_name;
  char *bootfile;
  char *address;
  char *backplane_address;
  char *boothost_address;
  char *gateway_address;
  uint32_t flags;
  char *target_name;
  char *startup_script;
  char *other;
};

struct alive_iocinfo_extra_linux
{
  char *user;
  char *group;
  char *hostname;
};

struct alive_iocinfo_extra_darwin
{
  char *user;
  char *group;
  char *hostname;
};

struct alive_iocinfo_extra_windows
{
  char *user;
  char *machine;
};


struct alive_env
{
  uint16_t number_envvar;
  char **envvar_key;
  char **envvar_value;

  uint16_t extra_type;
  void *extra;
};

enum alive_statuses { STATUS_UNKNOWN, STATUS_DOWN_UNKNOWN, STATUS_DOWN,
                      STATUS_UP, STATUS_CONFLICT };

struct alive_ioc
{
  char *ioc_name;
  union 
  {
    uint32_t raw_ip_address;
    unsigned char ip_address[4];
  };
  uint8_t status;
  time_t time_value;
  uint32_t user_msg;

  struct alive_env *environment;
};

struct alive_db
{
  time_t current_time;
  time_t start_time;
  uint16_t number_ioc;
  struct alive_ioc *ioc;
};

/////////////

enum alive_instance_statuses { INSTANCE_STATUS_UP, INSTANCE_STATUS_DOWN,
                               INSTANCE_STATUS_UNTIMED_DOWN,
                               INSTANCE_STATUS_MAYBE_UP,
                               INSTANCE_STATUS_MAYBE_DOWN };


struct alive_instance
{
  uint8_t status;
  union 
  {
    uint32_t raw_ip_address;
    unsigned char ip_address[4];
  };
  uint16_t origin_port;
  uint32_t heartbeat;
  uint16_t period;
  uint32_t incarnation; // boot time sent by IOC
  uint32_t boottime;    // boot time, computed to daemon's perspective
  uint32_t timestamp;   // time ping received by daemon
  uint16_t reply_port;
  uint32_t user_msg;

  struct alive_env *environment;
};

struct alive_detailed_ioc
{
  time_t current_time;
  time_t start_time;

  char *ioc_name;
  uint8_t overall_status;
  time_t overall_time_value;

  uint32_t number_instances;
  struct alive_instance *instances;
};

//////////////////////////////////
  
enum alive_events { NONE, FAIL, BOOT, RECOVER, MESSAGE, CONFLICT_START, 
                    CONFLICT_STOP};

struct alive_ioc_event_item
{
  uint32_t event;
  uint32_t user_msg;
  uint32_t time;
  union 
  {
    uint32_t raw_ip_address;
    unsigned char ip_address[4];
  };
};

struct alive_ioc_event_db
{
  time_t current_time;
  time_t start_time;
  int number;
  struct alive_ioc_event_item *instances;
};

/////////////////////////////////////////////

char *alive_default_database( int *port);
char *alive_client_api_version( void);

struct alive_db *alive_get_db( char *server, int port);
struct alive_db *alive_get_iocs( char *server, int port, int number, 
                                 char **names);
struct alive_db *alive_get_ioc( char *server, int port, char *name);
void alive_free_db( struct alive_db *iocs);

// Does not remove 'ioc', only it's allocated parts, so that a pointer 
// to a statically allocated structure can be passed to it.
void alive_free_ioc( struct alive_ioc *ioc);

/* // TEMPORARY */
/* // This function is intended to disappear when events added to API */
/* struct alive_env *alive_unpack_env( char *data, int length); */

struct alive_detailed_ioc *alive_get_debug( char *server, int port, 
                                            char *name);
struct alive_detailed_ioc *alive_get_conflicts( char *server, int port, 
                                                char *name);
void alive_free_detailed( struct alive_detailed_ioc *ioc);


struct alive_ioc_event_db *alive_get_ioc_event_db( char *server, int port,
                                             char *name);
void alive_free_ioc_event_db( struct alive_ioc_event_db *events);

#endif
