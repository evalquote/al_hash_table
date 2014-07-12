/*
 * The hash function used here is FNV,
 *   <http://www.isthe.com/chongo/tech/comp/fnv/>
 */
/*
 *   Use and distribution licensed under the BSD license.  See
 *   the LICENSE file for full text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define AL_HASH_O
#include "hash.h"
#undef AL_HASH_O

/*
 * WARN 0: NO
 *      1:
 *      2: FULL
 */
#ifndef AL_WARN
#define AL_WARN 1
#endif

#ifndef MEAN_CHAIN_LENGTH
#define MEAN_CHAIN_LENGTH 2
#endif

/* type of linked value */
typedef struct lvt_ {struct lvt_ *link; link_value_t value;} link_t;

/* type of lcdr value */
#define LCDR_SIZE_L 4
#define LCDR_SIZE_U 64
typedef struct lcdr_ {
  struct lcdr_ *link;
  unsigned int va_used;          // va[0] .. av[va_used-1] are used
  unsigned int va_size; // LCDR_SIZE_L .. LCDR_SIZE_U
  link_value_t va[1];   // variable size, LCDR_SIZE_L .. LCDR_SIZE_U
} lcdr_t;

/* hash entry, with unique key */

struct item {
  struct item *chain;
  char *key;
  union {
    value_t      value;
    link_value_t cstr;
    link_t       *link;
    struct al_skiplist_t *skiplist;
    void         *ptr;
    lcdr_t	 *lcdr;
  } u;
};

#define HASH_FLAG_PQ_SORT_DIC	AL_SORT_DIC
#define HASH_FLAG_PQ_SORT_C_DIC	AL_SORT_COUNTER_DIC
#define HASH_FLAG_SORT_NUMERIC	AL_SORT_NUMERIC
#define HASH_FLAG_SORT_ORD	(AL_SORT_DIC|AL_SORT_COUNTER_DIC)
#define HASH_FLAG_SORT_MASK	(HASH_FLAG_SORT_ORD|AL_SORT_NUMERIC|AL_SORT_VALUE)

#define HASH_FLAG_SCALAR	HASH_TYPE_SCALAR
#define HASH_FLAG_STRING	HASH_TYPE_STRING
#define HASH_FLAG_LINKED	HASH_TYPE_LINKED
#define HASH_FLAG_PQ		HASH_TYPE_PQ
#define HASH_FLAG_POINTER	HASH_TYPE_POINTER
#define HASH_FLAG_LCDR		HASH_TYPE_LCDR
#define HASH_TYPE_MASK		(HASH_TYPE_SCALAR|HASH_TYPE_STRING|HASH_TYPE_LINKED|HASH_FLAG_PQ|HASH_TYPE_POINTER|HASH_FLAG_LCDR)
#define HASH_FLAG_PARAM_SET	(HASH_TYPE_LCDR<<1)

#define ITER_FLAG_AE		0x10000   // call end() at end of iteration
#define ITER_FLAG_VIRTUAL	0x80000   // virtual hash_iter createed
                                          //  al_linked_hash_get() or al_pqueue_hash_get()

#define hash_size(n) (1<<(n))

struct al_hash_iter_t;

struct al_hash_t {
  unsigned int  hash_bit;
  unsigned int  n_rehashing;
  unsigned long n_entries;	// number of items in hash_table
  unsigned long n_entries_old;	// number of items in hash_table_old
  unsigned long n_cancel_rehashing;

  int rehashing;		// 1: under re-hashing
  unsigned int rehashing_front;
  long moving_unit;

  unsigned int hash_mask;
  unsigned int hash_mask_old;

  struct item **hash_table;	// main hash table
  struct item **hash_table_old;
  struct al_hash_iter_t *iterators;	// iterators attached to me
  unsigned long pq_max_n;	// priority queue, max number of entries
  int (*dup_p)(void *ptr, unsigned int size, void **ret_v);  // pointer hash pointer duplication
  int (*free_p)(void *ptr);				     // pointer hash pointer free
  unsigned int h_flag;		// sort order, ...
  const char *err_msg;		// output on auto ended iterator abend
};

/* iterator to hash table */
struct al_hash_iter_t {
  /*
   * if 0 <= index < hash_size(ht->hash_bit - 1)
   *   ht->hash_table_old[index]
   * else
   *   ht->hash_table[index - hash_size(ht->hash_table - 1)]
   */
  struct al_hash_t *ht; // parent
  unsigned int index;
  unsigned int oindex;	// save max index when key is sorted
  struct item **pplace;
  struct item **place;
  struct item *to_be_free; // iter_delete()ed item
  struct item **sorted;	   // for sort by key
  struct al_hash_iter_t *chain;
  unsigned int n_value_iter;    // number of value iterators pointed me
  unsigned int hi_flag;	        // lower 2byte are copy of ht->flag
};

struct al_linked_value_iter_t {
  link_t *li_link, *li_current;
  link_value_t *li_sorted;
  struct al_hash_iter_t *li_pitr; // parent
  unsigned int li_max_index;
  unsigned int li_index;     // index of sorted[]
  int li_flag; // AL_ITER_AE bit only
};

struct al_lcdr_value_iter_t {
  lcdr_t *cd_link, *cd_current;
  link_value_t *cd_sorted;
  struct al_hash_iter_t *cd_pitr; // parent
  unsigned int cd_max_index;
  unsigned int cd_index;     // index of sorted[]
  unsigned int cd_cindex;    // index of current->va[]
  int cd_flag; // AL_ITER_AE bit only
};

struct al_pqueue_value_iter_t {
  struct al_skiplist_t *sl;
  struct al_skiplist_iter_t *sl_iter;
  struct al_hash_iter_t *pi_pitr; // parent
  int pi_flag; // AL_ITER_AE bit only
};

// FNV-1a hash
#if 0
static
uint64_t
hash_fn(char *cp)
{
  uint64_t hv = 14695981039346656037UL;
  while (*cp) {
    hv ^= (uint64_t)*cp++;
    hv *= 1099511628211UL;
  }
  return hv;
}
#endif

static uint32_t
al_hash_fn_i(const char *cp)
{
  uint32_t hv = 2166136261U;
  while (*cp) {
    hv ^= (uint32_t)*cp++;
    hv *= 16777619U;
  }
  return hv;
}

static int
resize_hash(int bit, struct al_hash_t *ht)
{
  struct item **hash_table;

  hash_table = (struct item **)calloc(hash_size(bit), sizeof(struct item *));
  if (!hash_table) return -2;

  ht->hash_table = hash_table;
  ht->hash_bit = bit;
  ht->hash_mask = hash_size(bit)-1;
  ht->hash_mask_old = hash_size(bit-1)-1;
  return 0;
}

static void
moving(struct al_hash_t *ht)
{
  if (ht->iterators) return;
  long i;
  for (i = 0; i < ht->moving_unit; i++) {
    struct item *it = ht->hash_table_old[ht->rehashing_front];
    while (it) {
      unsigned int hindex = al_hash_fn_i(it->key) & ht->hash_mask;
      struct item *next = it->chain;
      it->chain = ht->hash_table[hindex];
      ht->hash_table[hindex] = it;
      ht->n_entries_old--;
      ht->n_entries++;
      it = next;
    }
    ht->hash_table_old[ht->rehashing_front] = NULL;
    ht->rehashing_front++;
    if (hash_size(ht->hash_bit-1) <= ht->rehashing_front) {
      free((void *)ht->hash_table_old);
      ht->rehashing_front = 0;
      ht->hash_table_old = NULL;
      ht->moving_unit *= 2;
      ht->rehashing = 0;
      break;
    }
  }
}

static int
do_rehashing(struct al_hash_t *ht)
{
  int ret = 0;

  ht->hash_table_old = ht->hash_table;
  ht->hash_table = NULL;
  ret = resize_hash(ht->hash_bit + 1, ht);
  if (ret) {
    /* unwind */
    ht->hash_table = ht->hash_table_old;
    ht->hash_table_old = NULL;
  } else {
    ht->rehashing = 1;
    ht->rehashing_front = 0;
    ht->n_entries_old = ht->n_entries;
    ht->n_entries = 0;
    ht->n_rehashing++;
    moving(ht);
  }
  return ret;
}

static struct item *
hash_find(struct al_hash_t *ht, char *key, unsigned int hv)
{
  struct item *it;
  unsigned int hindex;

  if (ht->rehashing && ht->rehashing_front <= (hindex = (hv & ht->hash_mask_old)))
    it = ht->hash_table_old[hindex];
  else
    it = ht->hash_table[hv & ht->hash_mask];

  while (it) {
    if (strcmp(key, it->key) == 0) { // found
      return it;
    }
    it = it->chain;
  }
  return NULL;
}

