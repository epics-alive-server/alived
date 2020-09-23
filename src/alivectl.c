/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

//#include <netdb.h>
//#include <pthread.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>
#include <errno.h>

//#include <time.h>
#include <fcntl.h>

#include "alived.h"
#include "config_parse.h"

char *control_socket = NULL;


int send_message( char *message, int length)
{
  int sockfd, len;
  struct sockaddr_un u_addr;

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) 
    {
      perror("socket");
      exit(1);
    }

  u_addr.sun_family = AF_UNIX;
  strcpy(u_addr.sun_path, control_socket);
  len = strlen(u_addr.sun_path) + sizeof(u_addr.sun_family);
  if (connect(sockfd, (struct sockaddr *)&u_addr, len) == -1) 
    {
      perror("connect");
      exit(1);
    }

  if(send(sockfd, message, length, 0) == -1) 
    {
      perror("send");
      exit(1);
    }
      
  // shutdown( sockfd, SHUT_RDWR);
  close(sockfd);

  return 0;
}



int ping_socket( void)
{
  int sockfd, len;
  struct sockaddr_un u_addr;

  char pingstr[] = "ping";

  int t, cnt;
  char str[16];

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) 
    {
      perror("socket");
      exit(1);
    }

  u_addr.sun_family = AF_UNIX;
  strcpy(u_addr.sun_path, control_socket);
  len = strlen(u_addr.sun_path) + sizeof(u_addr.sun_family);
  if (connect(sockfd, (struct sockaddr *)&u_addr, len) == -1) 
    {
      if( errno == ECONNREFUSED)
        {
          return 1;
        }
      else
        {
          perror("connect");
          exit(1);
        }
    }

  if(send(sockfd, pingstr, 5, 0) == -1) 
    {
      perror("send");
      exit(1);
    }

  shutdown( sockfd, SHUT_WR);
      
  cnt = 0;
  while( ((t = recv(sockfd, &str[cnt], 15-cnt, 0)) > 0) && (cnt < 15))
    {
      cnt += t;
      str[cnt] = '\0';
    }
  if (t < 0) 
    {
      perror("recv");
      exit(1);
    }

  shutdown( sockfd, SHUT_RD);
  close(sockfd);

  // don't know who is here if it doesn't repond "hello"
  if( strcmp( str, "hello\n"))
    exit(1);

  return 0;
}


int send_receive_message( char *message, int length)
{
  int sockfd, len;
  struct sockaddr_un u_addr;

  int t;
  char str[1024];

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) 
    {
      perror("socket");
      exit(1);
    }

  u_addr.sun_family = AF_UNIX;
  strcpy(u_addr.sun_path, control_socket);
  len = strlen(u_addr.sun_path) + sizeof(u_addr.sun_family);
  if (connect(sockfd, (struct sockaddr *)&u_addr, len) == -1) 
    {
      perror("connect");
      exit(1);
    }

  if(send(sockfd, message, length, 0) == -1) 
    {
      perror("send");
      exit(1);
    }

  shutdown( sockfd, SHUT_WR);
      
  while( (t=recv(sockfd, str, 1023, 0)) > 0)
    {
      str[t] = '\0';
      fputs( str, stdout);
    }

  if (t < 0) 
    {
      perror("recv");
      exit(1);
    }

  shutdown( sockfd, SHUT_RD);
  close(sockfd);

  return 0;
}


void helper(void)
{
  printf("alivectl (-v|-q|-p|-s|-c|-l|-e|-i <ioc>|-d <ioc>|-a <prefix>) [<socket>]\n"
         "  The socket can be specified, else a default value will be used.\n"
         "    -v  prints the version of the alive daemon and tools\n"
         "    -q  stops current alived\n"
         "    -p  pings the daemon to make sure it is running\n"
         "    -s  prints current status\n"
         "    -c  prints configuration information\n"
         "    -l  prints list of IOCs\n"
         "    -e  prints list of event subscription clients\n"
         "    -i  prints information about IOC\n"
         "    -d  deletes ioc specified from database\n"
         "    -a  archives the database into CSV files, using prefix for file names\n"
         );
  
}

#define BUFSIZE (4096)

