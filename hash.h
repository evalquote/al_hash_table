/* hash table libraries */
/* 
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 */

/* NOT thread safe */

#ifndef ALH_H
#define ALH_H
#include <inttypes.h>

/* type of hash entry scalar value */
typedef long value_t;

/* type of hash entry link value */
typedef const char * link_value_t;

#ifdef AL_HASH_O
void al_free_linked_value(link_value_t v) { if (v) free((void *)v); }
// void al_free_linked_value(link_value_t v) { ; }

int al_link_value(link_value_t v, link_value_t *vp) {
  link_value_t cp = strdup(v);
  if (cp) {
    *vp = cp;
    return 0;
  } else {
    *vp = NULL;
    return -1;
  }
}

// int al_link_value(link_value_t v, link_value_t *vp) { *vp = v; return 0; }

int
al_link_value_cmp(link_value_t a, link_value_t b) {
  return strcmp((const char *)a, (const char *)b);
}
#endif

/* type of hash table */
struct al_hash_t;

/* type of iterator pointed to hash table */
struct al_hash_iter_t;

/* type of iterator pointed to linked hash table */
struct al_linked_value_iter_t;

/* type of iterator pointed to priority queue hash table */
struct al_pqueue_value_iter_t;

/*
  statistics

  al_hash_bit:      bit size of main hash table
                    hash table size is  (1 << al_hash_bit)
  al_n_entries:     number of entries of main hash table
  al_n_entries_old: number of entries of previous half size hash table
                    entries of small hash table will moved to main hash table
  al_n_rehashing:   number of rehashing
  al_n_cancel_rehashing: number of cancelling of moving entries between hash
                         table iterators are attached to the hash table
 */
struct al_hash_stat_t {
  unsigned int  al_hash_bit;
  unsigned int  al_n_rehashing;
  unsigned long al_n_entries;
  unsigned long al_n_entries_old;
  unsigned long al_n_cancel_rehashing;
};

/*
 *  statistics
 *   histgram of chain length of main and previous small hash table
 *   if chain length is over 10, count up [10] of returned array
 */
typedef unsigned long al_chain_length_t[11];

/* default bit size of hash table */
#define AL_DEFAULT_HASH_BIT 15

/*
 * User API
 *
 * return 0  on success
 * return -2 allocation fails
 * return -3 parameter error (provide NULL pointer)
 */

/*
 * create hash table
 * bit == 0, use AL_DEFAULT_HASH_BIT
 */
int al_init_hash(int bit, struct al_hash_t **htp);
int al_init_linked_hash(int bit, struct al_hash_t **htp);
int al_init_pqueue_hash(int bit, struct al_hash_t **htp, int sort_order, unsigned long max_n);

/*
 * destroy hash table
 *   ht will be free()
 *
 * iterators attached to the hash table will be invalid
 *   (you must al_hash_iter_end() on the iterators)
 */
int al_free_hash(struct al_hash_t *ht);

/*
 * get statistics
 *
 * if acl is NULL, no chain statistics returned
 *   (counting chain length needs some CPU resources)
 */
int al_hash_stat(struct al_hash_t *ht, 
		 struct al_hash_stat_t *statp, 
		 al_chain_length_t acl);

int al_out_hash_stat(struct al_hash_t *ht, const char *title);

/*
 * return number of attached iterators of ht  (0 <= number)
 */
int al_hash_n_iterators(struct al_hash_t *ht);

/*
 * predicate
 * return  0: yes
 * return -1: no
 */
int al_is_linked_hash(struct al_hash_t *ht);
int al_is_linked_iter(struct al_hash_iter_t *iterp);
int al_is_pqueue_hash(struct al_hash_t *ht);
int al_is_pqueue_iter(struct al_hash_iter_t *iterp);

/*
 * set key and value to hash table
 *
 * if key is not in hash table, add it
 * else replace value field by v
 * if pv is not NULL, return previous value
 */
int item_set(struct al_hash_t *ht, char *key, value_t v);
int item_set_pv(struct al_hash_t *ht, char *key, value_t v, value_t *pv);

/*
 * return -6, hash table type is not 'linked'
 */
int item_add_value(struct al_hash_t *ht, char *key, link_value_t v);

/*
 * find key on the hash table
 * return -1, key is not found
 * return -5, iterators are attached on the hash table
 *            (only item_delete and item_delete_pv)
 * 
 * if pointer for return value (ret-v, ret_pv) is NULL, ignore it
 *
 * delete:
 *   either of scalor hash table and linked hash is acceptable as parameter
 * replace:
 *   if key is found, replace value field by v, else return -1
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
 *  sort_key 0: item appears arbitary order.
 *           1: item appears dictionary order of key
 *           2: item appears counter dictionary order of key
 *        else: return -7
 * return -99, internal error
 */