static int
hash_insert(struct al_hash_t *ht, unsigned int hv, char *key, struct item *it)
{
  int ret = 0;
  unsigned int hindex;

  if (ht->rehashing && ht->rehashing_front <= (hindex = (hv & ht->hash_mask_old))) {
    it->chain = ht->hash_table_old[hindex];
    ht->hash_table_old[hindex] = it;
    ht->n_entries_old++;
  } else {
    it->chain = ht->hash_table[hv & ht->hash_mask];
    ht->hash_table[hv & ht->hash_mask] = it;
    ht->n_entries++;
  }
  if (ht->rehashing) {
    moving(ht);
    if (ht->hash_mask * (MEAN_CHAIN_LENGTH + 1) < ht->n_entries)
      ht->n_cancel_rehashing++;
  } else if (!ht->rehashing && !ht->iterators &&
	     ht->hash_mask * (MEAN_CHAIN_LENGTH + 1) < ht->n_entries) {
    ret = do_rehashing(ht);
  }
  return ret;
}

static int
hash_v_insert(struct al_hash_t *ht, unsigned int hv, char *key, value_t v)
{
  int ret = 0;
  struct item *it = (struct item *)malloc(sizeof(struct item));
  if (!it) return -2;

  it->u.value = v;
  it->key = strdup(key);
  if (!it->key) {
    free((void *)it);
    return -2;
  }
  ret = hash_insert(ht, hv, key, it);
  if (ret) {
    free((void *)it->key);
    free((void *)it);
  }
  return ret;
}

static struct item *
hash_delete(struct al_hash_t *ht, char *key, unsigned int hv)
{
  struct item *it;
  struct item **place;
  unsigned int hindex;
  int old = 0;

  if (ht->rehashing && ht->rehashing_front <= (hindex = (hv & ht->hash_mask_old))) {
    place = &ht->hash_table_old[hindex];
    old = 1;
  } else {
    place = &ht->hash_table[hv & ht->hash_mask];
  }

  it = *place;
  while (it && strcmp(key, it->key) != 0) {
    place = &it->chain;
    it = it->chain;
  }
  if (!it) return NULL;

  *place = it->chain;
  if (old)
    ht->n_entries_old--;
  else
    ht->n_entries--;

  return it;
}

static int
init_hash(int bit, struct al_hash_t **htp)
{
  int ret = 0;
  if (!htp) return -3;
  *htp = NULL;

  struct al_hash_t *al_hash = (struct al_hash_t *)calloc(1, sizeof(struct al_hash_t));
  if (!al_hash) return -2;

  if (bit == 0)
    bit = AL_DEFAULT_HASH_BIT;

  ret = resize_hash(bit, al_hash);
  if (ret) return ret;

  al_hash->moving_unit = 2 * bit;
  *htp = al_hash;
  return 0;
}

int
al_init_hash(int type, int bit, struct al_hash_t **htp)
{
  if ((type & ~HASH_TYPE_MASK) != 0 || (type & (type - 1)) != 0) return -7;
  int ret = init_hash(bit, htp);
  if (!ret)
    (*htp)->h_flag = type;
  return ret;
}

int
al_set_pqueue_parameter(struct al_hash_t *ht, int sort_order, unsigned long max_n)
{
  if (!ht) return -3;
  if (!(ht->h_flag & HASH_FLAG_PQ)) return -6;
  if (ht->h_flag & HASH_FLAG_PARAM_SET) return -6; // parameter already set
  int so = sort_order & HASH_FLAG_SORT_ORD;
  if (so != AL_SORT_DIC && so != AL_SORT_COUNTER_DIC) return -7;

  ht->h_flag |= so | HASH_FLAG_PARAM_SET;
  if (sort_order & AL_SORT_NUMERIC)
    ht->h_flag |= HASH_FLAG_SORT_NUMERIC;
  ht->pq_max_n = max_n;

  return 0;
}

int
al_set_pointer_hash_parameter(struct al_hash_t *ht,
			      int (*dup_p)(void *ptr, unsigned int size, void **ret_v),
			      int (*free_p)(void *ptr))
{
  if (!ht) return -3;
  if (!(ht->h_flag & HASH_FLAG_POINTER)) return -6;
  if (ht->h_flag & HASH_FLAG_PARAM_SET) return -6; // parameter already set
  if (dup_p)
    ht->dup_p = dup_p;
  if (free_p)
    ht->free_p = free_p;
  ht->h_flag |= HASH_FLAG_PARAM_SET;

  return 0;
}

static void
free_linked_value(link_t *lp)
{
  while (lp) {
    link_t *nextp = lp->link;
    al_free_linked_value(lp->value);
    free((void *)lp);
    lp = nextp;
  }
}

static void
free_lcdr_value(lcdr_t *dp)
{
  unsigned int i;
  while (dp) {
    lcdr_t *nextp = dp->link;
    for (i = 0; i < dp->va_used; i++)
      al_free_linked_value(dp->va[i]);
    free((void *) dp);
    dp = nextp;
  }
}

static void
free_value(struct al_hash_t *ht, struct item *it)
{
  switch (ht->h_flag & HASH_TYPE_MASK) {
  case HASH_FLAG_STRING:
    al_free_linked_value(it->u.cstr);
    break;
  case HASH_FLAG_POINTER:
    if (ht->free_p)
      ht->free_p(it->u.ptr);
    else
      free(it->u.ptr);
    break;
  case HASH_FLAG_PQ:
    al_free_skiplist(it->u.skiplist);
    break;
  case HASH_FLAG_LINKED:
    free_linked_value(it->u.link);
    break;
  case HASH_FLAG_LCDR:
    free_lcdr_value(it->u.lcdr);
    break;
  }
}

static void
free_hash(struct al_hash_t *ht, struct item **itp, unsigned int start, unsigned int size)
{
  unsigned int i;
  for (i = start; i < size; i++) {
    struct item *it = itp[i];
    while (it) {
      struct item *next = it->chain;
      free_value(ht, it);
      free((void *)it->key);
      free((void *)it);
      it = next;
    }
  }
}

static void
free_to_be_free(struct al_hash_iter_t *iterp)
{
  if (iterp->to_be_free) {
    free((void *)iterp->to_be_free->key);
    free_value(iterp->ht, iterp->to_be_free);
    free((void *)iterp->to_be_free);
    iterp->to_be_free = NULL;
  }
}

int
al_free_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  if (ht->rehashing) {
    free_hash(ht, ht->hash_table_old, ht->rehashing_front, hash_size(ht->hash_bit - 1));
    free((void *)ht->hash_table_old);
  }
  free_hash(ht, ht->hash_table, 0, hash_size(ht->hash_bit));
  free((void *)ht->hash_table);

#if 1 <= AL_WARN
  if (ht->iterators)
    fprintf(stderr, "WARN: al_free_hash, iterators still exists on ht(%p)\n", ht);
#endif
  struct al_hash_iter_t *ip;
  for (ip = ht->iterators; ip; ip = ip->chain) {
    free_to_be_free(ip); // pointer hash needs ip->ht, to_be_free value free()ed early
    ip->ht = NULL;
  }
  free((void *)ht);
  return 0;
}

/**********/

static int
attach_iter(struct al_hash_t *ht, struct al_hash_iter_t *iterp)
{
  if (!ht) return -4;
#if 2 <= AL_WARN
  if (ht->iterators) {
    fprintf(stderr, "WARN: iter_init, other iterators exists on ht(%p)\n", ht);
  }
#endif
  iterp->chain = ht->iterators;
  ht->iterators = iterp;
  return 0;
}

static void
detach_iter(struct al_hash_t *ht, struct al_hash_iter_t *iterp)
{
  if (!ht || !ht->iterators) return;

  struct al_hash_iter_t **pp = &ht->iterators;
  struct al_hash_iter_t *ip = ht->iterators;

  while (ip && ip != iterp) {
    pp = &ip->chain;
    ip = ip->chain;
  }
  if (!ip) return;
  *pp = ip->chain;
}

static long
add_it_to_array_for_sorting(struct item **it_array, unsigned int index,
			    struct item **itp, unsigned int start,
			    unsigned int size, long nmax)
{
  unsigned int i;
  for (i = start; i < size; i++) {
    struct item *it = itp[i];
    while (it) {
      if (--nmax < 0) return -99;
      it_array[index++] = it;
      it = it->chain;
    }
  }
  return index;
}

static int
it_cmp(const void *a, const void *b)
{
  return strcmp((*(struct item **)a)->key, (*(struct item **)b)->key);
}

static int
itn_cmp(const void *a, const void *b)
{
  return -strcmp((*(struct item **)a)->key, (*(struct item **)b)->key);
}

static int
str_num_cmp(const char *a, const char *b)
{
#ifdef NUMSCAN
  double da, db;
  int na = 0, nb = 0;
  na = sscanf(a, "%lf", &da);
  nb = sscanf(b, "%lf", &db);
  if (na == 0 || nb == 0)
    return strcmp(a, b);
  else
    return da <= db ? -1 : 1;
#else
  if (a[0] == '-' && b[0] == '-')
    return -strcmp(a, b);
  else
    return strcmp(a, b);
#endif
}

static int
it_num_cmp(const void *a, const void *b)
{
  return str_num_cmp((*(struct item **)a)->key, (*(struct item **)b)->key);
}

static int
itn_num_cmp(const void *a, const void *b)
{
  return -str_num_cmp((*(struct item **)a)->key, (*(struct item **)b)->key);
}

static int
it_num_value_cmp(const void *a, const void *b)
{
  return (*(struct item **)a)->u.value <= (*(struct item **)b)->u.value ? -1 : 1;
}

