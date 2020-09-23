/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



// for qsort_r
#define _GNU_SOURCE
#include <stdlib.h>


#include <string.h>
#include <stdio.h>

#include <netinet/in.h>

#include "llrb_db.h"


//#define TEST


/////////////////////

/* 
   This is a tree-based memory-resident database. It's set up as a
   Left-Leaning Red-Black tree for autobalancing purposes.  The entire
   database supports multiple-reader, single writer locking
   (preferring the writer), and there is entry locking as well.

   There can't be multiple entries for the same key (that can be
   accomplished in the values with a linked list or something similar).

   All access to the database is through callbacks, to ensure that access
   is quick and to remove the need to manually do lock handling.

   The top element is simply a pointer holder, is always present,
   and only 'left' is used in the code. It should be used for nothing else.
*/


// used for making a linked list of keys of nodes to delete
struct key_link
{
  void *key;
  struct key_link *next;
};




void db_ps( struct tree_db *db, char *filename)
{
  FILE *fptr;

  void tree_print_branch( struct tree_node *node)
  {
    if( node == NULL)
      {
        return;
      }

    // this assumes key is a char*!!!!  should add helper function
    fprintf(fptr, "(%s %c)[", (char *) node->key, node->color == 0 ? 'B' : 'R');
    tree_print_branch( node->left);
    fprintf(fptr, "][");
    tree_print_branch( node->right);
    fprintf(fptr, "]");
  }

  if( (fptr = fopen(filename, "w")) == NULL)
    return;

  fputs(
        "%!\n"
        "/inch {72 mul} def\n"
        "/adv 0.9 inch def\n"
        "/span 10.5 inch def\n"
        "/btree "
        , fptr);

  worm_lock_reader(&(db->worm_mutex));

  fprintf( fptr, "[");
  tree_print_branch( db->tree->left);
  fprintf( fptr, "]");

  worm_unlock_reader(&(db->worm_mutex));

  fputs(
        " def\n"
        "/Helvetica findfont 8 scalefont setfont\n"
        "0.5 inch 5.5 inch translate\n"
        "/printtree { 3 dict begin /lvl exch def /lspan "
        "span lvl {2 div} repeat def\n"
        "/ladv adv lvl mul def\n"
        "aload length 0 gt\n"
        "{ gsave\n"
        "currentpoint exch pop ladv exch lspan 2 div add moveto "
        "lvl 1 add printtree\n"
        "grestore gsave\n"
        "currentpoint exch pop ladv exch lspan 2 div sub moveto "
        "lvl 1 add printtree\n"
        "grestore show } if\n"
        "end } def\n"
        "0 0 moveto\n"
        "btree 1 printtree\n"
        "showpage\n"
        , fptr);

  fclose(fptr);

}
/////


struct tree_node *tree_create( void)
{
  struct tree_node *idb;

  idb = calloc( 1, sizeof( struct tree_node) );
  if( idb == NULL)
    return NULL;

  //idb->key = NULL;
  //idb->values = NULL;
  idb->color = -1;
  idb->left = NULL;  // <---- only this is used!
  idb->right = NULL;

  return idb;
}

static void tree_destroy( struct tree_db *db, 
                          void (* values_func)( void *, void *), void *arg )
{
  void tree_destroy_helper( struct tree_node *node)
  {
    if( node == NULL)
      return;
    tree_destroy_helper( node->left);
    tree_destroy_helper( node->right);

    db->key_release( node->key);
    if( values_func != NULL)
      values_func( (void *) node->values, arg);
    free( node);
  }

  tree_destroy_helper( db->tree->left);
      
  free( db->tree);
}


void node_color_flip(struct tree_node *n)
{
  n->left->color = 1 - n->left->color;
  n->right->color = 1 - n->right->color;
  n->color = 1 - n->color;
}

struct tree_node *node_rotate_right(struct tree_node *n)
{
  struct tree_node *ntemp;

