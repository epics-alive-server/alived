/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/




#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config_parse.h"


#ifndef CFG_FILE
  #define CFG_FILE ""
#endif

static char *config_file = CFG_FILE;

// -1: error, 0: empty, 1: successful
static int string_parse( char *str, char **token1, char **token2)
{
  char *p;

  char *tt1, *tt2;
  
  // process comments
  p = str;
  while( *p != '\0')
    {
      if( *p == '#')
        {
          *p = '\0';
          break;
        }
      p++;
    }

  tt1 = str;
  while( isspace(*tt1) )
    tt1++;
  if( *tt1 == '\0') 
    return 0;
  p = tt1;
  while( isalnum(*p) || (*p == '_') )
    p++;
  if( !isspace(*p))
    {
      printf("Configuration file error in \"%s\" :\n"
             "Bad option specified.\n", config_file);
      return -1;
    }
  *p = '\0';
  tt2 = p + 1;
  while( isspace(*tt2) )
    tt2++;
  if( *tt2 == '\0')
    {
      printf("Configuration file error in \"%s\" :\n"
             "Missing value for \"%s\".\n", config_file, tt1);
      return -1;
    }
  if(*tt2 == '\"')
    {
      tt2++;
      p = strchr( tt2, '\"');
      if( p == NULL)
        {
          printf("Configuration file error in \"%s\" :\n"
                 "Bad value for \"%s\".\n", config_file, tt1);
          return -1;
        }
      *p = '\0';
    }
  else
    {
      p = tt2;
      while(!isspace(*p) && (*p != '\0'))
        p++;
      *p = '\0';
    }

  *token1 = tt1;
  *token2 = tt2;

  return 1;
}


int config_find(char *key, char **value)
{
  FILE *fptr;
  char buffer[1024];
  char *token1, *token2;

  int ret;
  

  if( (fptr = fopen( config_file, "r") ) == NULL)
    {
      printf("Can't open configuration file \"%s\".\n", config_file);
      return 1;
    }

  *value = NULL;
  
  while( fgets( buffer, 1024, fptr) )
    {
      ret = string_parse( buffer, &token1, &token2);
      if( ret == -1)
        return 1;
      if( (ret == 1) && !strcasecmp( token1, key) )
        {
          *value = strdup(token2);
          break;
        }
    }

  fclose( fptr);

  return 0;
}


void config_free_dictionary( struct config_dictionary *dict)
{
  int i;

  for( i = 0; i < dict->count; i++)
    {
      free( dict->key[i]);
      free( dict->value[i]);
    }
  free( dict->key);
  free( dict->value);
  free( dict->filename);
  free( dict);
}

struct config_dictionary *config_get_dictionary( char *filename)
{
  struct config_dictionary *dict;

  FILE *fptr;
  char buffer[1024];
  char *token1, *token2;

  int ret;

  int lines, cnt;

  if( filename == NULL)
    filename = config_file;
  
  if( (fptr = fopen( filename, "r") ) == NULL)
    {
      printf("Can't open configuration file \"%s\".\n", config_file);
      return NULL;
    }

  lines = 0;
  while( fgets( buffer, 1024, fptr) )
    lines++;
  dict = malloc( sizeof( struct config_dictionary )); 
  if( dict == NULL)
    {
      printf("Can't allocate memory.\n");
      return NULL;
    }
  dict->key = malloc( lines * sizeof( char *) );
  dict->value = malloc( lines * sizeof( char *) );
  dict->filename = strdup( filename);
  if( (dict->key == NULL) || (dict->value == NULL) || (dict->filename == NULL) )
    {
      config_free_dictionary( dict);
      printf("Can't allocate memory.\n");
      return NULL;
    }

  rewind( fptr);
  dict->count = 0;
  while( fgets( buffer, 1024, fptr) )
    {
      ret = string_parse( buffer, &token1, &token2);
      if( ret == -1)
        {
          config_free_dictionary( dict);
          return NULL;
        }
      if( ret == 1)
        {
          cnt = dict->count;
          dict->count++;
          dict->key[cnt] = strdup(token1);
          dict->value[cnt] = strdup(token2);
          if( (dict->key[cnt] == NULL) || (dict->value[cnt] == NULL) )
            {
              config_free_dictionary( dict);
              printf("Can't allocate memory.\n");
              return NULL;
            }
        }
    }

  fclose( fptr);

  return dict;
}
    
/* int dictionary_find( struct config_dictionary *dict, char *key, char **value) */
/* { */
/*   int l; */

/*   for( l = 0; l < dict->count; l++) */
/*     { */
/*       if( !strcasecmp( key, dict->key[l]) ) */
/*         { */
/*           *value = dict->value[l]; */
/*           return 1; */
/*         } */
/*     } */

/*   return 0; */
/* } */