static int
itn_num_value_cmp(const void *a, const void *b)
{
  return (*(struct item **)a)->u.value > (*(struct item **)b)->u.value ? -1 : 1;
}

static int
it_value_cmp(const void *a, const void *b)
{
  return strcmp((*(struct item **)a)->u.cstr, (*(struct item **)b)->u.cstr);
}

static int
itn_value_cmp(const void *a, const void *b)
{
  return -strcmp((*(struct item **)a)->u.cstr, (*(struct item **)b)->u.cstr);
}

static int
it_value_strnum_cmp(const void *a, const void *b)
{
  return str_num_cmp((*(struct item **)a)->u.cstr, (*(struct item **)b)->u.cstr);
}

static int
itn_value_strnum_cmp(const void *a, const void *b)
{
  return -str_num_cmp((*(struct item **)a)->u.cstr, (*(struct item **)b)->u.cstr);
}

int
al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp, int flag)
{
  if (!ht || !iterp) return -3;
  *iterp = NULL;

  int so = flag & HASH_FLAG_SORT_ORD;
  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;
  if (so < AL_SORT_NO || AL_SORT_COUNTER_DIC < so) return -7;

  if (so != AL_SORT_NO) { // do sort keys
    long sidx = 0;
    struct item **it_array = NULL;

    it_array = (struct item **)malloc(sizeof(struct item *) *
				      (ht->n_entries + ht->n_entries_old));
    if (!it_array) {
      free((void *)ip);
      return -2;
    }

    if (ht->rehashing) {
      sidx = add_it_to_array_for_sorting(it_array, sidx, ht->hash_table_old,
					 ht->rehashing_front, hash_size(ht->hash_bit - 1),
					 ht->n_entries_old);
      if (sidx != ht->n_entries_old) {
	free((void *)it_array);
	free((void *)ip);
	return -99;
      }
    }
    sidx = add_it_to_array_for_sorting(it_array, sidx, ht->hash_table,
				       0, hash_size(ht->hash_bit), ht->n_entries);
    if (sidx != ht->n_entries + ht->n_entries_old) {
      free((void *)it_array);
      free((void *)ip);
      return -99;
    }
    if (flag & AL_SORT_VALUE) { // sort by value part

      if ((ht->h_flag & HASH_FLAG_STRING) == 0) { // scalar
	if (so == AL_SORT_DIC)
	  qsort((void *)it_array, sidx, sizeof(struct item *), it_num_value_cmp);
	else
	  qsort((void *)it_array, sidx, sizeof(struct item *), itn_num_value_cmp);
      } else if (flag & AL_SORT_NUMERIC) {
	if (so == AL_SORT_DIC)
	  qsort((void *)it_array, sidx, sizeof(struct item *), it_value_strnum_cmp);
	else
	  qsort((void *)it_array, sidx, sizeof(struct item *), itn_value_strnum_cmp);
      } else {
	if (so == AL_SORT_DIC)
	  qsort((void *)it_array, sidx, sizeof(struct item *), it_value_cmp);
	else
	  qsort((void *)it_array, sidx, sizeof(struct item *), itn_value_cmp);
      }
    } else if (flag & AL_SORT_NUMERIC) {
      if (so == AL_SORT_DIC)
	qsort((void *)it_array, sidx, sizeof(struct item *), it_num_cmp);
      else
	qsort((void *)it_array, sidx, sizeof(struct item *), itn_num_cmp);
    } else if (so == AL_SORT_DIC) {
      qsort((void *)it_array, sidx, sizeof(struct item *), it_cmp);
    } else {
      qsort((void *)it_array, sidx, sizeof(struct item *), itn_cmp);
    }

    ip->sorted = it_array;
    ip->oindex = sidx;	// max index
  } else {
    /* AL_SORT_NO */

    unsigned int index = 0;
    unsigned int old_size = hash_size(ht->hash_bit - 1);
    unsigned int total_size = old_size + hash_size(ht->hash_bit);
    struct item **place = NULL;

    if (ht->rehashing) {
      index = ht->rehashing_front;
      place = &ht->hash_table_old[ht->rehashing_front];
    } else {
      index = old_size;
      place = &ht->hash_table[0];
    }
    while (! *place) {
      if (++index == old_size)
	place = &ht->hash_table[0];
      if (index == total_size) {
	place = NULL;
	break;
      }
      place++;
    }
    ip->index = index;
    ip->place = place;
  }

  ip->hi_flag = ht->h_flag | (flag & AL_ITER_AE);
  ip->ht = ht;
  *iterp = ip;
  attach_iter(ht, ip);
  return 0;
}

static int
advance_iter(struct al_hash_iter_t *iterp, struct item **it)
{
  free_to_be_free(iterp);

  struct al_hash_t *ht = iterp->ht;
  if (!ht || !ht->iterators) return -4;

  unsigned int index = iterp->index;

  if (iterp->sorted) {
    if (iterp->oindex <= index) return -1;
    *it = iterp->sorted[index];

    iterp->index++;
    return 0;
  }

  struct item **place = iterp->place;
  if (!place || !*place) {
    iterp->pplace = iterp->place = NULL;
    return -1;
  }

  unsigned int old_size = hash_size(ht->hash_bit - 1);
  unsigned int total_size = old_size + hash_size(ht->hash_bit);
  *it = *place;

  iterp->oindex = index;
  iterp->pplace = iterp->place;
  place = &(*place)->chain;

  while (!*place) {
    if (++index < old_size) {
      place = &ht->hash_table_old[index];
    } else if (index < total_size) {
      place = &ht->hash_table[index - old_size];
    } else {
      place = NULL;
      break;
    }
  }
  iterp->place = place;
  iterp->index = index;
  return 0;
}

static void
check_hash_iter_ae(struct al_hash_iter_t *iterp, int ret) {
  if (iterp->hi_flag & ITER_FLAG_AE) { // auto end
    if (ret == -1) { // normal end
      al_hash_iter_end(iterp);
    } else {
      const char *msg = "";
      if (iterp->ht)
	msg = iterp->ht->err_msg;
      fprintf(stderr, "hash_iter %s advance error (code=%d)\n", msg, ret);
    }
  }
}

int
al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v)
{
  int ret = 0;
  struct item *it;
  if (!iterp || !key) return -3;

  *key = NULL;
  ret = advance_iter(iterp, &it);
  if (!ret) {
    *key = it->key;
    if (ret_v) *ret_v = it->u.value;
  } else {
    check_hash_iter_ae(iterp, ret);
  }
  return ret;
}

int
al_hash_iter_str(struct al_hash_iter_t *iterp, const char **key, link_value_t *ret_v)
{
  int ret = 0;
  struct item *it;
  if (!iterp || !key) return -3;

  *key = NULL;
  ret = advance_iter(iterp, &it);
  if (!ret) {
    *key = it->key;
    if (ret_v) *ret_v = it->u.cstr;
  } else {
    check_hash_iter_ae(iterp, ret);
  }
  return ret;
}

int
al_hash_iter_pointer(struct al_hash_iter_t *iterp, const char **key, void **ret_v)
{
  int ret = 0;
  struct item *it;
  if (!iterp || !key) return -3;

  *key = NULL;
  ret = advance_iter(iterp, &it);
  if (!ret) {
    *key = it->key;
    if (ret_v) *ret_v = it->u.ptr;
  } else {
    check_hash_iter_ae(iterp, ret);
  }
  return ret;
}

int
al_hash_iter_end(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  free_to_be_free(iterp);

  detach_iter(iterp->ht, iterp);

#if 1 <= AL_WARN
  if (iterp->n_value_iter)
    fprintf(stderr, "WARN: al_hash_iter_end, %u value iterators still exists on iterp(%p)\n",
	    iterp->n_value_iter, iterp);
#endif

  if (iterp->sorted)
    free((void *)iterp->sorted);
  free((void *)iterp);

  return 0;
}

int
item_replace_iter(struct al_hash_iter_t *iterp, value_t v)
{
  if (!iterp) return -3;
  if (!iterp->ht || !iterp->ht->iterators) return -4;

  if (iterp->sorted) {
    unsigned int index = iterp->index;
    if (index == 0 || iterp->oindex < index || iterp->to_be_free) return -1;
    struct item *it = iterp->sorted[index - 1];
    if (!it) return -1;
    it->u.value = v;
  } else {
    if (!iterp->pplace) return -1;
    (*iterp->pplace)->u.value = v;
  }
  return 0;
}

static int
del_sorted_iter(struct al_hash_iter_t *iterp)
{
  unsigned int index = iterp->index;
  if (index == 0 || iterp->oindex < index || iterp->to_be_free) return -1;
  struct item *it = iterp->sorted[index - 1];
  if (!it) return -1;
  it = hash_delete(iterp->ht, it->key, al_hash_fn_i(it->key));
  if (!it) return -1;
  iterp->to_be_free = it;
  return 0;
}

/* scalar, linked and pqueue ht acceptable */
int
item_delete_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  if (!iterp->ht || !iterp->ht->iterators) return -4;

