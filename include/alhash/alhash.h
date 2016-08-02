/* hash table libraries */
/*
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 *
 * latest version see, https://github.com/evalquote/al_hash_table
 */

                 /* NOT thread safe */

#ifndef ALH_H
#define ALH_H
#include <inttypes.h>

/* switch */
#undef NUMSCAN
// #undef ITEM_PV	            /* define ITEM_PV enables item_xxx_pv() functions */
#define ITEM_PV	            /* define ITEM_PV enables item_xxx_pv() functions */
#undef INC_INIT_RETURN_ONE  /* inc_init() return 1 when new key */

/* type of hash entry scalar value */
typedef long value_t;

/* type of hash entry link value */
typedef const char * cstr_value_t;

/*
 *  priority queue, type
 */
typedef value_t pq_value_t;
typedef cstr_value_t pq_key_t;
struct al_skiplist_t;
struct al_skiplist_iter_t;

/*
 * heap type
 */
struct al_heap_t;
struct al_heap_iter_t;

/* flags */
/* sort order */
#define AL_SORT_NO		0x1
#define AL_SORT_DIC		0x2
#define AL_SORT_COUNTER_DIC	0x4
#define AL_SORT_NUMERIC		0x8
#define AL_SORT_VALUE		0x10
#define AL_SORT_FFK_ONLY	0x20
#define AL_SORT_FFK_REV		0x40
#define AL_FLAG_NONE		AL_SORT_NO
/* for iterator, call end() automatically at end of iteration */
#define AL_ITER_AE		0x10000
/* for heap value iterator only, pop root on every iteration */
#define AL_ITER_POP		0x20000

#define HASH_TYPE_SCALAR	0x100
#define HASH_TYPE_STRING	0x200
#define HASH_TYPE_LIST		0x400
#define HASH_TYPE_PQ		0x800
#define HASH_TYPE_POINTER	0x1000

/*
 *  for skiplist only
 *  sl_set always inserts new node to
 *  skiplist if key is already exists.
 */
#define SL_DUP_KEY              0x4000

/*
 * this macro is suitable for 'topk' parameter, means half position of items
 *
 * ex.
 *  ret = al_hash_topk_iter_init(ht_count, &itr,
 *				 AL_SORT_COUNTER_DIC|AL_SORT_VALUE|AL_SORT_FFK_REV,
 *				 AL_FFK_HALF);
 * The iterator itr points to the median of items in hash table ht_count.
 */
#define AL_FFK_HALF	(-2)

/* type of hash table */
struct al_hash_t;

/* type of iterator pointed to hash table */
struct al_hash_iter_t;


/* type of iterator pointed to list hash table */
struct al_list_value_iter_t;

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
  al_n_cancel_rehashing: number of canceling of moving entries between hash
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
 *   histogram of chain length of main and previous small hash table
 *   if chain length is over 10, count up [10] of returned array
 */
typedef unsigned long al_chain_length_t[11];

/* default bit size of hash table */
#define AL_DEFAULT_HASH_BIT 15

/*
 * User API
 *
 * return 0  on success
 * return -3 parameter error (provide NULL pointer)
 */

/*
 * create hash table
 *
 * type: one of following value type
 *    only one value for a key
 *       HASH_TYPE_SCALAR    long
 *       HASH_TYPE_STRING    char *
 *       HASH_TYPE_POINTER   void *
 *    multiple (char *) values for a key
 *       HASH_TYPE_LIST    // list is represented by cdr coding.
 *       HASH_TYPE_PQ      // priority queue
 *
 * bit == 0, use AL_DEFAULT_HASH_BIT
 *
 * al_set_pqueue_hash_parameter
 *  sort_order: AL_SORT_DIC:         item appears dictionary order of key
 *              AL_SORT_COUNTER_DIC: item appears counter dictionary order of key
 *              logior AL_SORT_NUMERIC: sort string numeric
 *  max_n:      0, unlimited
 *
 * al_set_pointer_hash_parameter
 *  dup_p   value duplication function   default: retp = malloc(size); memcpy(retp, ptr, size);
 *  free_p  value pointer free function  default: free(ptr);
 *  sort_p  value pointer compare function  default: NULL
 *  sort_rev_p  value pointer compare reverse function  default: NULL
 *    pass compare function parameter to al_get_pointer_hash_pointer(),
 *    pointer to user structure is returned.
 *
 * return -2 allocation fails
 * return -3 ht is NULL
 * return -6 ht is not pointer hash
 * return -9 call again
 *
 * al_init_unique_id
 *  initialize id for item_unique_id() and item_unique_id_with_inv()
 *
 * return -3 ht is NULL
 * return -6 ht is not scalor hash
 * return -7 ht is not empty
 */