  ntemp = n->left;
  n->left = ntemp->right;
  ntemp->right = n;
  ntemp->color = n->color;
  n->color = RED;
  return ntemp;
}

struct tree_node *node_rotate_left(struct tree_node *n)
{
  struct tree_node *ntemp;

  ntemp = n->right;
  n->right = ntemp->left;
  ntemp->left = n;
  ntemp->color = n->color;
  n->color = RED;
  return ntemp;
}


int node_is_red(struct tree_node *n)
{
  return ( (n != NULL) && (n->color == RED) );
}

struct tree_node *node_fix_up( struct tree_node *n)
{
  if( node_is_red( n->right) )
    n = node_rotate_left(n);

  if( node_is_red( n->left) && node_is_red( n->left->left) )
    n = node_rotate_right(n);

  if( node_is_red( n->left) && node_is_red( n->right) )
    node_color_flip( n);

  return n;
}



// returns 1 on success
static int tree_find( struct tree_db *db, struct tree_node *node, void *key, 
                      void (* func)( void *, void *), void *arg )
{
  int comp;

  while( node != NULL)
    {
      comp = db->key_compare( key, node->key);
      if( !comp )
        {
          if(db->record_lock_flag)
            pthread_mutex_lock(node->rec_mutex);
          if( func != NULL)
            func( node->values, arg);
          if(db->record_lock_flag)
            pthread_mutex_unlock(node->rec_mutex);
          return 1;
        }
      if( comp < 0)
        node = node->left;
      else
        node = node->right;
    }

  return 0;
}


static struct tree_node *tree_add_recursive
( struct tree_db *db, struct tree_node *node, void *key, char *new_flag, 
  void *(* new_func)( void *), void *(* existing_func)( void *, void *), 
  void *arg)
{
  struct tree_node *nptr;

  int comp; 

  if( node == NULL)
    {
      // create entry

      nptr = calloc( 1, sizeof( struct tree_node) );
      nptr->key = db->key_copy( key);
      nptr->color = RED;
      nptr->left = NULL;
      nptr->right = NULL;

      *new_flag = 1;

      if(!db->record_lock_flag)
        nptr->rec_mutex = NULL;
      else
        {
          nptr->rec_mutex = malloc( sizeof(pthread_mutex_t));

          // locking not REALLY needed here, as it's not part of DB yet
          pthread_mutex_init(nptr->rec_mutex, NULL);
          pthread_mutex_lock(nptr->rec_mutex);
        }
      
      nptr->values = new_func( arg);

      if(db->record_lock_flag)
        pthread_mutex_unlock(nptr->rec_mutex);

      return nptr;
    }
  
  comp = db->key_compare( key, node->key);
  if( !comp )
    {
      void *r;
      
      if( existing_func == NULL)
        return node;
      // find should have eliminated this happening, 
      // but it could due to race condition
      if(db->record_lock_flag)
        pthread_mutex_lock(node->rec_mutex);

      // allow for reallocating the data, which shows up by non-NULL return
      r = existing_func( node->values, arg);
      if( r != NULL)
        node->values = r;
      
      if(db->record_lock_flag)
        pthread_mutex_unlock(node->rec_mutex);
      return node;
    }

  nptr = tree_add_recursive( db, comp < 0 ? node->left : node->right, key, 
                             new_flag, new_func, existing_func, arg );
  if( nptr == NULL)
    return NULL;
  if( comp < 0)
    node->left = nptr;
  else
    node->right = nptr;

  return node_fix_up( node);
}