#if 1 <= AL_WARN
  if (iterp->ht->iterators->chain)
    fprintf(stderr, "WARN: iterm_delete_iter, other iterators exists on ht(%p)\n",
	    iterp->ht);
#endif
  if (iterp->sorted)
    return del_sorted_iter(iterp);

  if (!iterp->pplace) return -1;
  struct item *p_it = *iterp->pplace;
  if (!p_it) return -1;

  unsigned int old_size = hash_size(iterp->ht->hash_bit - 1);

  if ((void *)p_it == (void *)iterp->place)
    iterp->place = iterp->pplace;

  *iterp->pplace = p_it->chain;

  iterp->to_be_free = p_it;
  iterp->pplace = NULL; /* avoid double free */
  if (iterp->oindex < old_size)
    iterp->ht->n_entries_old--;
  else
    iterp->ht->n_entries--;
  return 0;
}

int
al_hash_n_iterators(struct al_hash_t *ht)
{
  int ret = 0;
  if (!ht) return -3;

  struct al_hash_iter_t *ip;
  for (ip = ht->iterators; ip; ip = ip->chain)
    ret++;
  return ret;
}

struct al_hash_t *
al_hash_iter_ht(struct al_hash_iter_t *iterp)
{
  return iterp ? iterp->ht : NULL;
}

static void
attach_value_iter(struct al_hash_iter_t *iterp)
{
  if (iterp)
    ++iterp->n_value_iter;
#if 2 <= AL_WARN
  if (2 <= iterp->n_value_iter)
    fprintf(stderr, "WARN: value_iter, other value iterators exists on iterp(%p)\n", iterp);
#endif
}

static void
detach_value_iter(struct al_hash_iter_t *iterp)
{
  if (iterp)
    --iterp->n_value_iter;
}

/**** iterator of value of linked hash ***/

static int
v_cmp(const void *a, const void *b)
{
  return strcmp(*(link_value_t *)a, *(link_value_t *)b);
}

static int
vn_cmp(const void *a, const void *b)
{
  return -strcmp(*(link_value_t *)a, *(link_value_t *)b);
}

static int
v_num_cmp(const void *a, const void *b)
{
  return str_num_cmp(*(link_value_t *)a, *(link_value_t *)b);
}

static int
vn_num_cmp(const void *a, const void *b)
{
  return -str_num_cmp(*(link_value_t *)a, *(link_value_t *)b);
}

void
sort_sarray(link_value_t *sarray, unsigned int nvalue, int flag)
{
  int so = flag & HASH_FLAG_SORT_ORD;
  if (flag & AL_SORT_NUMERIC) {
    if (so == AL_SORT_DIC)
      qsort((void *)sarray, nvalue, sizeof(link_value_t *), v_num_cmp);
    else
      qsort((void *)sarray, nvalue, sizeof(link_value_t *), vn_num_cmp);
  } if (so == AL_SORT_DIC) {
    qsort((void *)sarray, nvalue, sizeof(link_value_t *), v_cmp);
  } else {
    qsort((void *)sarray, nvalue, sizeof(link_value_t *), vn_cmp);
  }
}

static int
mk_linked_hash_iter(struct item *it, struct al_hash_iter_t *iterp,
		    struct al_linked_value_iter_t **v_iterp, int flag)
{
  int so = flag & HASH_FLAG_SORT_ORD;

  if (!v_iterp) return 0;
  *v_iterp = NULL;

  struct al_linked_value_iter_t *vip =
    (struct al_linked_value_iter_t *)calloc(1, sizeof(struct al_linked_value_iter_t));
  if (!vip) return -2;

  if (so == AL_SORT_NO || !it->u.link->link) { // no sort, or one value entry only
    vip->li_link = vip->li_current = it->u.link;
    vip->li_max_index = so ? 1 : 0;
  } else {
    unsigned int nvalue = vip->li_max_index;
    link_t *lp;
    if (nvalue == 0) { // al_linked_hash_nvalue() set vip->max_index as correct value
      for (lp = it->u.link; lp; lp = lp->link)
	nvalue++;	
    }
    link_value_t *sarray = (link_value_t *)malloc(nvalue * sizeof(link_value_t *));
    if (!sarray) {
      free((void *)vip);
      return -2;
    }
    vip->li_max_index = nvalue;
    for (nvalue = 0, lp = it->u.link; lp; lp = lp->link)
      sarray[nvalue++] = lp->value;

    sort_sarray(sarray, nvalue, flag);
    vip->li_sorted = sarray;
  }
  vip->li_flag = flag & AL_ITER_AE;
  vip->li_pitr = iterp;
  attach_value_iter(iterp);
  *v_iterp = vip;

  return 0;
}

int al_linked_hash_get(struct al_hash_t *ht, char *key,
		       struct al_linked_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  int so = flag & HASH_FLAG_SORT_ORD;
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_LINKED)) return -6;
  if (so < AL_SORT_NO || AL_SORT_COUNTER_DIC < so) return -7;

  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) return -1;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;

  ip->hi_flag = ITER_FLAG_VIRTUAL;
  ip->ht = ht;

  ret = mk_linked_hash_iter(it, ip, v_iterp, flag);
  if (ret) {
    free((void *)ip);
    return ret;
  }
  attach_iter(ht, ip);

  return 0;
}

int
al_linked_hash_iter(struct al_hash_iter_t *iterp, const char **key,
		    struct al_linked_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  int so = flag & HASH_FLAG_SORT_ORD;
  if (!iterp || !key) return -3;

  *key = NULL;
  if (!(iterp->hi_flag & HASH_FLAG_LINKED)) return -6;
  if (so < AL_SORT_NO || AL_SORT_COUNTER_DIC < so) return -7;

  struct item *it;

  ret = advance_iter(iterp, &it);
  if (ret) {
    check_hash_iter_ae(iterp, ret);
    return ret;
  }

  *key = it->key;
  return mk_linked_hash_iter(it, iterp, v_iterp, flag);
}

/* advance iterator */
int
al_linked_value_iter(struct al_linked_value_iter_t *v_iterp, link_value_t *ret_v)
{	
  link_value_t lv = NULL;
  if (!v_iterp) return -3;

  if (v_iterp->li_sorted) {
    if (v_iterp->li_index < v_iterp->li_max_index)
      lv = v_iterp->li_sorted[v_iterp->li_index++];
  } else {
    if (v_iterp->li_current) {
      link_t *rp = v_iterp->li_current;
      v_iterp->li_current = rp->link;
      lv = rp->value;
    }
  }

  if (lv) {
    if (ret_v)
      *ret_v = lv;
    return 0;
  }

  if (v_iterp->li_flag & AL_ITER_AE)
    al_linked_value_iter_end(v_iterp);

  return -1;
}

int
al_linked_value_iter_end(struct al_linked_value_iter_t *vip)
{
  if (!vip) return -3;
  detach_value_iter(vip->li_pitr);
  if (vip->li_pitr->hi_flag & ITER_FLAG_VIRTUAL) {
    detach_iter(vip->li_pitr->ht, vip->li_pitr);
    free((void *)vip->li_pitr);
  }
  if (vip->li_sorted)
    free((void *)vip->li_sorted);
  free((void *)vip);
  return 0;
}

int
al_linked_hash_nvalue(struct al_linked_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->li_max_index)
    return vip->li_max_index;
  unsigned int nvalue = 0;
  link_t *lp;
  for (lp = vip->li_link; lp; lp = lp->link)
    nvalue++;
  vip->li_max_index = nvalue;
  return nvalue;
}

int
al_linked_hash_rewind_value(struct al_linked_value_iter_t *vip)
{
  if (!vip) return -3;
  if (!vip->li_sorted || vip->li_max_index == 1)
    vip->li_current = vip->li_link;
  else
    vip->li_index = 0;

  return 0;
}

int
al_is_linked_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  return ht->h_flag & HASH_FLAG_LINKED ? 0 : -1;
}

int
al_is_linked_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  return iterp->hi_flag & HASH_FLAG_LINKED ? 0 : -1;
}

/**** iterator of value of lcdr hash ***/

static int
mk_lcdr_hash_iter(struct item *it, struct al_hash_iter_t *iterp,
		  struct al_lcdr_value_iter_t **v_iterp, int flag)
{
  int so = flag & HASH_FLAG_SORT_ORD;

  if (!v_iterp) return 0;
  *v_iterp = NULL;

  struct al_lcdr_value_iter_t *vip =
    (struct al_lcdr_value_iter_t *)calloc(1, sizeof(struct al_lcdr_value_iter_t));
  if (!vip) return -2;

  if (so == AL_SORT_NO) {
    vip->cd_link = vip->cd_current = it->u.lcdr;
  } else {
    unsigned int nvalue = vip->cd_max_index;
    lcdr_t *dp;
    if (nvalue == 0) { // al_lcd_hash_nvalue() set vip->cd_max_index as correct value
      for (dp = it->u.lcdr; dp; dp = dp->link)
	nvalue += dp->va_used;
    }
    link_value_t *sarray = (link_value_t *)malloc(nvalue * sizeof(link_value_t *));
    if (!sarray) {
      free((void *)vip);
      return -2;
    }
    vip->cd_max_index = nvalue;

    for (nvalue = 0, dp = it->u.lcdr; dp; dp = dp->link) {
      unsigned int i;
      for (i = 0; i < dp->va_used; i++)
	sarray[nvalue++] = dp->va[i];
    }

    sort_sarray(sarray, nvalue, flag);
    vip->cd_sorted = sarray;
  }