int al_init_hash(int type, int bit, struct al_hash_t **htp);
int al_set_pqueue_hash_parameter(struct al_hash_t *ht, int sort_order, unsigned long max_n);
int al_set_pointer_hash_parameter(struct al_hash_t *ht,
				  int (*dup_p)(void *ptr, unsigned int size, void **ret_v),
				  void (*free_p)(void *ptr),
				  int (*sort_p)(const void *, const void *),
				  int (*sort_rev_p)(const void *, const void *));
void *al_get_pointer_hash_pointer(const void *a);
void *al_get_pointer_list_hash_pointer(const void *a);
int al_init_unique_id(struct al_hash_t *ht, long id);

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

int al_nkeys(struct al_hash_t *ht, unsigned long *nkeys);
int al_next_unique_id(struct al_hash_t *ht, long *ret_id);

/*
 * set error message, print out it on error and AL_ITER_AE set.
 */
int al_set_hash_err_msg_impl(struct al_hash_t *ht, const char *msg);

/*
 * return number of attached iterators of ht  (0 <= number, else error)
 */
int al_hash_n_iterators(struct al_hash_t *ht);

/*
 * predicate
 * return  0: yes,  return -1: no
 */
int al_is_pqueue_hash(struct al_hash_t *ht);
int al_is_pqueue_iter(struct al_hash_iter_t *iterp);
int al_is_list_hash(struct al_hash_t *ht);
int al_is_list_iter(struct al_hash_iter_t *iterp);

/*
 * set key and value to hash table
 *
 * if key is not in hash table, add it, else replace value field by v
 * if ret_pv is not NULL, return previous value (when define ITEM_PV)
 * item_set_pointer2() if ret_v is not NULL, return duplicated pointer (that ht points) of v
 * return -2, allocation fails
 * item_set_pointer[2](), v must not be NULL or return -3
 */
int item_set(struct al_hash_t *ht, const char *key, value_t v);
int item_set_pv(struct al_hash_t *ht, const char *key, value_t v, value_t *ret_pv);
int item_set_str(struct al_hash_t *ht, const char *key, cstr_value_t v);
int item_set_pointer(struct al_hash_t *ht, const char *key, void *v, unsigned int size);
int item_set_pointer2(struct al_hash_t *ht, const char *key, void *v, unsigned int size, void **ret_v);

/*
 * add value to list or pqueue hashtable
 * return -2, allocation fails
 * return -6, hash table type is not 'linked'
 */
int item_add_value_impl(struct al_hash_t *ht, const char *key,
			value_t v, cstr_value_t lv, void *vp, unsigned int, int flag);
#define item_add_value(ht, key, v) item_add_value_impl((ht), (key), (v), NULL, NULL, 0, HASH_TYPE_SCALAR)
#define item_add_value_str(ht, key, lv) item_add_value_impl((ht), (key), 0, (lv), NULL, 0, HASH_TYPE_STRING)
#define item_add_value_ptr(ht, key, ptr, size) item_add_value_impl((ht), (key), 0, NULL, (ptr), (size), HASH_TYPE_POINTER)
#define item_add_value_pq(ht, key, v) item_add_value((ht), (key), (v))
#define item_add_value_pq_str(ht, key, lv) item_add_value_str((ht), (key), (lv))

/*
 * find key on the hash table
 * return -1, key is not found
 * return -5, iterators are attached on the hash table
 *            (only item_delete and item_delete_pv)
 * return -6, item_set/item_get/item_replace/item_unique_id on string_hash, or,
 *            item_set_str/item_get_str/item_replace_str on scalar hash, or,
 *            on item_unique_id_with_inv, first ht must be scalar hash and
 *            second one must be string hash.
 *
 * if pointer for return value (ret_v, ret_pv) is NULL, ignore it
 * if ret_pv is not NULL, return previous value (when define ITEM_PV)
 *
 * delete:
 *   either of scalar hash table and linked hash is acceptable as parameter
 * replace:
 *   if key is found, replace value field by v, else return -1
 * item_unique_id()/item_unique_id_with_inv()
 *   assign unique id number (long) to each hash key, saved in ht as value,
 *   and return it to *ret_id. the id is incremented by one.
 *   if some keys are deleted from ht, and set same key to ht later,
 *   new id number is assigned.
 *   item_unique_id_with_inv(), unique_id and key pair is saved in invht.
 *   a format "%ld" is used to convert unique_id number to key string.
 *   there are no linkage between ht and invht. so, when some keys are deleted
 *   from ht, id and key pairs are remained in invht.
 * item_get_or_set()
 *   if key is found, return the value and return 0
 *   if key is not found, new key/id entry is inserted to a hash table and return 1,
 *     the id is assigned to *v if v is not NULL
 */
