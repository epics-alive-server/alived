/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/


//#include "logging.h"


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>

///////////////////////////////////

char *time_to_string( char string[32], uint32_t time)
{
  time_t now;

  now = time;
  strftime( string, 31, "%Y-%m-%d %H:%M:%S", localtime( &now));
  string[31] = '\0'; // just in case

  return string;
}

char *address_to_string( char string[16], uint32_t address)
{
  unsigned char *raw_addr;

  raw_addr = (unsigned char *) &address;
  snprintf( string, 16, "%d.%d.%d.%d", raw_addr[0], raw_addr[1], raw_addr[2],
           raw_addr[3]);
  string[15] = '\0';
  return string;
}

////////////////////

char *buffer_string_grab( char **string)
{
  uint8_t len8;
  char *s;

  len8 = *((uint8_t *) (*string) );
  s = strndup( (*string) + 1, len8);
  *string += (len8 + 1);
  return s;
}


// bytes: 1 or 2 (doesn't check!)
char *file_string_grab( int bytes, FILE *fptr )
{
  uint8_t o8;
  uint16_t o16;

  int len;
  char *str;

  if( bytes == 1)
    {
      if( fread( &o8, sizeof( uint8_t), 1, fptr) != 1)
        return NULL;
      len = o8;
    }
  else
    {
      if( fread( &o16, sizeof( uint16_t), 1, fptr) != 1)
        return NULL;
      len = o16;
    }
  str = malloc( sizeof( char) * (len + 1) );
  if( fread( str, sizeof( char), len, fptr) != len)
    {
      free( str);
      return NULL;
    }
  str[len] = '\0';

  return str;
}


// ultra-paranoid reader, with 5 second timeout
// It's set up so that if the data is there, just grab it and go, but
// if not, drop into select where I can control the timeout.
int socket_reader( int sockfd, int size, char *buffer )
{
  int len; 

  // first part just lets it read easily if it can
  len = read( sockfd, buffer, size);
  if( len < 0)
    {
      // using EAGAIN as the socket is set to NON-BLOCKING!
      //      if(errno == EINTR)
      if( errno == EAGAIN)
        len = 0;
      else
        return 1;
    }
  if( len == size)
    return 0;

  while(1)
    {
      int addlen;

      struct timeval tv;
      fd_set rset;
      int retval; 

      FD_ZERO( &rset); 
      FD_SET( sockfd, &rset);
      // can change timer to change granularity
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      retval = select( sockfd + 1, &rset, NULL, NULL, &tv);
      if( retval == 0) // timeout
        return 1;
      if( retval < 0)
        {
          //          if(errno == EINTR)
          if(errno == EAGAIN)
            continue;
          else
            return 1;
        }

      if( FD_ISSET( sockfd, &rset) ) // this should always be the case
        {
          addlen = read( sockfd, buffer + len, size - len);
          if( !addlen)  // socket died
            return 1;
          if( addlen < 0)
            {
              //              if(errno == EINTR)
              if(errno == EAGAIN)
                continue;
              else
                return 1;
            }
          len += addlen;
          if( len == size)
            break;
        }

    }

  return 0;
}

// bytes: 1 or 2 (doesn't enforce!)
char *net_string_grab( int bytes, int sockfd )
{
  int len;
  char buffer[2];
  char *p;
  char *str;

  if( socket_reader( sockfd, bytes, buffer) )
    return NULL;

  if( bytes == 1)
    len = *((uint8_t *) buffer);
  else
    {
      p = buffer;
      len = ntohs( *((uint16_t *) p) );
    }

  str = malloc( sizeof( char) * (len + 1) );
  if( socket_reader( sockfd, len, str) )
    {
      free( str);
      return NULL;
    }
  str[len] = '\0';

  return str;
}

// bytes: 1 or 2 (doesn't enforce!)
char **net_string_array_grab( int count_bytes, int string_bytes, int sockfd, 
                              int *count )
{
  char **array;

  int len;
  char buffer[2];
  char *p;
  int i, j;

  *count = 0;

  if( socket_reader( sockfd, count_bytes, buffer) )
    return NULL;
  
  if( count_bytes == 1)
    len = *((uint8_t *) buffer);
  else
    {
      p = buffer;
      len = ntohs( *((uint16_t *) p) );
    }
  
  array = malloc( len * sizeof( char *));
  if( array == NULL)
    return NULL;
  for( i = 0; i < len; i++)
    {
      array[i] = net_string_grab( string_bytes, sockfd);
      if( array[i] == NULL)
        {
          for( j = 0; j < i; j++)
            free( array[j]);
          free(array);
          return NULL;
        }
    }

  *count = len;

  return array;
}


///////////////////////////////////////////////////


void file_string_write( int bytes, FILE *fptr, char *string)
{
  uint8_t o8;
  uint16_t o16, len;

  if( string == NULL)
    return;
  len = strlen( string);

  if( bytes == 1)
    {
      o8 = len;
      fwrite( &o8, sizeof(o8), 1, fptr);
    }
  else
    {
      o16 = len;
      fwrite( &o16, sizeof(o16), 1, fptr);
    }
  if( len)
    fwrite( string, sizeof(char), len, fptr);
}


int socket_writer( int sockfd, void *buffer, int size)
{
  int len; 

  // first part just lets it write easily if it can
  len = write( sockfd, buffer, size);
  if( len < 0)
    {
      //      if(errno == EINTR)
      if(errno == EAGAIN)
        len = 0;
      else
        return 1;
    }
  if( len == size)
    return 0;

  while(1)
    {
      int addlen;

      struct timeval tv;
      fd_set rset;
      int retval; 

      FD_ZERO( &rset); 
      FD_SET( sockfd, &rset);
      // can change timer to change granularity
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      retval = select( sockfd + 1, NULL, &rset, NULL, &tv);
      if( retval == 0) // timeout
        return 1;
      if( retval < 0)
        {
          //          if(errno == EINTR)
          if(errno == EAGAIN)
            continue;
          else
            return 1;
        }

      if( FD_ISSET( sockfd, &rset) ) // this should always be the case
        {
          addlen = write( sockfd, buffer + len, size - len);
          if( !addlen)
            return 1;
          if( addlen < 0)
            {
              //              if(errno == EINTR)
              if(errno == EAGAIN)
                continue;
              else
                return 1;
            }
          len += addlen;
          if( len == size)
            break;
        }
    }

  return 0;
}



////////////////////

int max( int a, int b)
{
  if( a > b)
    return a;
  else
    return b;
}


char *make_file_path( char *dir, char *ioc_name)
{
  char *filename;
  char *p;
  
  filename = malloc( (strlen(dir) + strlen( ioc_name) + 2) * sizeof(char) );

  p = stpcpy( filename, dir);
  p = stpcpy( p, "/");
  strcpy( p, ioc_name);

  return filename;
}

////////////////////////

// struct timeval now;
// gettimeofday( &now, NULL);

uint32_t timediff_msec( struct timeval earlier, struct timeval later)
{
  return (later.tv_sec - earlier.tv_sec)*1000 +
    (later.tv_usec - earlier.tv_usec)/1000 ;
}