  vip->cd_flag = flag & AL_ITER_AE;
  vip->cd_pitr = iterp;
  attach_value_iter(iterp);
  *v_iterp = vip;

  return 0;
}

int al_lcdr_hash_get(struct al_hash_t *ht, char *key,
		     struct al_lcdr_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  int so = flag & HASH_FLAG_SORT_ORD;
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_LCDR)) return -6;
  if (so < AL_SORT_NO || AL_SORT_COUNTER_DIC < so) return -7;

  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) return -1;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;

  ip->hi_flag = ITER_FLAG_VIRTUAL;
  ip->ht = ht;

  ret = mk_lcdr_hash_iter(it, ip, v_iterp, flag);
  if (ret) {
    free((void *)ip);
    return ret;
  }
  attach_iter(ht, ip);

  return 0;
}

int
al_lcdr_hash_iter(struct al_hash_iter_t *iterp, const char **key,
		  struct al_lcdr_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  int so = flag & HASH_FLAG_SORT_ORD;
  if (!iterp || !key) return -3;

  *key = NULL;
  if (!(iterp->hi_flag & HASH_FLAG_LCDR)) return -6;
  if (so < AL_SORT_NO || AL_SORT_COUNTER_DIC < so) return -7;

  struct item *it;

  ret = advance_iter(iterp, &it);
  if (ret) {
    check_hash_iter_ae(iterp, ret);
    return ret;
  }

  *key = it->key;
  return mk_lcdr_hash_iter(it, iterp, v_iterp, flag);
}

/* advance iterator */
int
al_lcdr_value_iter(struct al_lcdr_value_iter_t *v_iterp, link_value_t *ret_v)
{	
  link_value_t lv = NULL;
  if (!v_iterp) return -3;

  if (v_iterp->cd_sorted) {
    if (v_iterp->cd_index < v_iterp->cd_max_index)
      lv = v_iterp->cd_sorted[v_iterp->cd_index++];
  } else {
    while (v_iterp->cd_current) {
      lcdr_t *dp = v_iterp->cd_current;
      if (v_iterp->cd_cindex < dp->va_used) {
	lv = dp->va[v_iterp->cd_cindex++];
	break;
      }
      v_iterp->cd_cindex = 0;
      v_iterp->cd_current = dp->link;
    }
  }

  if (lv) {
    if (ret_v)
      *ret_v = lv;
    return 0;
  }

  if (v_iterp->cd_flag & AL_ITER_AE)
    al_lcdr_value_iter_end(v_iterp);

  return -1;
}

int
al_lcdr_value_iter_end(struct al_lcdr_value_iter_t *vip)
{
  if (!vip) return -3;

  detach_value_iter(vip->cd_pitr);
  if (vip->cd_pitr->hi_flag & ITER_FLAG_VIRTUAL) {
    detach_iter(vip->cd_pitr->ht, vip->cd_pitr);
    free((void *)vip->cd_pitr);
  }
  if (vip->cd_sorted)
    free((void *)vip->cd_sorted);
  free((void *)vip);

  return 0;
}

int
al_lcdr_hash_nvalue(struct al_lcdr_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->cd_max_index)
    return vip->cd_max_index;
  unsigned int nvalue = 0;
  lcdr_t *dp;
  for (dp = vip->cd_link; dp; dp = dp->link)
    nvalue += dp->va_used;
  vip->cd_max_index = nvalue;
  return nvalue;
}

int
al_lcdr_hash_rewind_value(struct al_lcdr_value_iter_t *vip)
{
  if (!vip) return -3;
  if (!vip->cd_sorted) {
    vip->cd_current = vip->cd_link;
    vip->cd_cindex = 0;
  } else {
    vip->cd_index = 0;
  }
  return 0;
}

int
al_is_lcdr_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  return ht->h_flag & HASH_FLAG_LCDR ? 0 : -1;
}

int
al_is_lcdr_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  return iterp->hi_flag & HASH_FLAG_LCDR ? 0 : -1;
}

/**** iterator of priority queue ***/

static int
mk_pqeueu_hash_iter(struct item *it, struct al_hash_iter_t *iterp,
		    struct al_pqueue_value_iter_t **v_iterp, int flag)
{
  int ret;
  if (!v_iterp) return 0;
  *v_iterp = NULL;

  struct al_pqueue_value_iter_t *vip =
    (struct al_pqueue_value_iter_t *)calloc(1, sizeof(struct al_pqueue_value_iter_t));
  if (!vip) return -2;

  vip->sl = it->u.skiplist;
  ret = al_sl_iter_init(vip->sl, &vip->sl_iter, AL_FLAG_NONE);
  if (ret) {
    free((void *)vip);
    return ret;
  }
  vip->pi_flag = flag & AL_ITER_AE;
  vip->pi_pitr = iterp;
  attach_value_iter(iterp);
  *v_iterp = vip;
  return 0;
}

int al_pqueue_hash_get(struct al_hash_t *ht, char *key,
		       struct al_pqueue_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  if (!ht || !key) return -3;
  if ((flag & ~AL_ITER_AE) != 0) return -7;  // only AL_ITER_AE is a valid flag.
  if (!(ht->h_flag & HASH_FLAG_PQ)) return -6;

  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) return -1;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;

  ip->hi_flag = ITER_FLAG_VIRTUAL;
  ip->ht = ht;

  ret = mk_pqeueu_hash_iter(it, ip, v_iterp, flag);
  if (ret) {
    free((void *)ip);
    return ret;
  }

  attach_iter(ht, ip);

  return 0;
}

int al_pqueue_hash_iter(struct al_hash_iter_t *iterp, const char **key,
			struct al_pqueue_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  if (!iterp || !key) return -3;
  if ((flag & ~AL_ITER_AE) != 0) return -7;  // only AL_ITER_AE is a valid flag.

  *key = NULL;
  if (!(iterp->hi_flag & HASH_FLAG_PQ)) return -6;

  struct item *it;
  ret = advance_iter(iterp, &it);
  if (ret) {
    check_hash_iter_ae(iterp, ret);
    return ret;
  }

  *key = it->key;

  return mk_pqeueu_hash_iter(it, iterp, v_iterp, flag);
}

int
al_pqueue_value_iter(struct al_pqueue_value_iter_t *vip,
		     link_value_t *keyp, value_t *ret_count)
{	
  if (!vip) return -3;
  int ret = al_sl_iter(vip->sl_iter, keyp, ret_count);
  if (ret && (vip->pi_flag & AL_ITER_AE)) { // auto end
    if (ret == -1) { // normal end
      al_pqueue_value_iter_end(vip);
    } else {
      const char *msg = "";
      if (vip->pi_pitr && vip->pi_pitr->ht)
	msg = vip->pi_pitr->ht->err_msg;
      fprintf(stderr, "pqueue_value_iter %s advance error (code=%d)\n", msg, ret);
    }
  }
  return ret;
}

int al_pqueue_value_iter_end(struct al_pqueue_value_iter_t *vip)
{
  if (!vip) return -3;
  al_sl_iter_end(vip->sl_iter);
  detach_value_iter(vip->pi_pitr);
  if (vip->pi_pitr->hi_flag & ITER_FLAG_VIRTUAL) {
    detach_iter(vip->pi_pitr->ht, vip->pi_pitr);
    free((void *)vip->pi_pitr);
  }
  free((void *)vip);
  return 0;
}

int
al_pqueue_hash_nvalue(struct al_pqueue_value_iter_t *vip)
{
  if (!vip) return -3;
  return sl_n_entries(vip->sl);
}

int al_pqueue_hash_rewind_value(struct al_pqueue_value_iter_t *vip)
{
  if (!vip) return -3;
  return al_sl_rewind_iter(vip->sl_iter);
}

int
al_is_pqueue_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  return ht->h_flag & HASH_FLAG_PQ ? 0 : -1;
}

int
al_is_pqueue_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  return iterp->hi_flag & HASH_FLAG_PQ ? 0 : -1;
}


/************************/

/* either scalar and linked ht acceptable */

int
item_key(struct al_hash_t *ht, char *key)
{
  if (!ht || !key) return -3;
  unsigned int hv = al_hash_fn_i(key);
  struct item *retp = hash_find(ht, key, hv);
  return retp ? 0 : -1;
}

int
item_get(struct al_hash_t *ht, char *key, value_t *v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);

  if (it) {
    if (v)
      *v = it->u.value;
    return 0;
  }
  return -1;
}

int
item_get_str(struct al_hash_t *ht, char *key, link_value_t *v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_STRING)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);

  if (it) {
    if (v)
      *v = it->u.cstr;
    return 0;
  }
  return -1;
}

int
item_get_pointer(struct al_hash_t *ht, char *key, void **v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_POINTER)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);

  if (it) {
    if (v)
      *v = it->u.ptr;
    return 0;
  }
  return -1;
}

