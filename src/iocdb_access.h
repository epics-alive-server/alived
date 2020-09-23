/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef IOCDB_ACCESS_H
#define IOCDB_ACCESS_H 1


void iocdb_make_netbuffer_env( struct netbuffer_struct *nbuff,
                               struct iocinfo_env *env);

void iocdb_socket_send_all( int socket);
void iocdb_socket_send_multi( int socket, int number, char **ioc_names);
void iocdb_socket_send_single( int socket, char *ioc_name);


void iocdb_socket_send_control_list( int socket);
int iocdb_socket_send_control_ioc( int socket, char *ioc_name);

void iocdb_socket_send_debug( int socket, char *ioc_name);
void iocdb_socket_send_conflicts( int socket, char *ioc_name);

void iocdb_snapshot_tree( char *prefix);

#endif