int item_key(struct al_hash_t *ht, const char *key);
int item_get(struct al_hash_t *ht, const char *key, value_t *ret_v);
int item_get_str(struct al_hash_t *ht, const char *key, cstr_value_t *ret_v);
int item_get_pointer(struct al_hash_t *ht, const char *key, void **ret_v);
int item_replace(struct al_hash_t *ht, const char *key, value_t v);
int item_replace_pv(struct al_hash_t *ht, const char *key, value_t v, value_t *ret_pv);
int item_replace_str(struct al_hash_t *ht, const char *key, cstr_value_t v);
int item_delete(struct al_hash_t *ht, const char *key);
int item_delete_pv(struct al_hash_t *ht, const char *key, value_t *ret_pv);
int item_unique_id(struct al_hash_t *ht, const char *key, long *ret_id);
int item_unique_id_with_inv(struct al_hash_t *ht, struct al_hash_t *invht, const char *key, long *ret_id);
int item_get_or_set(struct al_hash_t *ht, const char *key, value_t *v, value_t id);

int al_list_topk_hash_get(struct al_hash_t *ht, const char *key,
			  struct al_list_value_iter_t **v_iterp, int flag, long topk);
#define al_list_hash_get(ht, key, v_iterp, flag) al_list_topk_hash_get((ht), (key), (v_iterp), (flag), 0)

int al_pqueue_hash_get(struct al_hash_t *ht, const char *key,
		       struct al_pqueue_value_iter_t **v_iterp, int flag);

/*
 * increment value field by off
 * item_inc():
 *   if key is found, then increment value field by 'off' and
 *     set incremented value to *ret_v, and return 0
 *   if key is not found, then return -1. *ret_v is not changed.
 *
 * item_inc_init():
 *   if key is found, then increment value field by 'off' and
 *     set incremented value to *ret_v, and return 0
 *   if key is not found, add key with 'off' as initial value.
 *     (add key with 'init' as initial value. item_inc_init2())
 *     *ret_v is not modified.
 *     return 0, on successfully key adding, (undef   INC_INIT_RETURN_ONE)
 *     return 1, on successfully key adding, (defined INC_INIT_RETURN_ONE)
 *     return -2, allocation fails
 *     return -6, ht is string hash
 */
int item_inc(struct al_hash_t *ht, const char *key, value_t off, value_t *ret_v);
int item_inc_init(struct al_hash_t *ht, const char *key, value_t off, value_t *ret_v);
int item_inc_init2(struct al_hash_t *ht, const char *key, value_t off, value_t init, value_t *ret_v);

/* iterators */

/*
 * create an iterator attached to ht
 *   after first al_hash_iter() call, the iterator points entries
 *    (when hash table is not empty)
 *  flag AL_SORT_NO:          item appears arbitrary order.
 *       AL_SORT_DIC:         item appears dictionary order of key
 *       AL_SORT_COUNTER_DIC: item appears counter dictionary order of key
 *       logior AL_SORT_NUMERIC: sort key string numeric
 *       logior AL_SORT_VALUE: sort by value part instead of key,
 *                                 AL_SORT_NUMERIC is ignored, if it is logior_ed.
 *       logior AL_ITER_AE:   invoke al_hash_iter_end() automatically at end of iteration,
 *                            do not call al_hash_iter_end() again.
 *       else: return -7
 *   pointer_hash is not accept AL_SORT_VALUE flag, return -8 if set.
 * return -2, allocation fails
 * return -99, internal error
 *
 * al_hash_auto_end_iter_init() create an iterator which will be automatic end
 * at end of iteration.  It is not necessary to call al_hash_iter_end() on
 * normal end.
 *
 * Now, this function is implemented as macro, which calls al_hash_topk_iter_init()
 */