int
item_set(struct al_hash_t *ht, char *key, value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->u.value = v;
    return 0;
  }
  return hash_v_insert(ht, hv, key, v);
}

#ifdef ITEM_PV
int
item_set_pv(struct al_hash_t *ht, char *key, value_t v, value_t *ret_pv)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    if (ret_pv)
      *ret_pv = it->u.value;
    it->u.value = v;
    return 0;
  }
  return hash_v_insert(ht, hv, key, v);
}
#endif

int
item_set_str(struct al_hash_t *ht, char *key, link_value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_STRING)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  char *dp = strdup(v);
  if (!dp) return -2;

  if (it) {
    al_free_linked_value(it->u.cstr);
    it->u.cstr = dp;
    return 0;
  }
  return hash_v_insert(ht, hv, key, (intptr_t)dp);
}

int
item_set_pointer2(struct al_hash_t *ht, char *key, void *v, unsigned int size, void **ret_v)
{
  int ret = 0;
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_POINTER)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);

  void *ptr;
  if (ht->dup_p) {
    ret = ht->dup_p(v, size, &ptr);
    if (ret) return ret;
  } else {
    ptr = malloc(size);
    if (!ptr) return -2;
    memcpy(ptr, v, size);
  }
  if (it) {
    if (ht->free_p)
      ht->free_p(it->u.ptr);
    else
      free(it->u.ptr);
    it->u.ptr = ptr;
  } else {
    ret = hash_v_insert(ht, hv, key, (intptr_t)ptr);
  }
  if (!ret && ret_v)
    *ret_v = ptr;
  return ret;
}

inline int
item_set_pointer(struct al_hash_t *ht, char *key, void *v, unsigned int size)
{
  return item_set_pointer2(ht, key, v, size, NULL);
}

int
item_replace(struct al_hash_t *ht, char *key, value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->u.value = v;
    return 0;
  }
  return -1;
}

#ifdef ITEM_PV
int
item_replace_pv(struct al_hash_t *ht, char *key, value_t v, value_t *ret_pv)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    if (ret_pv)
      *ret_pv = it->u.value;
    it->u.value = v;
    return 0;
  }
  return -1;
}
#endif

int
item_replace_str(struct al_hash_t *ht, char *key, link_value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_STRING)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  char *dp = strdup(v);
  if (!dp) return -2;
  if (it) {
    al_free_linked_value(it->u.cstr);
    it->u.cstr = dp;
    return 0;
  }
  return -1;
}

/* either scalar and linked ht acceptable */
int
item_delete(struct al_hash_t *ht, char *key)
{
  if (!ht || !key) return -3;
  if (ht->iterators) {
#if 1 <= AL_WARN
    fprintf(stderr, "WARN: iterm_delete, other iterators exists on ht(%p)\n", ht);
#endif
    return -5;
  }
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_delete(ht, key, hv);
  if (it) {
    free_value(ht, it);
    free((void *)it->key);
    free((void *)it);
    return 0;
  }
  return -1;
}

#ifdef ITEM_PV
int
item_delete_pv(struct al_hash_t *ht, char *key, value_t *ret_pv)
{
  if (!ht || !key) return -3;
  if (ht->iterators) {
#if 1 <= AL_WARN
    fprintf(stderr, "WARN: iterm_delete_pv, other iterators exists on ht(%p)\n", ht);
#endif
    return -5;
  }
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_delete(ht, key, hv);
  if (it) {
    if (ret_pv)
      *ret_pv = it->u.value;
    free_value(ht->h_flag, it);
    free((void *)it->key);
    free((void *)it);
    return 0;
  }
  return -1;
}
#endif

int
item_inc(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) return -1;

  it->u.value += off;
  if (ret_v)
    *ret_v = it->u.value;
  return 0;
}

int
item_inc_init(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it)
    return hash_v_insert(ht, hv, key, (value_t)off);

  it->u.value += off;
  if (ret_v)
    *ret_v = it->u.value;
  return 0;
}

static int
add_value_to_pq(struct al_hash_t *ht, char *key, link_value_t v)
{
  int ret = 0;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) /* found, insert value part to sl */
    return sl_inc_init_n(it->u.skiplist, v, 1, NULL, ht->pq_max_n);

  it = (struct item *)malloc(sizeof(struct item));
  if (!it) return -2;

  struct al_skiplist_t *sl;
  ret = al_create_skiplist(&sl, ht->h_flag & HASH_FLAG_SORT_MASK);
  if (ret) goto free;

  it->u.skiplist = sl;
  ret = sl_inc_init_n(it->u.skiplist, v, 1, NULL, ht->pq_max_n);
  if (ret) goto free;

  it->key = strdup(key);
  if (!it->key) { ret = -2; goto free; }

  ret = hash_insert(ht, hv, key, it);
  if (ret) goto free_key;

  return 0;

  /* error return */
 free_key:
  free((void *)it->key);
 free:
  free((void *)it);
  al_free_skiplist(sl);
  return ret;
}

static int
add_value_to_lcdr(struct al_hash_t *ht, char *key, link_value_t v)
{
  link_value_t lv;
  int ret = al_link_value(v, &lv);  // ex. strdup(v)
  if (ret) return ret;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  lcdr_t *ndp = NULL;

  ret = -2;
  if (it) {
    lcdr_t *dp = it->u.lcdr;
    if (dp->va_size <= dp->va_used) {
      int sz = dp->va_size;
      if (sz < LCDR_SIZE_U)
	sz <<= 1;
      ndp = (lcdr_t *)calloc(1, sizeof(lcdr_t) + (sz - 1) * sizeof(link_value_t));
      if (!ndp) goto free_lv;

      ndp->va_size = sz;
      ndp->link = dp;
      it->u.lcdr = ndp;
      dp = ndp;
    }
    dp->va[dp->va_used++] = lv;
    return 0;
  }
  ndp = (lcdr_t *)calloc(1, sizeof(lcdr_t) + (LCDR_SIZE_L - 1) * sizeof(link_value_t));
  if (!ndp) goto free_lv;

  it = (struct item *)malloc(sizeof(struct item));
  if (!it) goto free_ndp;

  it->key = strdup(key);
  if (!it->key) goto free_it;

  ndp->va_size = LCDR_SIZE_L;
  ndp->va[ndp->va_used++] = lv;
  it->u.lcdr = ndp;

  ret = hash_insert(ht, hv, key, it);
  if (ret) goto free_key;

  return 0;

  /* error return */
 free_key:
    free((void *)it->key);
 free_it:
    free((void *)it);
 free_ndp:
    free((void *)ndp);
 free_lv:
    al_free_linked_value(lv);

    return ret;
}

int
item_add_value(struct al_hash_t *ht, char *key, link_value_t v)
{
  int ret = 0;
  if (!ht || !key) return -3;

  if (ht->h_flag & HASH_FLAG_PQ)
    return add_value_to_pq(ht, key, v);

  if (ht->h_flag & HASH_FLAG_LCDR)
    return add_value_to_lcdr(ht, key, v);

  if (!(ht->h_flag & HASH_FLAG_LINKED)) return -6;

  link_t *lp = (link_t *)malloc(sizeof(link_t));
  if (!lp) return -2;

  ret = al_link_value(v, &lp->value);  // ex. strdup(v)
  if (ret) {
    free((void *)lp);
    return ret;
  }

  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) { /* found, add link */
    lp->link = it->u.link;
    it->u.link = lp;
    return 0;
  }

  ret = -2;
  it = (struct item *)malloc(sizeof(struct item));
  if (!it) goto free_lp;

  it->key = strdup(key);
  if (!it->key) goto free;

  lp->link = NULL;
  it->u.link = lp;

  ret = hash_insert(ht, hv, key, it);
  if (ret) goto free_key;

  return 0;

  /* error return */
 free_key:
  free((void *)it->key);
 free:
  free((void *)it);
 free_lp:
  al_free_linked_value(lp->value);
  free((void *)lp);

  return ret;
}

/***/

static void
count_chain(al_chain_length_t acl,
	    struct item **itp, unsigned int start, unsigned int size)
{
  unsigned int i;
  for (i = start; i < size; i++) {
    int count = 0;
    struct item *it;
    for (it = itp[i]; it; it = it->chain)
      count++;
    if (count < 10)
      acl[count]++;
    else
      acl[10]++;
  }
}

int
al_hash_stat(struct al_hash_t *ht,
	     struct al_hash_stat_t *statp,
	     al_chain_length_t acl)
{
  if (!ht || !statp) return -3;

  statp->al_hash_bit = ht->hash_bit;
  statp->al_n_entries = ht->n_entries;
  statp->al_n_entries_old = ht->n_entries_old;
  statp->al_n_cancel_rehashing = ht->n_cancel_rehashing;
  statp->al_n_rehashing = ht->n_rehashing;

  if (!acl) return 0;

  memset((void *)acl, 0, sizeof(al_chain_length_t));

  if (ht->rehashing) {
    count_chain(acl, ht->hash_table_old, ht->rehashing_front,
		hash_size(ht->hash_bit - 1));
  }
  count_chain(acl, ht->hash_table, 0, hash_size(ht->hash_bit));

  return 0;
}