int al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp, int sort_key);

/*
 * destroy iterator
 *   iterp will free(), so do not use it any more
 */
int al_hash_iter_end(struct al_hash_iter_t *iterp);

/*
 * return attached hash table.
 * return NULL:  iterp is NULL
 *               hash table is destroyed.
 */
struct al_hash_t *
al_hash_iter_ht(struct al_hash_iter_t *iterp);

/*
 * advance iterator
 * return -1, reached end
 * return -4, hash table is destroyed
 *
 * do not modify the string pointed by `key'
 */
int al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v);

/*
 * replace value field pointed by iterator
 *   call al_hash_iter() in advance
 * return -1, not pointed (just created or pointed item is deleted)
 * return -4, hash table is destroyed
 * return -6, hash table type is not 'scalar'
 */
int item_replace_iter(struct al_hash_iter_t *iterp, value_t v);

/*
 * delete key/value item pointed by iterator
 *   call al_hash_iter() in advance
 * return -1, not pointed (just created or pointed item is deleted)
 * return -4, hash table is destroyed
 * return -6, hash table type is not 'scalar'
 */
int item_delete_iter(struct al_hash_iter_t *iterp);


/*** iterators for linked hash */

/*
 * advance iterator pointed to linked_hash
 *  sort_value 0: item appears arbitary order.
 *             1: item appears dictionary order of key
 *             2: item appears counter dictionary order of key
 *          else: return -7
 *  al_link_value_cmp() is applied to compare the value.
 */
int al_linked_hash_iter(struct al_hash_iter_t *iterp, const char **key,
			struct al_linked_value_iter_t **v_iterp, int sort_value);

/*
 * destroy iterator
 */
int al_linked_value_iter_end(struct al_linked_value_iter_t *v_iterp);

/*
 * advance value iterator
 * return -1, reached end
 */
int al_linked_value_iter(struct al_linked_value_iter_t *v_iterp,
			 link_value_t *ret_v);

/*
 * Return number of values belong to value iterator.
 */
int al_linked_hash_nvalue(struct al_linked_value_iter_t *v_iterp);

/*
 * Rewind value iteration.
 */
int al_linked_hash_rewind_value(struct al_linked_value_iter_t *v_iterp);


/*** iterators for priority queue hash */
/*
 * advance iterator pointed to pq_hash
 *  sort_value 0: item appears arbitary order.
 *             1: item appears dictionary order of key
 *             2: item appears counter dictionary order of key
 *          else: return -7
 *  al_link_value_cmp() is applied to compare the value.
 */
int al_pqueue_hash_iter(struct al_hash_iter_t *iterp, const char **key,
			struct al_pqueue_value_iter_t **v_iterp);

/*
 * destroy iterator
 * return -3, v_iterp NULL
 */
int al_pqueue_value_iter_end(struct al_pqueue_value_iter_t *v_iterp);

/*
 * advance priority queue value iterator
 * return -1, reached end
 */
int al_pqueue_value_iter(struct al_pqueue_value_iter_t *v_iterp,
			 link_value_t *keyp, value_t *ret_count);

/*
 * Return number of values belong to value iterator.
 */
int al_pqueue_hash_nvalue(struct al_pqueue_value_iter_t *v_iterp);

/*
 * Rewind value iteration.
 */
int al_pqueue_hash_rewind_value(struct al_pqueue_value_iter_t *v_iterp);


/*
 * Note:
 *
 * item_set(), item_set_pv(), item_inc_init():
 *   New key/value entry is inserted to a hash table attached
 *   with some iterators, iterators may or not may point the
 *   new entry on subsequent al_hash_iter() call.
 *
 * item_detete(), item_delete_pv():
 *   Can not delete entries of a hash table attached some iterators.
 *   Item_detete() / item_delete_pv() are return with -5 immediately
 *   (either key is found, or not found).
 *
 * item_delete_iter():
 *   The `key' pointer returned from al_hash_iter() is valid until 
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
 *   If iterators pointed to same hash table invoke item_delete_iter(), 
 *   result is undefined, it may cause crash.
 *
 *   Call item_delete_iter() immediately after inserting new key/value
 *   (by item_set(), etc) to the pointed hash table without advancing
 *   iterator, result of the deletion is undefined.
 *   ex:
 *   bad: al_hash_iter() -> item_set() -> item_delete_iter() -> al_hash_iter()
 *   ok:  al_hash_iter() -> item_set() -> al_hash_iter() -> item_delete_iter()
 *   ok:  al_hash_iter() -> item_delete_iter() -> item_set() -> al_hash_iter()
 *
 * Iterators will be invalid when pointd hash table is destroyed by
 * al_free_hash().  It is still necessary to call al_hash_iter_end() 
 * on the iterators to free memory resources in the situation.
 *
 * sorted iterator:
 *   Sorted iterator is implemented using array of pointer
 *   pointed to each hash item. So, the array may be very large.
 */