/* int al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp, int flag); */

 /*
  * Iterate on only first top-k items according to the specified sort order
  *   AL_SORT_DIC or AL_SORT_COUNTER_DIC.
  * if topk == 0 or flag is AL_SORT_NO, same as al_hash_iter_init()
  * top k item appears arbitrary order when AL_SORT_FFK_ONLY flag on.
  *   (additional sort is canceled)
  *   AL_SORT_FFK_REV reverse the order of the selected top k items.
  *
  * The macro AL_FFK_HALF is suitable for 'topk' parameter, means half position of items (median).
  * See above comment of AL_FFK_HALF.
  */
int al_hash_topk_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp, int flag, long topk);
#define al_hash_iter_init(ht, iterp, flag) al_hash_topk_iter_init((ht), (iterp), (flag), 0)

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
 * do not modify the string pointed by `key'
 */
int al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v);
int al_hash_iter_str(struct al_hash_iter_t *iterp, const char **key, cstr_value_t *ret_v);
int al_hash_iter_pointer(struct al_hash_iter_t *iterp, const char **key, void **ret_v);
/*
 * replace value field pointed by iterator
 * return -1, not pointed (just created or pointed item is deleted)
 * return -4, hash table is destroyed
 */
int item_replace_iter(struct al_hash_iter_t *iterp, value_t v);

/*
 * delete key/value item pointed by iterator
 * return -1, not pointed (just created or pointed item is deleted)
 * return -4, hash table is destroyed
 */
int item_delete_iter(struct al_hash_iter_t *iterp);

/*** iterators for list hash */

int al_list_hash_topk_iter(struct al_hash_iter_t *iterp, const char **key,
			   struct al_list_value_iter_t **v_iterp, int flag, long topk);
#define al_list_hash_iter(iterp, key, v_iterp, flag) al_list_hash_topk_iter((iterp), (key), (v_iterp), (flag), 0)

/*
 * destroy iterator
 */
int al_list_value_iter_end(struct al_list_value_iter_t *v_iterp);

/*
 * advance value iterator
 * return -1, reached end
 */
int al_list_value_iter(struct al_list_value_iter_t *v_iterp,
		       value_t *ret_v);
int al_list_value_iter_str(struct al_list_value_iter_t *v_iterp,
			   cstr_value_t *ret_v);
int al_list_value_iter_ptr(struct al_list_value_iter_t *v_iterp,
			   void **ret_v);

int
al_list_value_iter_min_max(struct al_list_value_iter_t *v_iterp,
			   value_t *ret_v_min, value_t *ret_v_max);
/*
 * Return number of values belong to value iterator.
 */
int al_list_hash_iter_nvalue(struct al_list_value_iter_t *v_iterp);

/*
 * Rewind value iteration.
 */
int al_list_hash_rewind_value(struct al_list_value_iter_t *v_iterp);



/*** iterators for priority queue hash */
/*
 * advance iterator pointed to pq_hash and create an another iterator for access values
 *  flag AL_FLAG_NONE: no flag
 *       AL_ITER_AE:   invoke al_pqueue_value_iter_end() automatically.
 *       else: return -7
 *  return -2 allocation fails
 */
int al_pqueue_hash_iter(struct al_hash_iter_t *iterp, const char **key,
			struct al_pqueue_value_iter_t **v_iterp, int flag);

/*
 * destroy iterator
 * return -3, v_iterp NULL
 */
int al_pqueue_value_iter_end(struct al_pqueue_value_iter_t *v_iterp);

/*
 * advance priority queue value iterator
 * return -1, reached end
 */
int al_pqueue_value_iter(struct al_pqueue_value_iter_t *vip, value_t *ret_v);
int al_pqueue_value_iter_str(struct al_pqueue_value_iter_t *v_iterp,
			     cstr_value_t *keyp, pq_value_t *ret_count);

/*
 * Return number of values belong to value iterator.
 */
int al_pqueue_hash_iter_nvalue(struct al_pqueue_value_iter_t *v_iterp);

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
 * item_delete(), item_delete_pv():
 *   Can not delete entries of a hash table attached some iterators.
 *   Item_delete() / item_delete_pv() are return with -5 immediately
 *   (either key is found, or not found).
 *
 * item_delete_iter():
 *   The `key' pointer returned from al_hash_iter() is valid until
 *   next al_hash_iter() call (unless hash table destroyed).
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
 * Iterators will be invalid when pointed hash table is destroyed by
 * al_free_hash().  It is still necessary to call al_hash_iter_end()
 * on the iterators to free memory resources in the situation.
 *
 * sorted iterator:
 *   Sorted iterator is implemented using array of pointer
 *   pointed to each hash item. So, the array may become very large.
 */