static void tree_multi_find( struct tree_db *db, struct tree_node *node, 
                             int number, void **keys, 
                             void (* func)( void *, void *), void *arg )
{
  int index;
  int match;

  // if you only have one, just use find now
  if( number == 1)
    {
      tree_find( db, node, *keys, func, arg);
      return;
    }

  match = 0;
  // divide list in two, send each piece in direction that makes sense
  for( index = 0; index < number; index++)
    {
      int comp;

      comp = db->key_compare( keys[index], node->key);
      if( comp > 0)
        break;
      if( comp == 0)
        {
          match = 1;
          break;
        }
    }
  
  if( (index > 0) && (node->left != NULL) )
    tree_multi_find( db, node->left, index, keys, func, arg);
  if( match)  // duplicates will disappear at this point
    {
      if(db->record_lock_flag)
        pthread_mutex_lock(node->rec_mutex);
      func( node->values, arg);
      if(db->record_lock_flag)
        pthread_mutex_unlock(node->rec_mutex);
      index++;
    }  
  if( (index < number) && (node->right != NULL) )
    tree_multi_find( db, node->right, number - index, &(keys[index]),
                     func, arg);
}


static void tree_walk( struct tree_node *node, void (* func)( void *, void *),
                       void *arg, int record_lock_flag)
{
  if( node->left != NULL)
    tree_walk( node->left, func, arg, record_lock_flag);
    
  if(record_lock_flag)
    pthread_mutex_lock(node->rec_mutex);
  func( node->values, arg);
  if(record_lock_flag)
    pthread_mutex_unlock(node->rec_mutex);
    
  if( node->right != NULL)
    tree_walk( node->right, func, arg, record_lock_flag);
}



static void tree_walk_delete( struct tree_node *node,
                              int (* func)( void *, void *),
                              void *arg, int record_lock_flag,
                              struct key_link **deleted_list)
{
  if( node->left != NULL)
    tree_walk_delete( node->left, func, arg, record_lock_flag, deleted_list);
    
  if(record_lock_flag)
    pthread_mutex_lock(node->rec_mutex);
  if( func( node->values, arg) )
    {
      struct key_link *new_key;

      new_key = malloc( sizeof(struct key_link) );
      // we are using the node's key, as it can't change since everything
      // is locked.  After we find the node to delete, we ignore it.
      // No need to make a copy of key.
      new_key->key = node->key;

      new_key->next = *deleted_list;
      *deleted_list = new_key;
    }
  if(record_lock_flag)
    pthread_mutex_unlock(node->rec_mutex);
    
  if( node->right != NULL)
    tree_walk_delete( node->right, func, arg, record_lock_flag, deleted_list);
}



// func returns 1 if deleted, 0 if not deleted
static struct tree_node *tree_delete( struct tree_db *db, 
                                      struct tree_node *node, void *key, 
                                      int (* func)( void *, void *), void *arg,
                                      int *success)
{
  struct tree_node *node_move_red_left(struct tree_node *n)
  {
    node_color_flip(n);
    if( node_is_red( n->right->left) )
      {
        n->right = node_rotate_right(n->right);
        n = node_rotate_left(n);
        node_color_flip(n);
      }
    return n;
  }

  struct tree_node *node_move_red_right(struct tree_node *n)
  {
    node_color_flip(n);
    if( node_is_red( n->left->left) )
      {
        n = node_rotate_right(n);
        node_color_flip(n);
      }
    return n;
  }

  struct tree_node *node_grab_delete_min(struct tree_node *n, void **key,
                                         void **values)
  {
    if( n->left == NULL)
      {
        // grab information
        *key = n->key;
        *values = n->values;

        if( n->rec_mutex != NULL)
          {
            pthread_mutex_destroy( n->rec_mutex);
            free( n->rec_mutex);
          }
        free(n);
        return NULL;
      }
    if( !node_is_red(n->left) && !node_is_red(n->left->left) )
      n = node_move_red_left(n);
    n->left = node_grab_delete_min(n->left, key, values);
    return node_fix_up(n);
  }

  int result;
  
  result = db->key_compare( key, node->key);

  // this gracefully returns in case entry to delete does not exist
  if( ((result < 0) && (node->left == NULL)) || 
      ((result > 0) && (node->right == NULL)) )
    return node_fix_up(node);

  if( result < 0)
    {
      if( !node_is_red(node->left) && !node_is_red(node->left->left) )
        node = node_move_red_left(node);
      node->left = tree_delete(db, node->left, key, func, arg, success);
    }
  else
    {
      if( node_is_red(node->left)) 
        {
          node = node_rotate_right(node);
        }

      if(!db->key_compare( key, node->key) && (node->right == NULL))
        {
          // not deleted
          if( (func != NULL) && !func( (void *) node->values, arg) )  
            return node_fix_up(node);
          
          db->key_release( node->key);
          if( node->rec_mutex != NULL)
            {
              pthread_mutex_destroy( node->rec_mutex);
              free( node->rec_mutex);
            }
          free( node);

          *success = 1;

          return NULL;
        }
      if(!node_is_red(node->right) && !node_is_red(node->right->left))
        {
          node = node_move_red_right(node);
        }
      if(!db->key_compare( key, node->key))
        {
          if( (func != NULL) && !func( (void *) node->values, arg) )  // not deleted
            return node_fix_up(node);
          db->key_release( node->key);

          *success = 1;

          node->right = node_grab_delete_min(node->right, &(node->key), 
                                             &(node->values));
        }
      else 
        node->right = tree_delete(db, node->right, key, func, arg, success);
    }
  return node_fix_up(node);
}

