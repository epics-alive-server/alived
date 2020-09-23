/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef IOCDB_H
#define IOCDB_H 1

#include <stdint.h>
#include <netinet/ip.h>

#include "gentypes.h"



enum os_type { GENERIC, VXWORKS, LINUX, DARWIN, WINDOWS};

struct iocinfo_extra_vxworks
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
  char *boothost_username;
  char *boothost_password;
  uint32_t flags;
  char *target_name;
  char *startup_script;
  char *other;
};


struct iocinfo_extra_linux
{
  char *user;
  char *group;
  char *hostname;
};

struct iocinfo_extra_darwin
{
  char *user;
  char *group;
  char *hostname;
};

struct iocinfo_extra_windows
{
  char *user;
  char *machine;
};


struct iocinfo_ping
{
  struct in_addr ip_address;
  uint16_t origin_port;
  uint16_t protocol_version;
  uint32_t heartbeat;
  uint16_t period;
  uint32_t incarnation; // boot time sent by IOC
  uint32_t boottime;    // boot time, computed to daemon's perspective
  uint32_t timestamp;   // time ping received by daemon
  uint16_t reply_port;
  uint32_t user_msg;
};

struct iocinfo_env
{
  struct sharedint_struct ref;

  uint16_t count;
  char **key;
  char **value;

  uint16_t extra_type;
  void *extra;
};

enum instance_statuses { INSTANCE_UP, INSTANCE_DOWN, INSTANCE_UNTIMED_DOWN,
                         INSTANCE_MAYBE_UP, INSTANCE_MAYBE_DOWN};


struct iocinfo_data
{
  int8_t status; 

  struct iocinfo_ping ping;
  struct iocinfo_env *env;

  struct iocinfo_data *next;
};

struct iocinfo 
{
  char *ioc_name;
  int conflict_flag;

  struct iocinfo_data *data_up;
  struct iocinfo_data *data_down;
};


enum statuses { STATUS_UNKNOWN, STATUS_DOWN_UNKNOWN, STATUS_DOWN, STATUS_UP,
                STATUS_CONFLICT };

//////////////////////////


struct access_names_db_struct
{
  int number;
  char **names;
};

////

struct access_info_struct
{
  char *ioc_name;
  uint8_t overall_status;
  uint32_t time_value;
  struct iocinfo_ping ping;
  struct iocinfo_env *env; 
};

struct access_info_db_struct
{
  int number;
  struct access_info_struct *infos;
};

////

struct access_instance_struct
{
  int8_t status;
  struct iocinfo_ping ping;
  struct iocinfo_env *env;
};

struct access_detail_struct
{
  char *ioc_name;
  uint8_t overall_status;
  uint32_t time_value;

  int instance_count;
  struct access_instance_struct *instances;
};

struct access_detail_db_struct
{
  int number;
  struct access_detail_struct *details;
};

/////////////////////////////////


int iocdb_start(void);
void iocdb_stop(void);

int iocdb_missing(void);
int iocdb_number_iocs(void);

int iocdb_remove( char *ioc_name, int files_flag);

struct access_names_db_struct *iocdb_names_get(void);
void iocdb_names_release(struct access_names_db_struct *name_db);

struct access_info_db_struct *iocdb_info_get_all(void);
struct access_info_db_struct *iocdb_info_get_multi(int number,
                                                   char **ioc_names);
struct access_info_db_struct *iocdb_info_get_single( char *ioc_name);
void iocdb_info_release(struct access_info_db_struct *info_db);

struct access_detail_db_struct *iocdb_get_debug(char *ioc_name);
void iocdb_debug_release(struct access_detail_db_struct *adds);

struct access_detail_db_struct *iocdb_get_conflict(char *ioc_name);
void iocdb_conflict_release(struct access_detail_db_struct *adds);


void iocdb_debug_print_tree( char *ps_name);


#endif