/*
 *  priority queue, implemented by skiplist
 */

/*
 *  return -2, allocation fails
 *  return -3, sl is NULL
 */
int al_create_skiplist(struct al_skiplist_t **slp, int flag);
int al_free_skiplist(struct al_skiplist_t *sl);
int al_skiplist_stat(struct al_skiplist_t *sl);
int al_set_skiplist_err_msg(struct al_skiplist_t *sl, const char *msg);

int sl_empty_p(struct al_skiplist_t *sl);
long sl_n_entries(struct al_skiplist_t *sl);

/*
 *  return -2, allocation fails
 */
int sl_set(struct al_skiplist_t *sl, pq_key_t key, pq_value_t v);
int sl_set_n(struct al_skiplist_t *sl, pq_key_t key, pq_value_t v, unsigned long max_n);

/*
 *  return -1, skip list is empty
 */
int sl_front(struct al_skiplist_t *sl, pq_key_t *keyp, pq_value_t *ret_v);
int sl_back(struct al_skiplist_t *sl, pq_key_t *keyp, pq_value_t *ret_v);
int sl_pop_front_node(struct al_skiplist_t *sl);

int sl_delete(struct al_skiplist_t *sl, pq_key_t key);
int sl_delete_last_node(struct al_skiplist_t *sl);
int sl_key(struct al_skiplist_t *sl, pq_key_t key);
int sl_get(struct al_skiplist_t *sl, pq_key_t key, pq_value_t *ret_v);

/*
 *  if key is found, then increment value field by 'off' and
 *    set incremented value to *ret_v, and return 0
 *  if key is not found, add key with 'off' as initial value.
 *    *ret_v is not modified.
 *    return 0, on successfully key adding, (undef   INC_INIT_RETURN_ONE)
 *    return 1, on successfully key adding, (defined INC_INIT_RETURN_ONE)
 *    return -2, allocation fails
 */
int sl_inc_init(struct al_skiplist_t *sl, pq_key_t key, pq_value_t off, pq_value_t *ret_v);

/*
 *  Same as sl_inc_init() except number of skiplist item is limited by max_n.
 *  return 0 when key is not inserted by max_n limitation.
 */
int sl_inc_init_n(struct al_skiplist_t *sl, pq_key_t key, pq_value_t off, pq_value_t *ret_v, unsigned long max_n);

/*
 *  create an iterator attached to sl
 *  flag AL_FLAG_NONE: no flag
 *       AL_ITER_AE:   invoke sl_iter_end() automatically.
 *       else: return -7
 *  return -2, allocation fails
 */
int sl_iter_init(struct al_skiplist_t *sl, struct al_skiplist_iter_t **iterp, int flag);

int sl_iter_end(struct al_skiplist_iter_t *iterp);
int sl_iter(struct al_skiplist_iter_t *iterp, pq_key_t *keyp, pq_value_t *ret_v);
int sl_rewind_iter(struct al_skiplist_iter_t *iterp);

/* heap */
int al_create_heap(struct al_heap_t **hpp, int sort_order, unsigned int max_n);
int al_free_heap(struct al_heap_t *hp);
int al_insert_heap(struct al_heap_t *hp, value_t v);
int al_pop_heap(struct al_heap_t *hp, value_t *ret_v);
int al_delete_heap(struct al_heap_t *hp, unsigned int pos, value_t *ret_v);
int hp_empty_p(struct al_heap_t *hp);
long hp_n_entries(struct al_heap_t *hp);
int hp_iter_init(struct al_heap_t *hp, struct al_heap_iter_t **iterp, int flag);
int hp_iter(struct al_heap_iter_t *iterp, pq_value_t *ret_v);
int hp_iter_end(struct al_heap_iter_t *iterp);
int hp_rewind_iter(struct al_heap_iter_t *iterp);

/* find first key */
/* qsort like interface, element size is sizeof(void *) */
void
al_ffk(void *base, long nel, unsigned long esize,
       int (*compar)(const void *, const void *),
       long topk);

/*
 *   utility
 */

#define _AL_S_(NAME)#NAME
#define _AL_FLSD(x, y) x _AL_S_(y)
#define _AL_FLS _AL_FLSD(__FILE__, [line __LINE__])
#define al_set_hash_err_msg(ht, msg) al_set_hash_err_msg_impl((ht), msg _AL_FLS)

char *
al_gettok(char *cp, char **savecp, char del);

/*
 *  char *cp = line;
 *  char buf[100]; int n;
 *  al_set_s(buf, cp, '\t');
 *  al_set_i(n, cp, '\t');
 */