//////////////////////////////////
// These functions below are really just to coordinate locking


// if just seeing if in db, can pass a NULL function
int db_find( struct tree_db *db, void *key, void (* func)( void *, void *), 
             void *arg)
{
  int ret;

  if( db->tree->left == NULL)
    return 0;

  worm_lock_reader(&(db->worm_mutex));
  ret = tree_find( db, db->tree->left, key, func, arg);
  worm_unlock_reader(&(db->worm_mutex));

  return ret;
}

// if just seeing if in db, can pass a NULL func function
int db_find_init( struct tree_db *db, void *key, void (* init)( void *, int), 
                  void (* func)( void *, void *), void *arg)
{
  int ret = 0;

  worm_lock_reader(&(db->worm_mutex));
  init( arg, db->number);
  if( db->tree->left != NULL)
    ret = tree_find( db, db->tree->left, key, func, arg);
  worm_unlock_reader(&(db->worm_mutex));

  return ret;
}

// returns 1 if there was an unresolvable conflict
// new function accepts new data
// existing function accepts old data and new data, to hash it out
int db_add( struct tree_db *db, void *key, void *(* new_func)( void *), 
            void *(* existing_func)( void *, void *), void *arg)
{
  struct tree_node *dbptr;
  char new_flag;

  new_flag = 0;

  worm_lock_writer(&(db->worm_mutex));
  dbptr = tree_add_recursive( db, db->tree->left, key, &new_flag, 
                              new_func, existing_func, arg);
  if( dbptr == NULL)
    {
      worm_unlock_writer(&(db->worm_mutex));
      return 1;
    }
  dbptr->color = BLACK;
  db->tree->left = dbptr;
  if( new_flag)
    db->number++;
  worm_unlock_writer(&(db->worm_mutex));

  return 0;
}



struct key_sorter_struct
{
  int (* key_sort_func)(const void *p1, const void *p2);
};

int key_sorter(const void *p1, const void *p2, void *arg)
{
  struct key_sorter_struct *kss;
  
  kss = arg;

  return kss->key_sort_func(* (void * const *) p1, * (void * const *) p2);
}


void db_multi_find( struct tree_db *db, int number, void **keys,
                    void (* func)( void *, void *), void *arg)
{
  struct key_sorter_struct kss;

  if( db->tree->left == NULL)
    return;
  
  kss.key_sort_func = db->key_compare;
  qsort_r( (void *) keys, number, sizeof( void *), key_sorter, (void *) &kss);

  worm_lock_reader(&(db->worm_mutex));
  tree_multi_find( db, db->tree->left, number, keys, func, arg );
  worm_unlock_reader(&(db->worm_mutex));
}

