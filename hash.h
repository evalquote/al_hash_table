/* hash table libraries */
/* y.amagai 2014.5 */

#ifndef ALH_H
#define ALH_H
#include <inttypes.h>

/* NOT thread safe */

/* type of hash entry value */
typedef  long value_t;

/* type of hash table */
struct al_hash_t;

/* type of iterator pointed to hash table */
struct al_hash_iter_t;

/*
  statistics

  al_hash_bit:    bit size of main hash table
                  hash table size is  (1 << al_hash_bit)
  al_n_items:     number of entries of main hash table
  al_n_items_old: number of entries of previous small hash table
                  entries of small hash table will moved to main hash table
  al_n_rehashing: number of rehashing
  al_n_cancel_rehashing: number of cancelling of moving entries between hash table
                         iterators are attached to the hash table
 */
struct al_hash_stat_t {
  unsigned int  al_hash_bit;
  unsigned long al_n_items;
  unsigned long al_n_items_old;
  unsigned long al_n_cancel_rehashing;
  unsigned int  al_n_rehashing;
};

/*
  statistics
  histgram of chain length of main and previous small hash table
 if chain length is over 10, count up [10] of returned array
 */
typedef unsigned long al_chain_lenght_t[11];

/* default bit size of hash table */
#define DEFAULT_HASH_BIT 15

/*
 * User API
 *
 * return 0  on success
 *
 */

/*
 * create hash table
 * bit == 0, use DEFAULT_HASH_BIT
 * return -2, cannot alloc memories
 * return -3, htp is NULL
 */
int al_init_hash(int bit, struct al_hash_t **htp);

/*
 * destroy hash table
 *   ht will be free()
 * return -3, ht is NULL
 *
 * iterators attached to the hash table will be invalid
 *   (you must al_hash_iter_end() on the iterators)
 */
int al_free_hash(struct al_hash_t *ht);

/*
 * get statistics
 * return -3, statp is NULL
 *
 * if acl is NULL, no chain statistics returned
 *   (counting chain length needs some CPU resources)
 */
int al_hash_stat(struct al_hash_t *ht, 
		 struct al_hash_stat_t *statp, 
		 al_chain_lenght_t acl);

/*
 * set key and value to hash table
 * return -2, cannot alloc memories
 * return -3, ht is NULL
 *
 * if key is not in hash table, add it
 * else replace value field by v
 * if pv is not NULL, return previous value
 */
int item_set(struct al_hash_t *ht, char *key, value_t v);
int item_set_pv(struct al_hash_t *ht, char *key, value_t v, value_t *pv);

/*
 * find key on the hash table
 * return -1, key is not found
 * return -3, ht or  key is NULL
 * return -5, iterators are attached on the hash table
 *            (only item_delete and item_delete_pv)
 * 
 * if pointer for return value (ret-v, ret_pv) is NULL, ignore it
 * replace:
 *  if key is found, replace value field by v, else return -1
 */
int item_key(struct al_hash_t *ht, char *key);
int item_get(struct al_hash_t *ht, char *key, value_t *ret_v);
int item_replace(struct al_hash_t *ht, char *key, value_t v);
int item_replace_pv(struct al_hash_t *ht, char *key, value_t v, value_t *ret_pv);
int item_delete(struct al_hash_t *ht, char *key);
int item_delete_pv(struct al_hash_t *ht, char *key, value_t *ret_pv);

/*
 * increment value field by off
 * item_inc():
 *   if key is found, then increment value field and
 *     set incremented value to *ret_v, and return 0
 *   if key if not found, then return -1. *ret_v is not changed.
 *  
 * item_inc_init():
 *   if key is found, then increment value field and
 *     set incremented value to *ret_v, and return 0
 *   if key if not found, add key with (valut_)off
 *     *ret_v is not changed. 
 *        return 0 on successfully key adding, 
 *        return -2, cannot alloc memories
 */
int item_inc(struct al_hash_t *ht, char *key, long off, value_t *ret_v);
int item_inc_init(struct al_hash_t *ht, char *key, long off, value_t *ret_v);

/* iterators */

/*
 * create iterator attached to ht
 *   after first al_hash_iter() call, the iterator points entries
 *    (when hash table is not empty)
 * return -2, cannot alloc memories
 * return -3, ht or iterp is NULL
 */
int al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp);

/*
 * advance iterator
 *   key appears arbitary order
 * return -1, cannot advance, reached end
 * return -3, iterp or key is NULL
 * return -4, hash table is destroyed
 *
 * do not modify the string pointed by `key'
 */
int al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v);

/*
 * destroy iterator
 * iterp will free() ed, so do not use it any more
 * return -3, iterp is NULL
 */
int al_hash_iter_end(struct al_hash_iter_t *iterp);

/*
 * replace value field pointed by iterator
 *   call al_hash_iter() in advance
 * return -1, not pointed (just created or pointed item is deleted)
 * return -3, iterp or key is NULL
 * return -4, hash table is destroyed
 */
int item_replace_iter(struct al_hash_iter_t *iterp, value_t v);

/*
 * delete key/value item pointed by iterator
 *   call al_hash_iter() in advance
 *
 * return -1, not pointed (just created or pointed item is deleted)
 * return -3, iterp or key is NULL
 * return -4, hash table is destroyed
 */
int item_delete_iter(struct al_hash_iter_t *iterp);


/*
 * Caution:
 *
 * item_set(), item_set_pv(), item_inc_init():
 *   new key/value entry is inserted to a hash table attached
 *   with some iterators, iterators may or not may point the
 *   new entry on subsequent al_hash_iter() call.
 *
 * item_detete(), item_delete_pv():
 *   can not delete entries of a hash table attached some iterators
 *   by item_detete() or item_delete_pv().
 *   item_detete() or item_delete_pv() is return with -5 immediately
 *   (either key is found, or not found).
 *
 * item_delete_iter():
 *   `key' pointer returned from al_hash_iter() is valid until 
 *   next al_hash_iter() call. 
 *
 *   ex.
 *     const char *ikey;
 *     while (!(ret = al_hash_iter(itr, &ikey, &v))) {
 *       if (v < 100) {
 *          printf("%s will be deleted\n", ikey);
 *          item_delete_iter(itr);
 *          printf("%s is deleted\n", ikey); // here, ikey is still valid
 *       }
 *     }
 *
 *   if iterators pointed to same hash table invoke item_delete_iter()
 *   concurrently, result is undefined, may cause crash.
 *
 *   item_delete_iter() result is undefined, my cause crash,
 *   immediately after new key/value (by item_set(), etc) is inserted to
 *   the pointed hash table, without advancing iterator.
 *   ex:
 *   bad: al_hash_iter() -> item_set() -> item_delete_iter() -> al_hash_iter()
 *   ok:  al_hash_iter() -> item_set() -> al_hash_iter() -> item_delete_iter()
 *   ok:  al_hash_iter() -> item_delete_iter() -> item_set() -> al_hash_iter()
 *
 * iterators will be invalid when pointd hash table is destroyed by
 * al_free_hash().  you must call al_hash_iter_end() on the iterators to
 * free memory resources.
 *
 */

#endif