#define al_set_i(to, cp, del) do{if (cp) (to)=atoi(al_gettok((cp),&(cp),(del)));}while(0)
#define al_set_l(to, cp, del) do{if (cp) (to)=atol(al_gettok((cp),&(cp),(del)));}while(0)
#define al_set_s(to, cp, del) do{if (cp) strncpy((to),al_gettok((cp),&(cp),(del)),sizeof(to)-1);}while(0)
#define al_set_sp(to, cp, del) do{if (cp) (to)=al_gettok((cp),&(cp),(del));}while(0)

/*
 *  char *elms[5], tmp[100];
 *  al_split(elms, tmp, "abc\tdef\t\tghi", "\t");
 *  elms== "abc", "def", "", "ghi", NULL
 *
 *  al_split_n(elms, tmp, "abc\tdef\t\tghi", "\t", 3);
 *  elms== "abc", "def", "\t\tghi", NULL
  */
int al_split_impl(char **elms, unsigned int elms_size, char *tmp_cp, unsigned int tmp_size, char *str, const char *del);
int al_split_n_impl(char **elms, unsigned int elms_size, char *tmp_cp, unsigned int tmp_size, char *str, const char *del, int n);
#define al_split(elms, tmp, str, del) al_split_impl((elms), sizeof(elms)/sizeof(char *), (tmp), sizeof(tmp), (str), (del))
#define al_split_n(elms, tmp, str, del, n) al_split_n_impl((elms), sizeof(elms)/sizeof(char *), (tmp), sizeof(tmp), (str), (del), (n))

/* not return nul string */
/*
 *  char *elms[5], tmp[100];
 *  al_split_nn(elms, tmp, "abc\tdef\t\tghi", "\t");
 *  elms== "abc", "def", "ghi", NULL
 *  al_split_nn_n(elms, tmp, "abc\tdef\t\tghi", "\t", 2);
 *  elms== "abc", "def\tghi", NULL
 */
int al_split_nn_impl(char **elms, unsigned int elms_size, char *tmp_cp, unsigned int tmp_size, char *str, const char *del);
int al_split_nn_n_impl(char **elms, unsigned int elms_size, char *tmp_cp, unsigned int tmp_size, char *str, const char *del, int n);
#define al_split_nn(elms, tmp, str, del) al_split_nn_impl((elms), sizeof(elms)/sizeof(char *), (tmp), sizeof(tmp), (str), (del))
#define al_split_nn_n(elms, tmp, str, del, n) al_split_nn_n_impl((elms), sizeof(elms)/sizeof(char *), (tmp), sizeof(tmp), (str), (del), (n))

/* get number of separated elements */
int n_elements(const char *str, const char *del);

/* get number of separated elements, not return nul string */
/*
 *  typical usage: see sample/alcount.c
 *
 *  int line_size = AL_LINE_SIZE;
 *  char *line = (char *)malloc(line_size);
 *  while (0 <= (len = al_readline(stdin, &line, &line_size))) {
 *    int nelm = n_elements_nn(line, " \t") + 1;  // + 1, for last NULL
 *    char *elms[nelm];
 *    int retn = al_split_nn_impl(elms, nelm, line, line_size, line, " \t");
 *  }
 */
int n_elements_nn(const char *str, const char *del);

int al_strcjoin_n_impl(char **elms, unsigned int elms_size, char *tmp_cp, unsigned int tmp_size, const char delch, int n);
#define al_strcjoin_n(elms, tmp_cp, del, n) al_str_join_n_impl((elms),sizeof(elms)/sizeof(char *),(tmp_cp),sizeof(tmp_cp),(del),(n))
#define al_strcjoin(elms, tmp_cp, del, n) al_str_join_n_impl((elms),sizeof(elms)/sizeof(char *),(tmp_cp),sizeof(tmp_cp),(del),(n),sizeof(elms)/sizeof(char *))

/* readline */
/* initial line buffer size */
#ifndef AL_LINE_SIZE
#define AL_LINE_SIZE 10240
#endif
/* increment size */
#ifndef AL_LINE_INC
#define AL_LINE_INC   2048
#endif
int al_readline(FILE *fd, char **line_p, int *line_size_p);

/*
int  line_size = AL_LINE_SIZE; // default initial size
char *line = (char *)malloc(line_size);
int len;
while (0 <= (len = al_readline(fd, &line, &line_size))) {
  ...
}
free(line);

 */

#endif