void db_multi_find_init( struct tree_db *db, int number, void **keys,
                         void (* init)( void *, int),
                         void (* func)( void *, void *), void *arg)
{
  struct key_sorter_struct kss;
  
  kss.key_sort_func = db->key_compare;
  qsort_r( (void *) keys, number, sizeof( void *), key_sorter, (void *) &kss);

  worm_lock_reader(&(db->worm_mutex));
  init( arg, db->number);
  if( db->tree->left != NULL)
    tree_multi_find( db, db->tree->left, number, keys, func, arg );
  worm_unlock_reader(&(db->worm_mutex));
}
 

void db_walk( struct tree_db *db, void (* func)( void *, void *), void *arg )
{
  if( db->tree->left == NULL)
    return;

  worm_lock_reader(&(db->worm_mutex));
  tree_walk( db->tree->left, func, arg, db->record_lock_flag );
  worm_unlock_reader(&(db->worm_mutex));
}


// Red-black trees are very annoying to delete from.
// Best strategy is to record what nodes to delete, then remove
// them one by one so the tree doesn't get screwed up.
// This locks down the entire database for entire process.
void db_walk_delete( struct tree_db *db, int (* func)( void *, void *),
                     void *arg )
{
  struct key_link *deleted_list = NULL, *dn;
  int success;
  
  if( db->tree->left == NULL)
    return;

  worm_lock_writer(&(db->worm_mutex));
  tree_walk_delete( db->tree->left, func, arg, db->record_lock_flag,
                    &deleted_list );
  
  while( deleted_list != NULL)
    {
      success = 0;  // this should always get changed to 1
      db->tree->left = tree_delete( db, db->tree->left,
                                    deleted_list->key, NULL, NULL, &success);
      if( success)
        db->number--;

      dn = deleted_list->next;
      free( deleted_list);
      deleted_list = dn;
    }
  
  worm_unlock_writer(&(db->worm_mutex));
}


void db_walk_init( struct tree_db *db, void (* init)( void *, int),
                   void (* func)( void *, void *), void *arg )
{
  worm_lock_reader(&(db->worm_mutex));
  init( arg, db->number);
  if( db->tree->left != NULL)
    tree_walk( db->tree->left, func, arg, db->record_lock_flag );
  worm_unlock_reader(&(db->worm_mutex));
}



int db_delete( struct tree_db *db, void *key, int (* func)( void *, void *), 
               void *arg)
{
  int success = 0;

  if( db->tree->left == NULL)
    return 0;

  worm_lock_writer(&(db->worm_mutex));
  db->tree->left = tree_delete( db, db->tree->left, key, func, arg, &success);
  if( success)
    db->number--;
  worm_unlock_writer(&(db->worm_mutex));
  
  return success;
}



struct tree_db *db_create(void *(* key_copy)( void *), 
                          void (* key_release)( void *),
                          int (* key_compare)( const void *, const void *),
                          int auto_lock_records)
{
  struct tree_db *db;

  db = calloc( 1, sizeof( struct tree_db) );
  if( db == NULL)
    return NULL;

  if( !auto_lock_records)
    db->record_lock_flag = 0;
  else
    db->record_lock_flag = 1;

  db->number = 0;
  db->key_copy = key_copy;
  db->key_release = key_release;
  db->key_compare = key_compare;
  db->tree = tree_create();
  if( db->tree == NULL)
    {
      free(db);
      return NULL;
    }

  worm_init( &(db->worm_mutex));

  return db;
}


void db_destroy(struct tree_db *db, void (* func)( void *, void *), void *arg )
{
  worm_lock_writer(&(db->worm_mutex));
  db->number = 0;
  tree_destroy( db, func, arg);
  worm_unlock_writer(&(db->worm_mutex));
  worm_clear(&(db->worm_mutex));
  free( db);
}


int db_count( struct tree_db *db)
{
  return db->number;
}