/*
 *  priority queue, implemented by skiplist
 */
typedef value_t pq_value_t;
typedef link_value_t pq_key_t;

struct al_skiplist_t;
struct al_skiplist_iter_t;

int al_create_skiplist(struct al_skiplist_t **slp, int sort_order);
int al_free_skiplist(struct al_skiplist_t *sl);
int sl_empty_p(struct al_skiplist_t *sl);
unsigned long sl_n_entries(struct al_skiplist_t *sl);
int sl_skiplist_stat(struct al_skiplist_t *sl);

int sl_set(struct al_skiplist_t *sl, pq_key_t key, value_t v);
int sl_set_n(struct al_skiplist_t *sl, pq_key_t key, value_t v, unsigned long max_n);
int sl_delete(struct al_skiplist_t *sl, pq_key_t key);
int sl_delete_last_node(struct al_skiplist_t *sl);
int sl_key(struct al_skiplist_t *sl, pq_key_t key);
int sl_get(struct al_skiplist_t *sl, pq_key_t key, value_t *ret_v);

int sl_inc_init(struct al_skiplist_t *sl, pq_key_t key, long off, value_t *ret_v);
int sl_inc_init_n(struct al_skiplist_t *sl, pq_key_t key, long off, value_t *ret_v, unsigned long max_n);

int al_sl_iter_init(struct al_skiplist_t *sl, struct al_skiplist_iter_t **iterp);
int al_sl_iter_end(struct al_skiplist_iter_t *iterp);
int al_sl_iter(struct al_skiplist_iter_t *iterp, pq_key_t *keyp, value_t *ret_v);
int al_sl_rewind_iter(struct al_skiplist_iter_t *iterp);

/*
 *   utility
 */

char *
al_gettok(char *cp, char **savecp, char del);

/*
 *  char *cp = line;
 *  char buf[100]; int n;
 *  al_set_s(buf, cp, '\t');
 *  al_set_i(n, cp, '\t');
 *
 */

#define al_set_i(to, cp, del) do{if (cp) (to)=atoi(al_gettok((cp),&(cp),del));} while(0)
#define al_set_l(to, cp, del) do{if (cp) (to)=atol(al_gettok((cp),&(cp),del));} while(0)
#define al_set_s(to, cp, del) do{if (cp) strncpy((to),al_gettok((cp),&(cp),del),sizeof(to)-1);} while(0)
#define al_set_sp(to, cp, del) do{if (cp) (to)=al_gettok((cp),&(cp),del);} while(0)

/*
 *  char *elms[5], tmp[100];
 *  al_split(elms, tmp, "abc\tdef\t\tghi", '\t');
 *  elms== "abc", "def", "", "ghi", NULL
 * 
 */
int al_split_impl(char **elms, int elms_size, char *tmp_cp, int tmp_size, const char *str, const char *del);
int al_split_n_impl(char **elms, int elms_size, char *tmp_cp, int tmp_size, const char *str, const char *del, int n);
#define al_split(elms, tmp, str, del) al_split_impl((elms), sizeof(elms)/sizeof(char *), (tmp), sizeof(tmp), (str), (del))
#define al_split_n(elms, tmp, str, del, n) al_split_impl((elms), sizeof(elms)/sizeof(char *), (tmp), sizeof(tmp), (str), (del), (n))

/* not return nul */
/*
 *  char *elms[5], tmp[100];
 *  al_split_nn(elms, tmp, "abc\tdef\t\tghi", '\t');
 *  elms== "abc", "def", "ghi", NULL, NULL
 */

int al_split_nn_impl(char **elms, int elms_size, char *tmp_cp, int tmp_size, const char *str, const char *del);
int al_split_nn_n_impl(char **elms, int elms_size, char *tmp_cp, int tmp_size, const char *str, const char *del, int n);
#define al_split_nn_n(elms, tmp, str, del, n) al_split_impl((elms), sizeof(elms)/sizeof(char *), (tmp), sizeof(tmp), (str), (del), (n))

#endif