int
al_out_hash_stat(struct al_hash_t *ht, const char *title)
{
  int ret = 0;
  struct al_hash_stat_t stat = {0, 0, 0, 0, 0};
  al_chain_length_t acl;
  ret = al_hash_stat(ht, &stat, acl);
  if (ret) return ret;
  fprintf(stderr, "%s bit %u  nitem %lu  oitem %lu  rehash %d  cancel %lu\n",
	  title,
	  stat.al_hash_bit,
	  stat.al_n_entries,
	  stat.al_n_entries_old,
	  stat.al_n_rehashing,
	  stat.al_n_cancel_rehashing);
  int i;
  for (i = 0; i < 11; i++) fprintf(stderr, "%9d", i);
  fprintf(stderr, "\n");
  for (i = 0; i < 11; i++) fprintf(stderr, "%9lu", acl[i]);
  fprintf(stderr, "\n");
  return 0;
}

int
al_set_hash_err_msg_impl(struct al_hash_t *ht, const char *msg)
{
  if (!ht) return -3;
  ht->err_msg = msg;
  return 0;
}

/*
 *  priority queue, implemented by skiplist
 */

#define SL_MAX_LEVEL 31
#undef SL_FIRST_KEY
#define SL_LAST_KEY

struct slnode_t {
  pq_key_t key;
  union {
    pq_value_t value;
  } u;
  struct slnode_t *forward[1]; /* variable sized array of forward pointers */
};

struct al_skiplist_t {
  struct slnode_t *head;
#ifdef SL_FIRST_KEY
  pq_key_t first_key;
#endif
#ifdef SL_LAST_KEY
  // pq_key_t last_key;
  struct slnode_t *last_node;
#endif
  unsigned long n_entries;
  int level;
  int sort_order;
  const char *err_msg;
};

struct al_skiplist_iter_t {
  struct al_skiplist_t *sl_p;
  struct slnode_t *current_node;
  int sl_flag;  // ITER_FLAG_AE bit only
};

static int
pq_k_cmp(struct al_skiplist_t *sl, pq_key_t a, pq_key_t b)
{
  if (sl->sort_order & HASH_FLAG_SORT_NUMERIC) {
    if (sl->sort_order & HASH_FLAG_PQ_SORT_DIC)
      return str_num_cmp(a, b);
    else
      return -str_num_cmp(a, b);
  }
  if (sl->sort_order & HASH_FLAG_PQ_SORT_DIC)
    return strcmp(a, b);
  else
    return -strcmp(a, b);
}

int
sl_skiplist_stat(struct al_skiplist_t *sl)
{
  struct slnode_t *np;
  int i;

  if (!sl) return -3;
  fprintf(stderr, "level %d  n_entries %lu  last '%s'\n",
	  sl->level, sl->n_entries, sl->last_node->key);
  for (i = 0; i < sl->level; i++) {
    long count = 0;
    fprintf(stderr, "[%02d]: ", i);

    for (np = sl->head->forward[i]; np; np = np->forward[i]) {
      // fprintf(stderr, "%s/%d(%p) ", np->key, np->nlevel, np);
      count++;
    }
    fprintf(stderr, "%ld\n", count);
  }

  return 0;
}

int
sl_set_skiplist_err_msg(struct al_skiplist_t *sl, const char *msg)
{
  if (!sl) return -3;
  sl->err_msg = msg;
  return 0;
}



static struct slnode_t *
find_node(struct al_skiplist_t *sl, pq_key_t key, struct slnode_t *update[])
{
  int i;
  struct slnode_t *np = sl->head;
  for (i = sl->level - 1; 0 <= i; --i) {
    while (np->forward[i]) {
      int c = pq_k_cmp(sl, np->forward[i]->key, key);
      if (c < 0) {
	np = np->forward[i];
      } else if (c == 0) { // key found
	return np->forward[i];
      } else {
	break;
      }
    }
    update[i] = np;
  }
  return NULL;
}

static struct slnode_t *
mk_node(int level, pq_key_t key)
{
  struct slnode_t *np = (struct slnode_t *)calloc(1, sizeof(struct slnode_t) + level * sizeof(struct slnode_t *));
  if (!np) return NULL;

  np->key = strdup(key);
  if (!np->key) {
    free((void *)np);
    return NULL;
  }

  return np;
}

int
al_create_skiplist(struct al_skiplist_t **slp, int sort_order)
{
  if (!slp) return -3;
  int so = sort_order & (~AL_SORT_NUMERIC);
  if (so != AL_SORT_DIC && so != AL_SORT_COUNTER_DIC) return -7;

  struct al_skiplist_t *sl = (struct al_skiplist_t *)calloc(1, sizeof(struct al_skiplist_t));
  if (!sl) return -2;

  sl->head = mk_node(SL_MAX_LEVEL, "");
  if (!sl->head) {
    free((void *)sl);
    return -2;
  }
  sl->head->u.value = 0;

  sl->level = 1;
  sl->sort_order = sort_order;
#ifdef SL_FIRST_KEY
  sl->first_key = "";
#endif
#ifdef SL_LAST_KEY
  sl->last_node = sl->head;
#endif
  sl->n_entries = 0;
  *slp = sl;
  return 0;
}

int
al_free_skiplist(struct al_skiplist_t *sl)
{
  if (!sl) return -3;

  struct slnode_t *np, *next;
  np = sl->head->forward[0];

  while (np) {
    next = np->forward[0];
    free((void *)np->key);
    free((void *)np);
    np = next;
  }
  free((void *)sl->head->key);
  free((void *)sl->head);
  free((void *)sl);
  return 0;
}

static void
sl_delete_node(struct al_skiplist_t *sl, pq_key_t key, struct slnode_t *np, struct slnode_t **update)
{
  int i;
#ifdef SL_FIRST_KEY
  int c = strcmp(sl->first_key, key);  // eq check, insted of pq_k_cmp()
#endif

  for (i = 0; i < sl->level; i++)
    if (update[i]->forward[i] == np)
      update[i]->forward[i] = np->forward[i];

#ifdef SL_LAST_KEY
  if (update[0]->forward[0] == NULL) {
    sl->last_node = update[0];
  }
#endif
  for (--i; 0 <= i; --i) {
    if (sl->head->forward[i] == NULL)
      sl->level--;
  }

  free((void *)np->key);
  free((void *)np);
  sl->n_entries--;
#ifdef SL_FIRST_KEY
  if (c == 0) {
    if (sl->head->forward[0] == NULL)
      sl->first_key = "";
    else
      sl->first_key = sl->head->forward[0]->key;
  }
#endif
}

int
sl_delete(struct al_skiplist_t *sl, pq_key_t key)
{
  struct slnode_t *update[SL_MAX_LEVEL], *np;
  int i;

  if (!sl || !key) return -3;

  np = sl->head;
  for (i = sl->level - 1; 0 <= i; --i) {
    while (np->forward[i] && pq_k_cmp(sl, np->forward[i]->key, key) < 0)
      np = np->forward[i];
    update[i] = np;
  }

  np = np->forward[0];
  if (!np || strcmp(np->key, key) != 0) return 0;

  sl_delete_node(sl, key, np, update);
  return 0;
}

#ifdef SL_LAST_KEY
int
sl_delete_last_node(struct al_skiplist_t *sl)
{
  struct slnode_t *update[SL_MAX_LEVEL], *np, *lnode, *p;
  int i;

  if (!sl) return -3;
  lnode = sl->last_node;
  np = sl->head;
  for (i = sl->level - 1; 0 <= i; --i) {  // start from top level
    for (;;) {
      p = np->forward[i];
      if (!p || p == lnode) break;
      np = p;
    }
    update[i] = np;
  }

  np = np->forward[0];
  if (!np || np != lnode) return 0;

  sl_delete_node(sl, sl->last_node->key, np, update);
  return 0;
}
#endif

static int
get_level(pq_key_t key)
{
  int level = 1;
  uint32_t r = al_hash_fn_i(key);
  while (r & 1) { // "r&1": p is 0.5,  "(r&3)==3": p is 0.25, "(r&3)!=0": p is 0.75
    level++;
    r >>= 1;
  }
  return level < SL_MAX_LEVEL ? level : SL_MAX_LEVEL;
}

static int
node_set(struct al_skiplist_t *sl, pq_key_t key, struct slnode_t *update[], struct slnode_t **ret_np)
{
  struct slnode_t *new_node;
  int i, level;

  level = get_level(key);
  new_node = mk_node(level, key);
  if (!new_node) return -2;

  if (sl->level < level) {
    for (i = sl->level; i < level; i++)
      update[i] = sl->head;
    sl->level = level;
  }

  for (i = 0; i < level; i++) {
    new_node->forward[i] = update[i]->forward[i];
    update[i]->forward[i] = new_node;
  }

#ifdef SL_FIRST_KEY
  sl->first_key = sl->head->forward[0]->key;
#endif
#ifdef SL_LAST_KEY
  if (new_node->forward[0] == NULL)
    sl->last_node = new_node;
#endif
  sl->n_entries++;
  *ret_np = new_node;
  return 0;
}