int path_expand( char *buffer, int buffer_length, char *filepath)
{
  if( filepath[0] != '/')
    {
      if( getcwd( buffer, buffer_length - strlen(filepath) - 1) != NULL)
        {
          strcat(buffer, "/");
          strcat(buffer, filepath);
        }
      else
        return 1;
    }
  else
    {
      if( strlen(filepath) >= buffer_length)
        return 1;
      else
        strcpy( buffer, filepath);
    }
  return 0;
}




int main( int argc, char *argv[])
{
  int opt;
  char buffer[BUFSIZE];
  char *arg;
  int cnt;


  enum command { None, Help, Version, Quit, Ping, List, Subscribers, Info,
                 Stats, Delete, Archive, Configuration, TreeDump,
                 Experimental };
  enum command cmd = None;

  arg = NULL;
  while((opt = getopt( argc, argv, "hvcqpsx:lei:d:a:t:")) != -1)
    {
      // only one command switch can be specified
      if( cmd != None)
        {
          printf("Error: Only one command can be specified at a time.\n");
          return 1;
        }

      switch(opt)
        {
        case 'h':
          cmd = Help;
          break;
        case 'v':
          cmd = Version;
          break;
        case 'q':
          cmd = Quit;
          break;
        case 'p':
          cmd = Ping;
          break;
        case 'l':
          cmd = List;
          break;
        case 'e':
          cmd = Subscribers;
          break;
        case 'i':
          cmd = Info;
          arg = strdup(optarg);
          break;
        case 'd':
          cmd = Delete;
          arg = strdup(optarg);
          break;
        case 's':
          cmd = Stats;
          break;
        case 'a':
          cmd = Archive;
          arg = strdup(optarg);
          break;
        case 'c':
          cmd = Configuration;
          break;
        case 't':
          cmd = TreeDump;
          arg = strdup(optarg);
          break;
        case 'x':
          cmd = Experimental;
          arg = strdup(optarg);
          break;
        case ':':
        case '?':
          return 2;
          break;
        }
    }

  if( (argc - optind) > 1)
    {
      printf("Error: Only single control port argument allowed as "
             "non-command.\n");
      return 3;
    }
  if( (argc - optind) == 1)
    {
      control_socket = strdup( argv[optind] );
      if( control_socket == NULL)
        {
          printf("Error: Out of memory.\n");
          return 4;
        }
    }
  else
    {
      if( config_find( "control_socket", &control_socket) )
        return 5;
    }


  switch( cmd)
    {
    case None:
    case Help:
      helper();
      break;
    case Version:
      send_receive_message( "version", 8); 
      break;
    case Quit:
      send_receive_message( "quit", 5); 
      break;
    case Ping:
      if( ping_socket() )
        fputs( "down\n", stdout);
      else
        fputs( "up\n", stdout);
      break;
    case Stats:
      send_receive_message( "stats", 6); 
      break;
    case List:
      send_receive_message( "list", 5); 
      break;
    case Subscribers:
      send_receive_message( "subscribers", 12); 
      break;
    case Configuration:
      send_receive_message( "configuration", 14);
      break;
    case Info:
      cnt = snprintf( buffer, BUFSIZE, "info %s", arg);
      if( cnt < BUFSIZE)
        send_receive_message( buffer, cnt + 1);
      break;
    case Delete:
      cnt = snprintf( buffer, BUFSIZE, "delete %s", arg);
      if( cnt < BUFSIZE)
        send_receive_message( buffer, cnt + 1);
      break;
    case Archive:
      cnt = snprintf( buffer, BUFSIZE, "archive ");
      if( (cnt < BUFSIZE) && !path_expand( buffer + cnt, BUFSIZE - cnt, arg) )
        send_message( buffer, strlen( buffer) + 1);
      break;
    case TreeDump:
      cnt = snprintf( buffer, BUFSIZE, "tree_dump ");
      if( (cnt < BUFSIZE) && !path_expand( buffer + cnt, BUFSIZE - cnt, arg) )
        send_message( buffer, strlen( buffer) + 1);
      break;
    case Experimental:
      cnt = snprintf( buffer, BUFSIZE, "experimental %s", arg);
      if( cnt < BUFSIZE)
        send_receive_message( buffer, cnt + 1);
      break;
    }

  return 0;
}









