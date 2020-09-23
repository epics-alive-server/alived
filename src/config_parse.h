/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef CONFIG_PARSE_H
#define CONFIG_PARSE_H 1



struct config_dictionary
{
  char *filename;
  int count;
  char **key;
  char **value;
};

struct config_dictionary *config_get_dictionary( char *filename);
void config_free_dictionary( struct config_dictionary *dict);
int dictionary_find( struct config_dictionary *dict, char *key, char **value);
int config_find(char *key, char **value);


#endif