int
sl_set(struct al_skiplist_t *sl, pq_key_t key, value_t v)
{
  int ret = 0;
  struct slnode_t *update[SL_MAX_LEVEL];
  struct slnode_t *np;

  if (!sl || !key) return -3;
  if (!(np = find_node(sl, key, update))) {
    ret = node_set(sl, key, update, &np);
    if (ret) return ret;
  }
  np->u.value = v;
  return 0;
}

#ifdef SL_LAST_KEY
int
sl_set_n(struct al_skiplist_t *sl, pq_key_t key, value_t v, unsigned long max_n)
{
  int ret = 0;
  if (!sl || !key) return -3;

  if (max_n == 0 || sl->n_entries < max_n)
    return sl_set(sl, key, v);

  if (pq_k_cmp(sl, sl->last_node->key, key) <= 0) return 0;
  ret = sl_set(sl, key, v);
  if (ret) return ret;

  while (max_n < sl->n_entries) {
    ret = sl_delete_last_node(sl);
    if (ret) return ret;
  }
  return 0;
}
#endif

int
sl_get(struct al_skiplist_t *sl, pq_key_t key, value_t *ret_v)
{
  if (!sl || !key) return -3;

  struct slnode_t *np;
  struct slnode_t *update[SL_MAX_LEVEL];
  if ((np = find_node(sl, key, update))) {
    if (ret_v)
      *ret_v = np->u.value;
    return 0; // found
  }
  return -1; // not found
}

int
sl_key(struct al_skiplist_t *sl, pq_key_t key)
{
  return sl_get(sl, key, NULL);
}

int
sl_inc_init(struct al_skiplist_t *sl, pq_key_t key, long off, value_t *ret_v)
{
  struct slnode_t *update[SL_MAX_LEVEL];
  struct slnode_t *np;
  int ret = 0;

  if (!sl || !key) return -3;
  if (!(np = find_node(sl, key, update))) {
    ret = node_set(sl, key, update, &np);
    if (ret) return ret;
    np->u.value = (value_t)off;
  } else {
    np->u.value += off;
    if (ret_v)
      *ret_v = np->u.value;
  }
  return 0;
}
#ifdef SL_LAST_KEY
int
sl_inc_init_n(struct al_skiplist_t *sl, pq_key_t key, long off, value_t *ret_v, unsigned long max_n)
{
  int ret = 0;
  if (!sl || !key) return -3;

  if (max_n == 0 || sl->n_entries < max_n)
    return sl_inc_init(sl, key, off, ret_v);

  if (pq_k_cmp(sl, sl->last_node->key, key) < 0) return 0;
  ret = sl_inc_init(sl, key, off, ret_v);
  if (ret) return ret;

  while (max_n < sl->n_entries) {
    ret = sl_delete_last_node(sl);
    if (ret) return ret;
  }
  return 0;
}
#endif

int
sl_empty_p(struct al_skiplist_t *sl)
{
  if (!sl) return -3;
  return sl->head->forward[0] == NULL ? 0 : -1;
}

unsigned long
sl_n_entries(struct al_skiplist_t *sl)
{
  if (!sl) return -3;
  return sl->n_entries;
}

int
al_sl_iter_init(struct al_skiplist_t *sl, struct al_skiplist_iter_t **iterp, int flag)
{
  if (!sl) return -3;
  if ((flag & ~AL_ITER_AE) != 0) return -7;  // only AL_ITER_AE is a valid flag.
  struct al_skiplist_iter_t *itr = (struct al_skiplist_iter_t *)malloc(sizeof(struct al_skiplist_iter_t));
  if (!itr) return -2;
  itr->sl_p = sl;
  itr->current_node = sl->head->forward[0];
  itr->sl_flag = flag & ITER_FLAG_AE;
  *iterp = itr;
  return 0;
}

int
al_sl_iter(struct al_skiplist_iter_t *iterp, pq_key_t *keyp, value_t *ret_v)
{
  int ret = -3;
  if (iterp)
    ret = iterp->current_node ? 0 : -1;
  if (ret) {
    if (!(iterp->sl_flag & ITER_FLAG_AE)) return ret;
    if (ret == -1) {
      al_sl_iter_end(iterp);
    } else {
      const char *msg = "";
      if (iterp->sl_p)
	msg = iterp->sl_p->err_msg;
      fprintf(stderr, "sl_iter %s advance error (code=%d)\n", msg, ret);
    }
    return ret;
  }

  *keyp = iterp->current_node->key;
  if (ret_v)
    *ret_v = iterp->current_node->u.value;
  iterp->current_node = iterp->current_node->forward[0];

  return 0;
}

int
al_sl_iter_end(struct al_skiplist_iter_t *iterp)
{
  if (!iterp) return -3;
  free((void *)iterp);
  return 0;
}

int
al_sl_rewind_iter(struct al_skiplist_iter_t *itr)
{
  if (!itr) return 3;
  struct al_skiplist_t *sl = itr->sl_p;
  itr->current_node = sl->head->forward[0];
  return 0;
}

/*****************************************************************/
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char * __restrict dst, const char * __restrict src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}
/*****************************************************************/

char *
al_gettok(char *cp, char **savecp, char del)
{
  char *p = strchr(cp, del);
  if (p) {
    *p = '\0';
    *savecp = p + 1;
  } else {
    *savecp = NULL;
  }
  return cp;
}

int
al_split_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, const char *str, const char *dels)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    size_t ret = strlcpy(tmp_cp, str, tmp_size);
    if (tmp_size <= ret) return -8;
    while ((*ap = strsep(&tmp_cp, dels)) != NULL) {
      if (++ap >= &elms[size]) break;
    }
  }
  if (ap < &elms[size])
    *ap = NULL;
  return ap - &elms[0];
}

int
al_split_n_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, const char *str, const char *dels, int n)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    // strncpy(tmp_cp, str, tmp_size);
    size_t ret = strlcpy(tmp_cp, str, tmp_size);
    if (tmp_size <= ret) return -8;
    while (0 < --n && (*ap = strsep(&tmp_cp, dels)) != NULL) {
      if (++ap >= &elms[size]) break;
    }
    if (tmp_cp && n <= 0)
      *ap++ = tmp_cp;
  }
  if (ap < &elms[size])
    *ap = NULL;
  return ap - &elms[0];
}

int
al_split_nn_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, const char *str, const char *dels)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    size_t ret = strlcpy(tmp_cp, str, tmp_size);
    if (tmp_size <= ret) return -8;
    while ((*ap = strsep(&tmp_cp, dels)) != NULL) {
      if (**ap != '\0' && ++ap >= &elms[size]) break;
    }
  }
  if (ap < &elms[size])
    *ap = NULL;
  return ap - &elms[0];
}

int
al_split_nn_n_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, const char *str, const char *dels, int n)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    size_t ret = strlcpy(tmp_cp, str, tmp_size);
    if (tmp_size <= ret) return -8;
    --n;
    while (0 < n && (*ap = strsep(&tmp_cp, dels)) != NULL) {
      if (**ap == '\0') continue;
      if (++ap >= &elms[size]) break;
      --n;
    }
    if (tmp_cp && n <= 0)
      *ap++ = tmp_cp;
  }
  if (ap < &elms[size])
    *ap = NULL;
  return ap - &elms[0];
}

/* join elms[0] .. elms[n-1] with delch */
int
al_strcjoin_n_impl(char **elms, unsigned int elms_size,
		   char *tmp_cp, unsigned int tmp_size, const char delch, int n)
{
  int sret, i;
  if (!elms || !tmp_cp) return -3;
  *tmp_cp = '\0';
  n = elms_size < n ? elms_size : n;
  for (i = 0; i < n; i++) {
    if (elms[i] == NULL) break;
    sret = strlcpy(tmp_cp, elms[i], tmp_size);
    if (tmp_size <= sret) return -8;	// tmp_cp overflow
    if (n <= i + 1) break;
    if (i < elms_size - 1 && elms[i + 1] == NULL) break;
    tmp_cp += sret;
    tmp_size -= sret;
    if (tmp_size < 2) return -8;
    *tmp_cp++ = delch;
    *tmp_cp++ = '\0';
    tmp_size -= 2;
  }
  return 0;
}

#if 0
/* not used yet */
/* join elms[0] .. elms[n-1] with del string */
int
al_strjoin_n_impl(char **elms, unsigned int elms_size,
		  char *tmp_cp, unsigned int tmp_size, const char *del, int n)
{
  int sret, i;
  if (!elms || !tmp_cp) return -3;
  *tmp_cp = '\0';
  n = elms_size < n ? elms_size : n;
  for (i = 0; i < n; i++) {
    if (elms[i] == NULL) break;
    sret = strlcpy(tmp_cp, elms[i], tmp_size);
    if (tmp_size <= sret) return -8;	// tmp_cp overflow
    if (n <= i + 1) break;
    if (i < elms_size - 1 && elms[i + 1] == NULL) break;
    tmp_cp += sret;
    tmp_size -= sret;
    sret = strlcpy(tmp_cp, del, tmp_size);
    if (tmp_size <= sret) return -8;	// tmp_cp overflow
    tmp_cp += sret;
    tmp_size -= sret;
  }
  return 0;
}
#endif

/****************************/
