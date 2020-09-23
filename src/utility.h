/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef UTILITY_H
#define UTILITY_H 1

#include <stdio.h>

char *address_to_string( char string[16], uint32_t address );
char *time_to_string( char string[32], uint32_t time);

char *buffer_string_grab( char **string);
char *file_string_grab( int bytes, FILE *fptr );
char *net_string_grab( int bytes, int sockfd );
char **net_string_array_grab( int count_bytes, int string_bytes, int sockfd, 
                              uint16_t *count );

void file_string_write( int bytes, FILE *fptr, char *string);

int socket_reader( int sockfd, int size, char *buffer );
int socket_writer( int sockfd, void *buffer, int size);

int max( int a, int b);
char *make_file_path( char *dir, char *ioc_name);

uint32_t timediff_msec( struct timeval earlier, struct timeval later);


#endif
