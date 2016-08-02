/*
 * The hash function used here is FNV,
 *   <http://www.isthe.com/chongo/tech/comp/fnv/>
 */
/*
 *   Use and distribution licensed under the BSD license.
 *   See the LICENSE file for full text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alhash.h"

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

/* forward declaration */
union item_u;

/* type of list value */
#define LCDR_SIZE_L 4
#define LCDR_SIZE_U 64
typedef struct list_ {
  struct list_ *link;
  unsigned int va_used; // va[0] .. va[va_used-1] are used
  unsigned int va_size; // LCDR_SIZE_L .. LCDR_SIZE_U
  union {
    value_t value[1];    // variable size, LCDR_SIZE_L .. LCDR_SIZE_U       
    cstr_value_t va[1];  // variable size, LCDR_SIZE_L .. LCDR_SIZE_U
    void *ptr[1];        // variable size, LCDR_SIZE_L .. LCDR_SIZE_U
  } u;
} list_t;

#define HEAP_SIZE_L 16     /* initial size of heap */
struct al_heap_t {
  unsigned int flag;       /* sort order */
  unsigned int max_n;      /* max queue length */
  unsigned int heap_size;  /* size of allocated heap, <= max_n */
  unsigned int n_entries;  /* number of entries on heap <= heap_size */
  union item_u *heap;      /* size <= max_n + 1, one origin */
  const char *err_msg;     /* output on auto ended iterator abend */
};

/* hash entry, with unique key */

union item_u {
  value_t      value;
  cstr_value_t cstr;
  struct al_skiplist_t *skiplist;
  struct al_heap_t *heap;
  void         *ptr;
  list_t       *list;
};

struct item {
  struct item *chain;
  char *key;
  union item_u u;
};

#define HASH_FLAG_PQ_SORT_DIC   AL_SORT_DIC
#define HASH_FLAG_PQ_SORT_C_DIC AL_SORT_COUNTER_DIC
#define HASH_FLAG_SORT_NUMERIC  AL_SORT_NUMERIC
#define HASH_FLAG_SORT_ORD      (AL_SORT_NO|AL_SORT_DIC|AL_SORT_COUNTER_DIC)
#define HASH_FLAG_SORT_DICC     (AL_SORT_DIC|AL_SORT_COUNTER_DIC)
#define HASH_FLAG_SORT_MASK     (HASH_FLAG_SORT_ORD|AL_SORT_NUMERIC|AL_SORT_VALUE)

#define HASH_FLAG_SCALAR        HASH_TYPE_SCALAR
#define HASH_FLAG_STRING        HASH_TYPE_STRING
#define HASH_FLAG_LIST          HASH_TYPE_LIST
#define HASH_FLAG_PQ            HASH_TYPE_PQ
#define HASH_FLAG_POINTER       HASH_TYPE_POINTER
#define HASH_TYPE_MASK          (HASH_TYPE_SCALAR|HASH_TYPE_STRING|HASH_TYPE_LIST|HASH_TYPE_PQ|HASH_TYPE_POINTER)
#define HASH_FLAG_PARAM_SET     (HASH_TYPE_POINTER<<1)

#define ITER_FLAG_AE            0x10000   // call end() at end of iteration
#define ITER_FLAG_VIRTUAL       0x80000   // virtual hash_iter created
                                          //  al_list_hash_get() or al_pqueue_hash_get()

#define hash_size(n) (1U<<(n))

struct al_hash_iter_t;

struct al_hash_t {
  unsigned int  hash_bit;
  unsigned int  n_rehashing;
  unsigned long n_entries;      // number of items in hash_table
  unsigned long n_entries_old;  // number of items in hash_table_old
  unsigned long n_cancel_rehashing;

  int rehashing;                // 1: under re-hashing
  unsigned int rehashing_front;
  long moving_unit;

  unsigned int hash_mask;
  unsigned int hash_mask_old;
  long unique_id;

  struct item **hash_table;     // main hash table
  struct item **hash_table_old;
  struct al_hash_iter_t *iterators;     // iterators attached to me
  unsigned long pq_max_n;       // priority queue, max number of entries
  int (*dup_p)(void *ptr, unsigned int size, void **ret_v);  // pointer hash pointer duplication
  void (*free_p)(void *ptr);                                 // pointer hash pointer free
  int (*sort_p)(const void *, const void *);                 // pointer hash pointer sort
  int (*sort_rev_p)(const void *, const void *);             // pointer hash pointer sort (rev)
  unsigned int h_flag;          // sort order, ...
  const char *err_msg;          // output on auto ended iterator abend
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
  unsigned int oindex;  // save max index when key is sorted
  struct item **pplace;
  struct item **place;
  struct item *to_be_free; // iter_delete()ed item
  struct item **sorted;    // for sort by key
  struct al_hash_iter_t *chain;
  unsigned int n_value_iter;    // number of value iterators pointed me
  unsigned int hi_flag;         // lower 2byte are copy of ht->h_flag
};

struct al_list_value_iter_t {
  list_t *ls_link, *ls_current;
  union {
    value_t *ls_sorted;
    cstr_value_t *ls_sorted_str;
    void **ls_sorted_ptr;
    void *vptr; // for free()
  } u;
  struct al_hash_iter_t *ls_pitr; // parent
  unsigned int ls_max_index;
  unsigned int ls_index;     // index of sorted[]
  unsigned int ls_cindex;    // index of current->va[]
  int ls_flag;
};

struct al_pqueue_value_iter_t {
  union {
    struct al_skiplist_t *sl;
    struct al_heap_t *hp;
  } u;
  union {
    struct al_skiplist_iter_t *sl_iter;
    struct al_heap_iter_t *hp_iter;
  } ui;

  struct al_hash_iter_t *pi_pitr; // parent
  int pi_flag;
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
    if (hash_size(ht->hash_bit - 1) <= ht->rehashing_front) {
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
  if (ret < 0) {
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
hash_find(struct al_hash_t *ht, const char *key, unsigned int hv)
{
  struct item *it;
  unsigned int hindex;

  if (ht->rehashing && ht->rehashing_front <= (hindex = (hv & ht->hash_mask_old)))
    it = ht->hash_table_old[hindex];
  else
    it = ht->hash_table[hv & ht->hash_mask];

  while (it) {
    if (strcmp(key, it->key) == 0) // found
      return it;
    it = it->chain;
  }
  return NULL;
}

static int
hash_insert(struct al_hash_t *ht, unsigned int hv, const char *key, struct item *it)
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
hash_v_insert(struct al_hash_t *ht, unsigned int hv, const char *key, union item_u u)
{
  int ret = 0;
  struct item *it = (struct item *)malloc(sizeof(struct item));
  if (!it) return -2;

  it->u = u;
  it->key = strdup(key);
  if (!it->key) {
    free((void *)it);
    return -2;
  }
  ret = hash_insert(ht, hv, key, it);
  if (ret < 0) {
    free((void *)it->key);
    free((void *)it);
  }
  return ret;
}

static struct item *
hash_delete(struct al_hash_t *ht, const char *key, unsigned int hv)
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

  if (bit <= 0)
    bit = AL_DEFAULT_HASH_BIT;

  ret = resize_hash(bit, al_hash);
  if (ret < 0) return ret;

  al_hash->moving_unit = 2 * bit;
  *htp = al_hash;
  return 0;
}

int
al_init_hash(int type, int bit, struct al_hash_t **htp)
{
  if ((type & ~HASH_TYPE_MASK) != 0) return -7;
  if ((type & HASH_TYPE_LIST) != 0) { // list hash
    if ((type & (HASH_TYPE_PQ)) != 0 ||
        (type & (HASH_TYPE_SCALAR|HASH_TYPE_STRING|HASH_TYPE_POINTER)) == 0 ||
        (type & (HASH_TYPE_SCALAR|HASH_TYPE_STRING|HASH_TYPE_POINTER)) == (HASH_TYPE_SCALAR|HASH_TYPE_STRING|HASH_TYPE_POINTER) ) {
      return -7;
    }
  } else if ((type & HASH_TYPE_PQ) != 0) {
    if ((type & (HASH_TYPE_LIST|HASH_TYPE_POINTER)) != 0 ||
        (type & (HASH_TYPE_SCALAR|HASH_TYPE_STRING)) == 0 ||
        (type & (HASH_TYPE_SCALAR|HASH_TYPE_STRING)) == (HASH_TYPE_SCALAR|HASH_TYPE_STRING) ) {
      return -7;
    }
  } else if ((type & (type - 1)) != 0) {
    return -7;
  }

  int ret = init_hash(bit, htp);
  if (0 <= ret)
    (*htp)->h_flag = type;
  return ret;
}

int
al_set_pqueue_hash_parameter(struct al_hash_t *ht, int sort_order, unsigned long max_n)
{
  if (!ht) return -3;
  if (!(ht->h_flag & HASH_FLAG_PQ)) return -6;
  if (ht->h_flag & HASH_FLAG_PARAM_SET) return -10; // parameter already set
  int so = sort_order & HASH_FLAG_SORT_ORD;
  if (so != AL_SORT_DIC && so != AL_SORT_COUNTER_DIC) return -7;
  if (max_n == 0) return -9;

  ht->h_flag |= so | HASH_FLAG_PARAM_SET;
  if (sort_order & AL_SORT_NUMERIC)
    ht->h_flag |= HASH_FLAG_SORT_NUMERIC;
  ht->pq_max_n = max_n;

  return 0;
}

int
al_set_pointer_hash_parameter(struct al_hash_t *ht,
                              int (*dup_p)(void *ptr, unsigned int size, void **ret_v),
                              void (*free_p)(void *ptr),
                              int (*sort_p)(const void *, const void *),
                              int (*sort_rev_p)(const void *, const void *))
{
  if (!ht) return -3;
  if (!(ht->h_flag & HASH_FLAG_POINTER)) return -6;
  if (ht->h_flag & HASH_FLAG_PARAM_SET) return -10; // parameter already set
  ht->dup_p = dup_p;
  ht->free_p = free_p;
  ht->sort_p = sort_p;
  ht->sort_rev_p = sort_rev_p;

  ht->h_flag |= HASH_FLAG_PARAM_SET;
  if (!sort_p != !sort_rev_p) return -8;

  return 0;
}

void *
al_get_pointer_hash_pointer(const void *a)
{
  return (*(struct item **)a)->u.ptr;
}

void *
al_get_pointer_list_hash_pointer(const void *a)
{
  return (void *)a;
}

int
al_init_unique_id(struct al_hash_t *ht, long id)
{
  if (!ht) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  if (ht->n_entries || ht->n_entries_old) return -10;

  ht->unique_id = id;
  return 0;
}

static void
free_list_value(struct al_hash_t *ht, list_t *dp)
{
  int flag = ht->h_flag;
  unsigned int i;
  while (dp) {
    list_t *nextp = dp->link;
    if (flag & HASH_FLAG_STRING) {
      for (i = dp->va_used; 0 < i;)
        free((void *)dp->u.va[--i]);
    } else if (flag & HASH_FLAG_POINTER) {
      for (i = dp->va_used; 0 < i;)
        if (ht->free_p)
          ht->free_p(dp->u.ptr[--i]);
        else
          free(dp->u.ptr[--i]);
    }
    free((void *) dp);
    dp = nextp;
  }
}

static void
free_value(struct al_hash_t *ht, struct item *it)
{
  switch (ht->h_flag & HASH_TYPE_MASK) {
  case HASH_FLAG_STRING:
    free((void *)it->u.cstr);
    break;
  case HASH_FLAG_POINTER:
    if (ht->free_p)
      ht->free_p(it->u.ptr);
    else
      free(it->u.ptr);
    break;
  case HASH_FLAG_PQ|HASH_FLAG_STRING:
    al_free_skiplist(it->u.skiplist);
    break;
  case HASH_FLAG_PQ|HASH_FLAG_SCALAR:
    al_free_heap(it->u.heap);
    break;
  case HASH_FLAG_LIST|HASH_FLAG_SCALAR:
  case HASH_FLAG_LIST|HASH_FLAG_STRING:
  case HASH_FLAG_LIST|HASH_FLAG_POINTER:
    free_list_value(ht, it->u.list);
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

  /* remove iterp from chain */
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
  else if (da == db)
    return 0;
  else
    return da < db ? -1 : 1;
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
  if ((*(struct item **)a)->u.value == (*(struct item **)b)->u.value) return 0;
  return (*(struct item **)a)->u.value < (*(struct item **)b)->u.value ? -1 : 1;
}

static int
itn_num_value_cmp(const void *a, const void *b)
{
  if ((*(struct item **)a)->u.value == (*(struct item **)b)->u.value) return 0;
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

static int
(*sort_f(struct al_hash_t *ht, int flag))(const void *, const void *)
{
  int so = flag & HASH_FLAG_SORT_DICC;

  if (flag & AL_SORT_VALUE) {           // sort by value part
    if ((ht->h_flag & (HASH_FLAG_PQ|HASH_FLAG_LIST)) != 0) {
      return NULL;
    } else if ((ht->h_flag & HASH_FLAG_POINTER) != 0) {
      return so == AL_SORT_DIC ? ht->sort_p : ht->sort_rev_p; // may be NULL
    } else if ((ht->h_flag & HASH_FLAG_STRING) == 0) {
      return so == AL_SORT_DIC ? it_num_value_cmp : itn_num_value_cmp;
    } else if (flag & AL_SORT_NUMERIC) {
      return so == AL_SORT_DIC ? it_value_strnum_cmp : itn_value_strnum_cmp;
    } else {
      return so == AL_SORT_DIC ? it_value_cmp : itn_value_cmp;
    }
  } else {                              // sort by key part
    if (flag & AL_SORT_NUMERIC) {
      return so == AL_SORT_DIC ? it_num_cmp : itn_num_cmp;
    } else {
      return so == AL_SORT_DIC ? it_cmp : itn_cmp;
    }
  }
}

inline static int
iter_sort(struct al_hash_t *ht, struct al_hash_iter_t *ip, int flag, long topk)
{
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

  int (*sf)(const void *, const void *) = sort_f(ht, flag);
  if (sf == NULL)
    return -8;

  if (topk == AL_FFK_HALF)
    topk = sidx / 2;

  if (0 < topk && topk < sidx) {
    al_ffk((void *)it_array, sidx, sizeof(struct item *), sf, topk);
    it_array = (struct item **)realloc(it_array, sizeof(struct item *) * topk); // reduce size
    sidx = topk;
    if (flag & AL_SORT_FFK_REV) {
      // bit not HASH_FLAG_SORT_ORD part of flag
      int f = (flag & ~HASH_FLAG_SORT_ORD) | ((~flag) & HASH_FLAG_SORT_ORD);
      sf = sort_f(ht, f);       // reverse order
    }
  }
  if (topk == 0 || (flag & AL_SORT_FFK_ONLY) == 0)
    qsort((void *)it_array, sidx, sizeof(struct item *), sf);

  ip->sorted = it_array;
  ip->oindex = sidx;    // max index
  return 0;
}

inline static int
iter_nsort(struct al_hash_t *ht, struct al_hash_iter_t *ip)
{
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
  return 0;
}

static int
check_iter_init_flag(int flag, long topk)
{
  int so = flag & HASH_FLAG_SORT_ORD;
  if (so == 0)
    return -7;
  if (0 < topk || topk == AL_FFK_HALF) {
    if (so & AL_SORT_NO)
      return -7;
    if ((so & (AL_SORT_FFK_ONLY|AL_SORT_FFK_REV)) == (AL_SORT_FFK_ONLY|AL_SORT_FFK_REV))
      return -7;
  }
  if ((so & AL_SORT_NO) && ((so & HASH_FLAG_SORT_MASK) != AL_SORT_NO))
    return -7;
  if (so == HASH_FLAG_SORT_DICC)
    return -7;
  return so;
}

int
al_hash_topk_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp,
                       int flag, long topk)
{
  if (!ht || !iterp) return -3;
  int so = check_iter_init_flag(flag, topk);
  if (so < 0) return so;

  *iterp = NULL;
  int ret = 0;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;

  if (so == AL_SORT_NO)
    ret = iter_nsort(ht, ip);
  else
    ret = iter_sort(ht, ip, flag, topk);
  if (0 <= ret) { // no error
    ip->hi_flag = ht->h_flag | (flag & AL_ITER_AE);
    ip->ht = ht;
    *iterp = ip;
    attach_iter(ht, ip);
  }
  return ret;
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
      if (iterp->ht && iterp->ht->err_msg)
        msg = iterp->ht->err_msg;
      fprintf(stderr, "hash_iter %s advance error (code=%d)", msg, ret);
      if (ret == -4) {
        al_hash_iter_end(iterp);
        fprintf(stderr, ", iter_end() done");
      }
      fprintf(stderr, "\n");
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
  if (0 <= ret) {
    *key = it->key;
    if (ret_v) *ret_v = it->u.value;
  } else {
    check_hash_iter_ae(iterp, ret);
  }
  return ret;
}

int
al_hash_iter_str(struct al_hash_iter_t *iterp, const char **key, cstr_value_t *ret_v)
{
  int ret = 0;
  struct item *it;
  if (!iterp || !key) return -3;

  *key = NULL;
  ret = advance_iter(iterp, &it);
  if (0 <= ret) {
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
  if (0 <= ret) {
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

/* scalar, list and pqueue ht acceptable */
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
  if (iterp && 2 <= iterp->n_value_iter)
    fprintf(stderr, "WARN: value_iter, other value iterators exists on iterp(%p)\n", iterp);
#endif
}

static void
detach_value_iter(struct al_hash_iter_t *iterp)
{
  if (iterp)
    --iterp->n_value_iter;
}

/**** iterator of value of list hash ***/

static int
v_cmp(const void *a, const void *b)
{
  return strcmp(*(cstr_value_t *)a, *(cstr_value_t *)b);
}

static int
vn_cmp(const void *a, const void *b)
{
  return -strcmp(*(cstr_value_t *)a, *(cstr_value_t *)b);
}

static int
v_num_cmp(const void *a, const void *b)
{
  return str_num_cmp(*(cstr_value_t *)a, *(cstr_value_t *)b);
}

static int
vn_num_cmp(const void *a, const void *b)
{
  return -str_num_cmp(*(cstr_value_t *)a, *(cstr_value_t *)b);
}

static int
vv_cmp(const void *a, const void *b)
{
  if (*(value_t *)a == *(value_t *)b) return 0;
  return *(value_t *)a < *(value_t *)b ? -1 : 1;
}

static int
vvn_cmp(const void *a, const void *b)
{
  if (*(value_t *)a == *(value_t *)b) return 0;
  return *(value_t *)a > *(value_t *)b ? -1 : 1;
}

static int
(*sort_vf(struct al_hash_t *ht, int flag))(const void *, const void *)
{
  int so = flag & HASH_FLAG_SORT_DICC;
  if (flag & HASH_TYPE_POINTER) {
    return so == AL_SORT_DIC ? ht->sort_p : ht->sort_rev_p; // may be NULL
  } else if (flag & HASH_TYPE_SCALAR) {
    return so == AL_SORT_DIC ? vv_cmp : vvn_cmp;
  } else if (flag & AL_SORT_NUMERIC) {
    return so == AL_SORT_DIC ? v_num_cmp : vn_num_cmp;
  } else {
    return so == AL_SORT_DIC ? v_cmp : vn_cmp;
  }
}

/**** iterator of value of list hash ***/

static int
mk_list_hash_iter(struct item *it, struct al_hash_iter_t *iterp,
                  struct al_list_value_iter_t **v_iterp, int flag, long topk)
{
  int so = check_iter_init_flag(flag, topk);
  if (so < 0) return so;

  *v_iterp = NULL;

  struct al_list_value_iter_t *vip =
    (struct al_list_value_iter_t *)calloc(1, sizeof(struct al_list_value_iter_t));
  if (!vip) return -2;

  if (so == AL_SORT_NO) {
    vip->ls_link = vip->ls_current = it->u.list;
  } else {
    struct al_hash_t *ht = iterp->ht;
    cstr_value_t *sarray = NULL;
    value_t *svarray = NULL;
    void **sparray = NULL;
    int i, j;
    unsigned int nvalue = vip->ls_max_index;
    list_t *dp;

    int (*sf)(const void *, const void *) = sort_vf(ht, flag);
    if (sf == NULL) {
      free((void *)vip);
      return -8;
    }

    if (nvalue == 0) { // al_list_hash_iter_nvalue() may set vip->ls_max_index to correct value
      for (dp = it->u.list; dp; dp = dp->link)
        nvalue += dp->va_used;
    }

    if (iterp->hi_flag & HASH_FLAG_SCALAR) {
      svarray = (value_t *)malloc(nvalue * sizeof(value_t));
      if (!svarray) goto free_vip;
      for (i = 0, dp = it->u.list; dp; dp = dp->link)
        for (j = 0; j < dp->va_used; j++) svarray[i++] = dp->u.value[j];
      vip->u.ls_sorted = svarray;
    } else if (iterp->hi_flag & HASH_FLAG_STRING) {
      sarray = (cstr_value_t *)malloc(nvalue * sizeof(cstr_value_t));
      if (!sarray) goto free_vip;
      for (i = 0, dp = it->u.list; dp; dp = dp->link)
        for (j = 0; j < dp->va_used; j++) sarray[i++] = dp->u.va[j];
      vip->u.ls_sorted_str = sarray;
    } else { // pointer
      sparray = (void **)malloc(nvalue * sizeof(void *));
      if (!sparray) goto free_vip;
      for (i = 0, dp = it->u.list; dp; dp = dp->link)
        for (j = 0; j < dp->va_used; j++) sparray[i++] = dp->u.ptr[j];
      vip->u.ls_sorted_ptr = sparray;
    }

    vip->ls_max_index = nvalue;

    if (topk == AL_FFK_HALF)
      topk = nvalue / 2;

    if (0 < topk && topk < nvalue) {
      if (iterp->hi_flag & HASH_FLAG_SCALAR) {
        al_ffk((void *)vip->u.ls_sorted, nvalue, sizeof(value_t), sf, topk);
        vip->u.ls_sorted = (value_t *)realloc(vip->u.ls_sorted, sizeof(value_t) * topk);
      } else if (iterp->hi_flag & HASH_FLAG_STRING) {
        al_ffk((void *)vip->u.ls_sorted_str, nvalue, sizeof(cstr_value_t), sf, topk);
        vip->u.ls_sorted_str = (cstr_value_t *)realloc(vip->u.ls_sorted_str, sizeof(cstr_value_t) * topk);
      } else { // pointer
        al_ffk((void *)vip->u.ls_sorted_ptr, nvalue, sizeof(void *), sf, topk);
        vip->u.ls_sorted_ptr = (void *)realloc(vip->u.ls_sorted_ptr, sizeof(void *) * topk);
      }

      nvalue = topk;
      vip->ls_max_index = topk;
      if (flag & AL_SORT_FFK_REV) {
        // bit_not (AL_SORT_DIC|AL_SORT_COUNTER_DIC) part of flag
        int f = (flag & ~HASH_FLAG_SORT_DICC) | ((~flag) & HASH_FLAG_SORT_DICC);
        sf = sort_vf(ht, f);    // reverse order
      }
    }
    if (topk == 0 || (flag & AL_SORT_FFK_ONLY) == 0) {
      if (iterp->hi_flag & HASH_FLAG_SCALAR)
        qsort((void *)vip->u.ls_sorted, nvalue, sizeof(value_t), sf);
      if (iterp->hi_flag & HASH_FLAG_STRING)
        qsort((void *)vip->u.ls_sorted_str, nvalue, sizeof(cstr_value_t), sf);
      else
        qsort((void *)vip->u.ls_sorted_ptr, nvalue, sizeof(void *), sf);
    }
  }

  vip->ls_flag = flag;
  vip->ls_pitr = iterp;
  attach_value_iter(iterp);
  *v_iterp = vip;

  return 0;

 free_vip:
  free((void *)vip);
  return -2;
}

int
al_list_topk_hash_get(struct al_hash_t *ht, const char *key,
                      struct al_list_value_iter_t **v_iterp, int flag, long topk)
{
  int ret = 0;
  if (!ht || !key || !v_iterp) return -3;
  if (!(ht->h_flag & HASH_FLAG_LIST)) return -6;

  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) return -1;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;

  flag |= (ht->h_flag & HASH_TYPE_MASK);
  ip->hi_flag = (ht->h_flag & HASH_TYPE_MASK) | ITER_FLAG_VIRTUAL;
  ip->ht = ht;

  ret = mk_list_hash_iter(it, ip, v_iterp, flag, topk);
  if (ret < 0) {
    free((void *)ip);
    return ret;
  }
  attach_iter(ht, ip);

  return 0;
}

int
al_list_hash_topk_iter(struct al_hash_iter_t *iterp, const char **key,
                       struct al_list_value_iter_t **v_iterp, int flag, long topk)
{
  int ret = 0;
  struct item *it;
  if (!iterp || !key || !v_iterp) return -3;

  *key = NULL;
  if (!(iterp->hi_flag & HASH_FLAG_LIST))
    ret = -6;
  else
    ret = advance_iter(iterp, &it);

  if (ret < 0) {
    check_hash_iter_ae(iterp, ret);
    return ret;
  }

  *key = it->key;
  flag |= (iterp->hi_flag & HASH_TYPE_MASK);
  return mk_list_hash_iter(it, iterp, v_iterp, flag, topk);
}

/* advance iterator */

int
al_list_value_iter(struct al_list_value_iter_t *v_iterp, value_t *ret_v)
{       
  int ret = -1;
  value_t lv = 0;
  if (!v_iterp) return -3;
  if ((v_iterp->ls_flag & (HASH_TYPE_STRING|HASH_TYPE_POINTER))) return -6;

  if (v_iterp->u.ls_sorted) {
    if (v_iterp->ls_index < v_iterp->ls_max_index) {
      lv = v_iterp->u.ls_sorted[v_iterp->ls_index++];
      ret = 0;
    }
  } else {
    while (v_iterp->ls_current) {
      list_t *dp = v_iterp->ls_current;
      if (v_iterp->ls_cindex < dp->va_used) {
        lv = dp->u.value[v_iterp->ls_cindex++];
        ret = 0;
        break;
      }
      v_iterp->ls_cindex = 0;
      v_iterp->ls_current = dp->link;
    }
  }

  if (ret == 0) {
    if (ret_v)
      *ret_v = lv;
  } else if (v_iterp->ls_flag & AL_ITER_AE)
    al_list_value_iter_end(v_iterp);

  return ret;
}

int
al_list_value_iter_str(struct al_list_value_iter_t *v_iterp, cstr_value_t *ret_v)
{       
  int ret = -1;
  cstr_value_t lv = NULL;
  if (!v_iterp) return -3;
  if ((v_iterp->ls_flag & (HASH_TYPE_SCALAR|HASH_TYPE_POINTER))) return -6;

  if (v_iterp->u.ls_sorted_str) {
    if (v_iterp->ls_index < v_iterp->ls_max_index) {
      lv = v_iterp->u.ls_sorted_str[v_iterp->ls_index++];
      ret = 0;
    }
  } else {
    while (v_iterp->ls_current) {
      list_t *dp = v_iterp->ls_current;
      if (v_iterp->ls_cindex < dp->va_used) {
        lv = dp->u.va[v_iterp->ls_cindex++];
        ret = 0;
        break;
      }
      v_iterp->ls_cindex = 0;
      v_iterp->ls_current = dp->link;
    }
  }

  if (ret == 0) {
    if (ret_v)
      *ret_v = lv;
  } else if (v_iterp->ls_flag & AL_ITER_AE)
    al_list_value_iter_end(v_iterp);

  return ret;
}

int
al_list_value_iter_ptr(struct al_list_value_iter_t *v_iterp, void **ret_v)
{
  int ret = -1;
  void *vptr = NULL;

  if (!v_iterp) return -3;
  if ((v_iterp->ls_flag & (HASH_TYPE_SCALAR|HASH_TYPE_STRING))) return -6;

  if (v_iterp->u.ls_sorted_ptr) {
    if (v_iterp->ls_index < v_iterp->ls_max_index) {
      vptr = v_iterp->u.ls_sorted_ptr[v_iterp->ls_index++];
      ret = 0;
    }
  } else {
    while (v_iterp->ls_current) {
      list_t *dp = v_iterp->ls_current;
      if (v_iterp->ls_cindex < dp->va_used) {
        vptr = dp->u.ptr[v_iterp->ls_cindex++];
        ret = 0;
        break;
      }
      v_iterp->ls_cindex = 0;
      v_iterp->ls_current = dp->link;
    }
  }

  if (ret == 0) {
    if (ret_v)
      *ret_v = vptr;
  } else if (v_iterp->ls_flag & AL_ITER_AE)
    al_list_value_iter_end(v_iterp);

  return ret;
}

int
al_list_value_iter_end(struct al_list_value_iter_t *vip)
{
  if (!vip) return -3;

  detach_value_iter(vip->ls_pitr);
  if (vip->ls_pitr->hi_flag & ITER_FLAG_VIRTUAL) {
    detach_iter(vip->ls_pitr->ht, vip->ls_pitr);
    free((void *)vip->ls_pitr);
  }
  if (vip->u.vptr)
    free((void *)vip->u.vptr);

  free((void *)vip);

  return 0;
}

int
al_list_value_iter_min_max(struct al_list_value_iter_t *v_iterp,
                           value_t *ret_v_min, value_t *ret_v_max)
{
  if (!v_iterp) return -3;
  if ((v_iterp->ls_flag & HASH_TYPE_STRING)) return -6;
  if (!v_iterp->u.ls_sorted) return -6;
  if (v_iterp->ls_max_index == 0) return -1;
  
  value_t va, vb;
  value_t mx, mi;

  if ((v_iterp->ls_flag & AL_SORT_FFK_ONLY) == 0) {
    mx = va = v_iterp->u.ls_sorted[0];
    mi = vb = v_iterp->u.ls_sorted[v_iterp->ls_max_index - 1];
    if (va < vb) { mi = va; mx = vb; }
  } else {
    mx = mi = v_iterp->u.ls_sorted[0];
    unsigned int i = (v_iterp->ls_max_index & 1);
    for (; i < v_iterp->ls_max_index; i += 2) {
      if ((va = v_iterp->u.ls_sorted[i]) < (vb = v_iterp->u.ls_sorted[i+1])) {
        mx = mx < vb ? vb : mx;
        mi = va < mi ? va : mi;
      } else {
        mx = mx < va ? va : mx;
        mi = vb < mi ? vb : mi;
      }
    }
  }
  if (ret_v_min) *ret_v_min = mi;
  if (ret_v_max) *ret_v_max = mx;
  return 0;
}

int
al_list_hash_iter_nvalue(struct al_list_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->ls_max_index)
    return vip->ls_max_index;
  int nvalue = 0;
  list_t *dp;
  for (dp = vip->ls_link; dp; dp = dp->link)
    nvalue += dp->va_used;
  vip->ls_max_index = nvalue;
  return nvalue;
}

int
al_list_hash_rewind_value(struct al_list_value_iter_t *vip)
{
  if (!vip) return -3;
  if (!vip->u.vptr) {
    vip->ls_current = vip->ls_link;
    vip->ls_cindex = 0;
  } else {
    vip->ls_index = 0;
  }
  return 0;
}

int
al_is_list_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  return ht->h_flag & HASH_FLAG_LIST ? 0 : -1;
}

int
al_is_list_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  return iterp->hi_flag & HASH_FLAG_LIST ? 0 : -1;
}

/**** iterator of priority queue ***/

static int
mk_pqueue_str_hash_iter(struct item *it, struct al_hash_iter_t *iterp,
                        struct al_pqueue_value_iter_t **v_iterp, int flag)
{
  int ret;
  *v_iterp = NULL;

  if (flag & AL_ITER_POP)
    return -7;

  struct al_pqueue_value_iter_t *vip =
    (struct al_pqueue_value_iter_t *)calloc(1, sizeof(struct al_pqueue_value_iter_t));
  if (!vip) return -2;

  vip->u.sl = it->u.skiplist;
  ret = sl_iter_init(vip->u.sl, &vip->ui.sl_iter, AL_FLAG_NONE);
  if (ret < 0) {
    free((void *)vip);
    return ret;
  }
  vip->pi_flag = (flag & AL_ITER_AE) | HASH_FLAG_STRING;
  vip->pi_pitr = iterp;
  attach_value_iter(iterp);
  *v_iterp = vip;
  return 0;
}

static int
mk_pqueue_hash_iter(struct item *it, struct al_hash_iter_t *iterp,
                    struct al_pqueue_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  *v_iterp = NULL;

  struct al_pqueue_value_iter_t *vip =
    (struct al_pqueue_value_iter_t *)calloc(1, sizeof(struct al_pqueue_value_iter_t));
  if (!vip) return -2;

  vip->u.hp = it->u.heap;
  ret = hp_iter_init(vip->u.hp, &vip->ui.hp_iter, flag & AL_ITER_POP);
  if (ret < 0) {
    free((void *)vip);
    return ret;
  }
  vip->pi_flag = (flag & AL_ITER_AE) | HASH_FLAG_SCALAR;
  vip->pi_pitr = iterp;
  attach_value_iter(iterp);
  *v_iterp = vip;
  return 0;
}

int
al_pqueue_hash_get(struct al_hash_t *ht, const char *key,
                   struct al_pqueue_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  if (!ht || !key || !v_iterp) return -3;
  if ((flag & ~(AL_ITER_AE|AL_ITER_POP|AL_FLAG_NONE)) != 0) return -7;
  if (!(ht->h_flag & HASH_FLAG_PQ)) return -6;

  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) return -1;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;

  ip->hi_flag = ITER_FLAG_VIRTUAL;
  ip->ht = ht;

  if (ht->h_flag & HASH_TYPE_STRING)
    ret = mk_pqueue_str_hash_iter(it, ip, v_iterp, flag);
  else
    ret = mk_pqueue_hash_iter(it, ip, v_iterp, flag);

  if (ret < 0) {
    free((void *)ip);
    return ret;
  }

  attach_iter(ht, ip);

  return 0;
}

int
al_pqueue_hash_iter(struct al_hash_iter_t *iterp, const char **key,
                    struct al_pqueue_value_iter_t **v_iterp, int flag)
{
  int ret = 0;
  struct item *it;
  if (!iterp || !key || !v_iterp) return -3;

  *key = NULL;
  if (!(iterp->hi_flag & HASH_FLAG_PQ))
    ret = -6;
  else if ((flag & ~(AL_ITER_AE|AL_ITER_POP|AL_FLAG_NONE)) != 0)
    ret = -7;
  else
    ret = advance_iter(iterp, &it);

  if (ret < 0) {
    check_hash_iter_ae(iterp, ret);
    return ret;
  }

  *key = it->key;

  if (iterp->hi_flag & HASH_TYPE_STRING)
    return mk_pqueue_str_hash_iter(it, iterp, v_iterp, flag);
  else
    return mk_pqueue_hash_iter(it, iterp, v_iterp, flag);
}

int
al_pqueue_value_iter(struct al_pqueue_value_iter_t *vip, value_t *ret_v)
{       
  if (!vip) return -3;
  int ret = hp_iter(vip->ui.hp_iter, ret_v);
  if (ret < 0 && (vip->pi_flag & AL_ITER_AE)) {
    if (ret == -1) { // normal end
      al_pqueue_value_iter_end(vip);
    } else {
      const char *msg = "";
      if (vip->pi_pitr && vip->pi_pitr->ht && vip->pi_pitr->ht->err_msg)
        msg = vip->pi_pitr->ht->err_msg;
      fprintf(stderr, "pqueue_value_iter %s advance error (code=%d)", msg, ret);
    }
  }
  return ret;
}

int
al_pqueue_value_iter_str(struct al_pqueue_value_iter_t *vip,
                         cstr_value_t *ret_v, pq_value_t *ret_count)
{       
  if (!vip) return -3;
  int ret = sl_iter(vip->ui.sl_iter, ret_v, ret_count);
  if (ret < 0 && (vip->pi_flag & AL_ITER_AE)) { // auto end
    if (ret == -1) { // normal end
      al_pqueue_value_iter_end(vip);
    } else {
      const char *msg = "";
      if (vip->pi_pitr && vip->pi_pitr->ht && vip->pi_pitr->ht->err_msg)
        msg = vip->pi_pitr->ht->err_msg;
      fprintf(stderr, "pqueue_value_iter_str %s advance error (code=%d)\n", msg, ret);
    }
  }
  return ret;
}

int
al_pqueue_value_iter_end(struct al_pqueue_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->pi_flag & HASH_FLAG_SCALAR)
    hp_iter_end(vip->ui.hp_iter);
  else
    sl_iter_end(vip->ui.sl_iter);

  detach_value_iter(vip->pi_pitr);
  if (vip->pi_pitr->hi_flag & ITER_FLAG_VIRTUAL) {
    detach_iter(vip->pi_pitr->ht, vip->pi_pitr);
    free((void *)vip->pi_pitr);
  }
  free((void *)vip);
  return 0;
}

int
al_pqueue_hash_iter_nvalue(struct al_pqueue_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->pi_flag & HASH_FLAG_STRING)
    return sl_n_entries(vip->u.sl);
  if (vip->pi_flag & HASH_FLAG_SCALAR)
    return hp_n_entries(vip->u.hp);
  return -6;
}

int
al_pqueue_hash_rewind_value(struct al_pqueue_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->pi_flag & HASH_FLAG_STRING)
    return sl_rewind_iter(vip->ui.sl_iter);
  else
    return hp_rewind_iter(vip->ui.hp_iter);
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

/* either scalar and list ht acceptable */

int
item_key(struct al_hash_t *ht, const char *key)
{
  if (!ht || !key) return -3;
  unsigned int hv = al_hash_fn_i(key);
  struct item *retp = hash_find(ht, key, hv);
  return retp ? 0 : -1;
}

int
item_get(struct al_hash_t *ht, const char *key, value_t *v)
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
item_get_str(struct al_hash_t *ht, const char *key, cstr_value_t *v)
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
item_get_pointer(struct al_hash_t *ht, const char *key, void **v)
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
item_set(struct al_hash_t *ht, const char *key, value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->u.value = v;
    return 0;
  }
  union item_u u = { .value = v };
  return hash_v_insert(ht, hv, key, u);
}

#ifdef ITEM_PV
int
item_set_pv(struct al_hash_t *ht, const char *key, value_t v, value_t *ret_pv)
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
  union item_u u = { .value = v };
  return hash_v_insert(ht, hv, key, u);
}
#endif

int
item_set_str(struct al_hash_t *ht, const char *key, cstr_value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_STRING)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  cstr_value_t lv = NULL;
  if (v) {
    lv = strdup(v);
    if (!lv) return -2;
  }

  if (it) {
    free((void *)it->u.cstr);
    it->u.cstr = lv;
    return 0;
  }
  union item_u u = { .cstr = lv };
  int ret = hash_v_insert(ht, hv, key, u);
  if (ret < 0)
    free((void *)lv);
  return ret;
}

int
item_set_pointer2(struct al_hash_t *ht, const char *key, void *v, unsigned int size, void **ret_v)
{
  int ret = 0;
  if (!ht || !key || !v) return -3;
  if (!(ht->h_flag & HASH_FLAG_POINTER)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);

  void *ptr;
  if (ht->dup_p) {
    ret = ht->dup_p(v, size, &ptr);
    if (ret < 0) return ret;
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
    union item_u u = { .ptr = ptr };
    ret = hash_v_insert(ht, hv, key, u);
  }
  if (0 <= ret && ret_v)
    *ret_v = ptr;
  return ret;
}

inline int
item_set_pointer(struct al_hash_t *ht, const char *key, void *v, unsigned int size)
{
  return item_set_pointer2(ht, key, v, size, NULL);
}

int
item_replace(struct al_hash_t *ht, const char *key, value_t v)
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
item_replace_pv(struct al_hash_t *ht, const char *key, value_t v, value_t *ret_pv)
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
item_replace_str(struct al_hash_t *ht, const char *key, cstr_value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_STRING)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);

  if (it) {
    cstr_value_t lv = NULL;
    if (v)
      lv = strdup(v);
    if (!lv) return -2;
    free((void *)it->u.cstr);
    it->u.cstr = lv;
    return 0;
  }
  return -1;
}

/* either scalar and list ht acceptable */
int
item_delete(struct al_hash_t *ht, const char *key)
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
item_delete_pv(struct al_hash_t *ht, const char *key, value_t *ret_pv)
{
  if (!ht || !key) return -3;
  if ((ht->h_flag & HASH_TYPE_MASK) != HASH_FLAG_SCALAR) return -6;
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
    free_value(ht, it);
    free((void *)it->key);
    free((void *)it);
    return 0;
  }
  return -1;
}
#endif

int
item_unique_id(struct al_hash_t *ht, const char *key, long *id)
{
  if (!ht || !key || !id) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    *id = it->u.value;
    return 0;
  }
  union item_u u = { .value = ht->unique_id++ };
  int ret = hash_v_insert(ht, hv, key, u);
  if (!ret) 
    *id = u.value;
  return ret;
}

int
item_unique_id_with_inv(struct al_hash_t *ht, struct al_hash_t *invht, const char *key, long *id)
{
  int ret = 0;
  if (!ht || !invht || !key || !id) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  if (!(invht->h_flag & HASH_FLAG_STRING)) return -6;

  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    *id = it->u.value;
    return 0;
  }
  union item_u u = { .value = ht->unique_id++ };
  ret = hash_v_insert(ht, hv, key, u);

  if (!ret) {
    cstr_value_t lv = strdup(key);
    if (!lv) {
      ht->unique_id--;
      hash_delete(ht, key, hv);
      return -2;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", u.value);
    unsigned int ihv = al_hash_fn_i(buf);
    union item_u uu = { .cstr = lv };
    ret = hash_v_insert(invht, ihv, buf, uu);
    if (!ret) {
      *id = u.value;
    } else {
      free((void *)lv);
      ht->unique_id--;
      hash_delete(ht, key, hv);
    }
  }

  return ret;
}

int
item_get_or_set(struct al_hash_t *ht, const char *key, value_t *ret_v, value_t id)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    if (ret_v)
      *ret_v = it->u.value;
    return 0;
  }
  union item_u u = { .value = id };
  int ret = hash_v_insert(ht, hv, key, u);
  if (!ret) {
    if (ret_v)
      *ret_v = id;
    return 1;
  }
  return ret;
}

int
item_inc(struct al_hash_t *ht, const char *key, value_t off, value_t *ret_v)
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
item_inc_init(struct al_hash_t *ht, const char *key, value_t off, value_t *ret_v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) {
    union item_u u = { .value = off };
#ifdef INC_INIT_RETURN_ONE
    int ret = hash_v_insert(ht, hv, key, u);
    return ret == 0 ? 1 : ret;
#else
    return hash_v_insert(ht, hv, key, u);
#endif
  }

  it->u.value += off;
  if (ret_v)
    *ret_v = it->u.value;
  return 0;
}

int
item_inc_init2(struct al_hash_t *ht, const char *key, value_t off, value_t init, value_t *ret_v)
{
  if (!ht || !key) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (!it) {
    union item_u u = { .value = init };
#ifdef INC_INIT_RETURN_ONE
    int ret = hash_v_insert(ht, hv, key, u);
    return ret == 0 ? 1 : ret;
#else
    return hash_v_insert(ht, hv, key, u);
#endif
  }

  it->u.value += off;
  if (ret_v)
    *ret_v = it->u.value;
  return 0;
}

static int
add_value_to_pq(struct al_hash_t *ht, const char *key, cstr_value_t v)
{
  int ret = 0;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) /* found, insert value part to sl */
    return sl_inc_init_n(it->u.skiplist, v, 1, NULL, ht->pq_max_n);

  it = (struct item *)malloc(sizeof(struct item));
  if (!it) return -2;

  struct al_skiplist_t *sl = NULL;
  ret = al_create_skiplist(&sl, ht->h_flag & HASH_FLAG_SORT_MASK);
  if (ret < 0) goto free;

  it->u.skiplist = sl;
  ret = sl_inc_init_n(it->u.skiplist, v, 1, NULL, ht->pq_max_n);
  if (ret < 0) goto free;

  it->key = strdup(key);
  if (!it->key) { ret = -2; goto free; }

  ret = hash_insert(ht, hv, key, it);
  if (ret < 0) goto free_key;

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
add_value_to_heap(struct al_hash_t *ht, const char *key, value_t v)
{
  int ret = 0;
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) /* found, insert value part to heap */
    return al_insert_heap(it->u.heap, v);

  it = (struct item *)malloc(sizeof(struct item));
  if (!it) return -2;

  struct al_heap_t *hp = NULL;
  ret = al_create_heap(&hp, ht->h_flag & HASH_FLAG_SORT_ORD, ht->pq_max_n);
  if (ret < 0) goto free;

  it->u.heap = hp;
  ret = al_insert_heap(it->u.heap, v);
  if (ret < 0) goto free;

  it->key = strdup(key);
  if (!it->key) { ret = -2; goto free; }

  ret = hash_insert(ht, hv, key, it);
  if (ret < 0) goto free_key;

  return 0;

  /* error return */
 free_key:
  free((void *)it->key);
 free:
  free((void *)it);
  al_free_heap(hp);
  return ret;
}

static int
add_value_to_list(struct al_hash_t *ht, const char *key, value_t v, cstr_value_t lv, void *vptr, unsigned int size)
{
  if (ht->h_flag & HASH_FLAG_STRING) {
    if (lv) {
      lv = strdup(lv);
      if (!lv) return -2;
    }
  } else if (ht->h_flag & HASH_FLAG_POINTER) {
    if ((ht->h_flag & HASH_FLAG_PARAM_SET) == 0) return -10;
    if (vptr) {
      void *p = NULL;
      if (ht->dup_p) {
        int ret = ht->dup_p(vptr, size, &p);
        if (ret < 0) return ret;
      } else {
        p = malloc(size);
        if (!p) return -2;
        memcpy(p, vptr, size);
      }
      vptr = p;
    }
  }
  unsigned int hv = al_hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  list_t *ndp = NULL;

  int ret = -2;
  if (it) {
    list_t *dp = it->u.list;
    if (dp->va_size <= dp->va_used) {
      int sz = dp->va_size;
      if (sz < LCDR_SIZE_U)
        sz <<= 1;
      if (ht->h_flag & HASH_FLAG_SCALAR)
        ndp = (list_t *)calloc(1, sizeof(list_t) + (sz - 1) * sizeof(value_t)); 
      else if (ht->h_flag & HASH_FLAG_STRING)
        ndp = (list_t *)calloc(1, sizeof(list_t) + (sz - 1) * sizeof(cstr_value_t));
      else
        ndp = (list_t *)calloc(1, sizeof(list_t) + (sz - 1) * sizeof(void *));

      if (!ndp) goto free_lv;

      ndp->va_size = sz;
      ndp->link = dp;
      it->u.list = ndp;
      dp = ndp;
    }

    if (ht->h_flag & HASH_FLAG_SCALAR)
      dp->u.value[dp->va_used++] = v;
    else if (ht->h_flag & HASH_FLAG_STRING)
      dp->u.va[dp->va_used++] = lv;
    else
      dp->u.ptr[dp->va_used++] = vptr;

    return 0;
  }

  if (ht->h_flag & HASH_FLAG_SCALAR)
    ndp = (list_t *)calloc(1, sizeof(list_t) + (LCDR_SIZE_L - 1) * sizeof(value_t));
  else if (ht->h_flag & HASH_FLAG_STRING)
    ndp = (list_t *)calloc(1, sizeof(list_t) + (LCDR_SIZE_L - 1) * sizeof(cstr_value_t));
  else
    ndp = (list_t *)calloc(1, sizeof(list_t) + (LCDR_SIZE_L - 1) * sizeof(void *));

  if (!ndp) goto free_lv;

  it = (struct item *)malloc(sizeof(struct item));
  if (!it) goto free_ndp;

  it->key = strdup(key);
  if (!it->key) goto free_it;

  ndp->va_size = LCDR_SIZE_L;

  if (ht->h_flag & HASH_FLAG_SCALAR)
    ndp->u.value[ndp->va_used++] = v;
  else if (ht->h_flag & HASH_FLAG_STRING)
    ndp->u.va[ndp->va_used++] = lv;
  else
    ndp->u.ptr[ndp->va_used++] = vptr;

  it->u.list = ndp;

  ret = hash_insert(ht, hv, key, it);
  if (ret < 0) goto free_key;

  return 0;

  /* error return */
 free_key:
    free((void *)it->key);
 free_it:
    free((void *)it);
 free_ndp:
    free((void *)ndp);
 free_lv:
    if (ht->h_flag & HASH_FLAG_STRING)
      free((void *)lv);
    else if (ht->free_p)
      ht->free_p(vptr);
    else
      free(vptr);

    return ret;
}

int
item_add_value_impl(struct al_hash_t *ht, const char *key,
                    value_t v, cstr_value_t lv, void *vptr, unsigned int size, int flag)
{
  if (!ht || !key) return -3;

  if (ht->h_flag & HASH_FLAG_PQ) {
    if ((ht->h_flag & HASH_FLAG_PARAM_SET) == 0) return -10;
    if (flag == HASH_FLAG_STRING) 
      return add_value_to_pq(ht, key, lv);
    if (flag == HASH_FLAG_SCALAR)
      return add_value_to_heap(ht, key, v);
    return -6;
  }

  if (ht->h_flag & HASH_FLAG_LIST) {
    if ((ht->h_flag & (HASH_FLAG_SCALAR|HASH_FLAG_STRING|HASH_TYPE_POINTER)) != flag) return -6;
    return add_value_to_list(ht, key, v, lv, vptr, size);
  }
  return -6;
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
  if (ret < 0) return ret;
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
al_nkeys(struct al_hash_t *ht, unsigned long *nkeys)
{
  if (!ht || !nkeys) return -3;
  *nkeys = ht->n_entries + ht->n_entries_old;
  return 0;
}

int
al_next_unique_id(struct al_hash_t *ht, long *ret_id)
{
  if (!ht || !ret_id) return -3;
  if (!(ht->h_flag & HASH_FLAG_SCALAR)) return -6;
  *ret_id = ht->unique_id;
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
 *  string priority queue, implemented by skiplist
 */

#define SL_MAX_LEVEL 31
#undef SL_FIRST_KEY
#define SL_LAST_KEY

struct slnode {
  pq_key_t key;
  union {
    pq_value_t value;
  } u;
  struct slnode *forward[1]; /* variable sized array of forward pointers */
};

struct al_skiplist_t {
  struct slnode *head;
#ifdef SL_FIRST_KEY
  pq_key_t first_key;
#endif
#ifdef SL_LAST_KEY
  // pq_key_t last_key;
  struct slnode *last_node;
#endif
  unsigned long n_entries;
  int level;
  int flag;
  const char *err_msg;
};

struct al_skiplist_iter_t {
  struct al_skiplist_t *sl_p;
  struct slnode *current_node;
  int sl_flag;  // ITER_FLAG_AE bit only
};

static int
pq_k_cmp(struct al_skiplist_t *sl, pq_key_t a, pq_key_t b)
{
  if (sl->flag & HASH_FLAG_SORT_NUMERIC) {
    if (sl->flag & HASH_FLAG_PQ_SORT_DIC)
      return str_num_cmp(a, b);
    else
      return -str_num_cmp(a, b);
  }
  if (sl->flag & HASH_FLAG_PQ_SORT_DIC)
    return strcmp(a, b);
  else
    return -strcmp(a, b);
}

int
al_skiplist_stat(struct al_skiplist_t *sl)
{
  struct slnode *np;
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
al_set_skiplist_err_msg(struct al_skiplist_t *sl, const char *msg)
{
  if (!sl) return -3;
  sl->err_msg = msg;
  return 0;
}

static struct slnode *
find_node(struct al_skiplist_t *sl, pq_key_t key, struct slnode *update[])
{
  int i;
  struct slnode *np = sl->head;
  for (i = sl->level - 1; 0 <= i; --i) {
    while (np->forward[i]) {
      int c = pq_k_cmp(sl, np->forward[i]->key, key);
      if (c < 0) {
        np = np->forward[i];
      } else {
#ifndef SL_ALLOW_DUPKEY
        if (c == 0) // key found
          return np->forward[i];
        // building update[] is not completed
#endif
        break;
      }
    }
    update[i] = np;
  }
#ifdef SL_ALLOW_DUPKEY
  np = np->forward[0];
  if (np && pq_k_cmp(sl, np->key, key) == 0) {
    return np;
  }
#endif
  return NULL;
}

static struct slnode *
mk_node(int level, pq_key_t key)
{
  struct slnode *np = (struct slnode *)calloc(1, sizeof(struct slnode) +
                                              (level - 1) * sizeof(struct slnode *));
  if (!np) return NULL;

  np->key = strdup(key);
  if (!np->key) {
    free((void *)np);
    return NULL;
  }

  return np;
}

int
al_create_skiplist(struct al_skiplist_t **slp, int flag)
{
  if (!slp) return -3;
  int so = flag & (~AL_SORT_NUMERIC);
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
  sl->flag = flag;
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

  struct slnode *np, *next;
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
sl_delete_node(struct al_skiplist_t *sl, pq_key_t key, struct slnode *np, struct slnode **update)
{
  int i;
#ifdef SL_FIRST_KEY
  int c = strcmp(sl->first_key, key);  // eq check, instead of pq_k_cmp()
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
  struct slnode *update[SL_MAX_LEVEL], *np;
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
  struct slnode *update[SL_MAX_LEVEL], *np, *lnode, *p;
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
node_set(struct al_skiplist_t *sl, pq_key_t key, struct slnode *update[], struct slnode **ret_np)
{
  struct slnode *new_node;
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
sl_set(struct al_skiplist_t *sl, pq_key_t key, pq_value_t v)
{
  int ret = 0;
  struct slnode *update[SL_MAX_LEVEL];
  struct slnode *np;

  if (!sl || !key) return -3;
  np = find_node(sl, key, update);
  if (!np || (sl->flag & SL_DUP_KEY)) {
    ret = node_set(sl, key, update, &np);
    if (ret < 0) return ret;
  }
  np->u.value = v;
  return 0;
}

#ifdef SL_LAST_KEY
int
sl_set_n(struct al_skiplist_t *sl, pq_key_t key, pq_value_t v, unsigned long max_n)
{
  int ret = 0;
  if (!sl || !key) return -3;

  if (max_n == 0 || sl->n_entries < max_n)
    return sl_set(sl, key, v);

  if (pq_k_cmp(sl, sl->last_node->key, key) <= 0) return 0;
  ret = sl_set(sl, key, v);
  if (ret < 0) return ret;

  while (max_n < sl->n_entries) {
    ret = sl_delete_last_node(sl);
    if (ret < 0) return ret;
  }
  return 0;
}
#endif

int
sl_get(struct al_skiplist_t *sl, pq_key_t key, pq_value_t *ret_v)
{
  if (!sl || !key) return -3;

  struct slnode *np;
  struct slnode *update[SL_MAX_LEVEL];
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
sl_inc_init(struct al_skiplist_t *sl, pq_key_t key, pq_value_t off, pq_value_t *ret_v)
{
  struct slnode *update[SL_MAX_LEVEL];
  struct slnode *np;
  int ret = 0;

  if (!sl || !key) return -3;
  if (!(np = find_node(sl, key, update))) {
    ret = node_set(sl, key, update, &np);
    if (ret < 0) return ret;
    np->u.value = off;
#ifdef INC_INIT_RETURN_ONE
    return 1;
#endif
  } else {
    np->u.value += off;
    if (ret_v)
      *ret_v = np->u.value;
  }
  return 0;
}
#ifdef SL_LAST_KEY
int
sl_inc_init_n(struct al_skiplist_t *sl, pq_key_t key, pq_value_t off, pq_value_t *ret_v, unsigned long max_n)
{
  int ret = 0;
  if (!sl || !key) return -3;

  if (max_n == 0 || sl->n_entries < max_n)
    return sl_inc_init(sl, key, off, ret_v);

  if (pq_k_cmp(sl, sl->last_node->key, key) < 0) return 0;
  ret = sl_inc_init(sl, key, off, ret_v);
  if (ret < 0) return ret;

  while (max_n < sl->n_entries) {
    ret = sl_delete_last_node(sl);
    if (ret < 0) return ret;
  }
  return 0;
}
#endif

int
sl_front(struct al_skiplist_t *sl, pq_key_t *keyp, pq_value_t *ret_v)
{
  if (!sl) return -3;
  if (sl->head->forward[0] == NULL) return -1; // sl is empty

  if (keyp)
    *keyp = sl->head->forward[0]->key;
  if (ret_v)
    *ret_v = sl->head->forward[0]->u.value;
  return 0;
}

int
sl_pop_front_node(struct al_skiplist_t *sl)
{

  if (!sl) return -3;
  if (sl->head->forward[0] == NULL) return -1; // sl is empty

  struct slnode *update[SL_MAX_LEVEL], *np;
  int i;

  np = sl->head;
  for (i = sl->level - 1; 0 <= i; --i) {
    update[i] = np;
  }

#ifdef SL_FIRST_KEY
  pq_key_t key = np->forward[0]->key;
  sl_delete_node(sl, key, np->forward[0], update);
#else
  sl_delete_node(sl, "", np->forward[0], update);
#endif

  return 0;
}

#ifdef SL_LAST_KEY
int
sl_back(struct al_skiplist_t *sl, pq_key_t *keyp, pq_value_t *ret_v)
{
  if (!sl) return -3;
  if (sl->head->forward[0] == NULL) return -1; // sl is empty

  if (keyp)
    *keyp = sl->last_node->key;
  if (ret_v)
    *ret_v = sl->last_node->u.value;
  return 0;
}
#endif

int
sl_empty_p(struct al_skiplist_t *sl)
{
  if (!sl) return -3;
  return sl->head->forward[0] == NULL ? 0 : -1;
}

long
sl_n_entries(struct al_skiplist_t *sl)
{
  if (!sl) return -3;
  return sl->n_entries;
}

int
sl_iter_init(struct al_skiplist_t *sl, struct al_skiplist_iter_t **iterp, int flag)
{
  if (!sl || !iterp) return -3;
  if ((flag & ~(AL_ITER_AE|AL_FLAG_NONE)) != 0) return -7; // AL_ITER_AE, AL_FLAG_NONE are valid flags.
  struct al_skiplist_iter_t *itr = (struct al_skiplist_iter_t *)malloc(sizeof(struct al_skiplist_iter_t));
  if (!itr) return -2;
  itr->sl_p = sl;
  itr->current_node = sl->head->forward[0];
  itr->sl_flag = flag & ITER_FLAG_AE;
  *iterp = itr;
  return 0;
}

int
sl_iter(struct al_skiplist_iter_t *iterp, pq_key_t *keyp, pq_value_t *ret_v)
{
  int ret = -3;
  if (!iterp)
    return -3;
  ret = iterp->current_node ? 0 : -1;
  if (ret < 0) {
    if (!(iterp->sl_flag & ITER_FLAG_AE)) return ret;
    if (ret == -1) {
      sl_iter_end(iterp);
    } else {
      const char *msg = "";
      if (iterp->sl_p && iterp->sl_p->err_msg)
        msg = iterp->sl_p->err_msg;
      fprintf(stderr, "sl_iter %s advance error (code=%d)\n", msg, ret);
    }
    return ret;
  }

  if (keyp)
    *keyp = iterp->current_node->key;
  if (ret_v)
    *ret_v = iterp->current_node->u.value;
  iterp->current_node = iterp->current_node->forward[0];

  return 0;
}

int
sl_iter_end(struct al_skiplist_iter_t *iterp)
{
  if (!iterp) return -3;
  free((void *)iterp);
  return 0;
}

int
sl_rewind_iter(struct al_skiplist_iter_t *itr)
{
  if (!itr) return 3;
  struct al_skiplist_t *sl = itr->sl_p;
  itr->current_node = sl->head->forward[0];
  return 0;
}

/*
 *  heap structure
 */

static int
heap_num_value_cmp(const void *a, const void *b)
{
  if (((union item_u *)a)->value == ((union item_u *)b)->value) return 0;
  return ((union item_u *)a)->value < ((union item_u *)b)->value ? -1 : 1;
}

static int
heapn_num_value_cmp(const void *a, const void *b)
{
  if (((union item_u *)a)->value == ((union item_u *)b)->value) return 0;
  return ((union item_u *)a)->value > ((union item_u *)b)->value ? -1 : 1;
}

static void
down_heap(struct al_heap_t *hp, int index, int (*compar)(const void *, const void *))
{
  int j;
  int k = index;
  union item_u *heap = hp->heap;
  union item_u v = heap[k];
  while (k <= hp->n_entries / 2) {
    j = k + k;
    if (j < hp->n_entries && compar(&heap[j], &heap[j + 1]) < 0)
      j++;
    if (0 <= compar(&v, &heap[j]))
      break;
    heap[k] = heap[j];
    k = j;
  }
  heap[k] = v;
}

static void
up_heap(struct al_heap_t *hp, int index, int (*compar)(const void *, const void *))
{
  int k = index;
  union item_u *heap = hp->heap;
  union item_u v = heap[k];
  while (1 < k && compar(&heap[k / 2], &v) <= 0) {
    heap[k] = heap[k / 2];
    k = k / 2;
  }
  heap[k] = v;
}

static void
replace_heap(struct al_heap_t *hp, value_t v, int (*compar)(const void *, const void *))
{
  hp->heap[0].value = v;
  down_heap(hp, 0, compar);
}

static int
enlarge_heap(struct al_heap_t *hp)
{
  unsigned int ns = hp->heap_size * 3 / 2;
  if (hp->max_n < ns)
    ns = hp->max_n;
  union item_u *up = (union item_u *)realloc(hp->heap, sizeof(union item_u) * ns);
  if (!up) return -3;  
  hp->heap = up;
  hp->heap_size = ns;
  return 0;
}

#if 0
/* not used */
static void
shrink_heap(struct al_heap_t *hp)
{
  if (hp->heap_size / 3 <= n_entries)
    return;
  if (hp->heap_size <= HEAP_SIZE_L)
    return;

  unsigned int ns = hp->heap_size / 2;
  if (ns < HEAP_SIZE_L)
    ns = HEAP_SIZE_L;
  union item_u *up = (union item_u *)realloc(hp->heap, sizeof(union item_u) * ns);
  hp->heap = up;
  hp->heap_size = ns;
}
#endif

int
al_insert_heap(struct al_heap_t *hp, value_t v)
{
  if (!hp) return -3;
  int ret = 0;
  unsigned int so = hp->flag;

  if (so == AL_SORT_DIC) {
    // keep smaller value items
    // root has the maximum value on heap
    if (hp->n_entries == hp->max_n) {
      if (hp->heap[1].value <= v) return 0;
      replace_heap(hp, v, heap_num_value_cmp);
    } else {
      if (hp->n_entries == hp->heap_size &&
          (ret = enlarge_heap(hp)) < 0)
        return ret;
      hp->heap[++hp->n_entries].value = v;
      up_heap(hp, hp->n_entries, heap_num_value_cmp);
    }
  } else  { // AL_SORT_COUNTER_DIC, keep bigger value items
    // root has the minimum value on heap
    if (hp->n_entries == hp->max_n) {
      if (v <= hp->heap[1].value) return 0;
      replace_heap(hp, v, heapn_num_value_cmp);
    } else {
      if (hp->n_entries == hp->heap_size &&
          (ret = enlarge_heap(hp)) < 0)
        return ret;
      hp->heap[++hp->n_entries].value = v;
      up_heap(hp, hp->n_entries, heapn_num_value_cmp);
    }
  }
  return ret;
}

int
al_pop_heap(struct al_heap_t *hp, value_t *ret_v)
{
  if (!hp) return -3;

  if (!hp->n_entries) return -1; // empty
  if (ret_v)
    *ret_v = hp->heap[1].value;
  if (hp->n_entries == 1) {
    hp->n_entries--;
    return 0;
  }
  hp->heap[1].value = hp->heap[hp->n_entries--].value;

  if (hp->n_entries <= 1)
    return 0;

  if (hp->flag == AL_SORT_DIC) {
    down_heap(hp, 1, heap_num_value_cmp);
  } else {
    down_heap(hp, 1, heapn_num_value_cmp);
  }

  return 0;
}

int
al_delete_heap(struct al_heap_t *hp, unsigned int pos, value_t *ret_v)
{
  if (!hp) return -3;

  if (!hp->n_entries) return -1; // empty
  if (pos < 1 || hp->n_entries < pos)
    return -9;
  if (ret_v)
    *ret_v = hp->heap[pos].value;
  if (pos == hp->n_entries) {
    hp->n_entries--;
    return 0;
  }
  hp->heap[pos].value = hp->heap[hp->n_entries--].value;
  if (hp->n_entries <= 1)
    return 0;

  if (1 < pos &&
      ((hp->flag == AL_SORT_DIC && hp->heap[pos].value > hp->heap[pos / 2].value) ||
       (hp->flag != AL_SORT_DIC && hp->heap[pos].value < hp->heap[pos / 2].value) )) {
    if (hp->flag == AL_SORT_DIC) {
      up_heap(hp, pos, heap_num_value_cmp);
    } else {
      up_heap(hp, pos, heapn_num_value_cmp);
    }
  } else {
    if (hp->flag == AL_SORT_DIC) {
      down_heap(hp, pos, heap_num_value_cmp);
    } else {
      down_heap(hp, pos, heapn_num_value_cmp);
    }
  }

  return 0;
}

int
al_create_heap(struct al_heap_t **hpp, int sort_order, unsigned int max_n)
{
  if (!hpp) return -3;
  int so = sort_order & HASH_FLAG_SORT_ORD;
  if (so != AL_SORT_DIC && so != AL_SORT_COUNTER_DIC) return -7;
  if (max_n == 0) return -9;

  struct al_heap_t *hp = (struct al_heap_t *)malloc(sizeof(struct al_heap_t));
  if (!hp) return -3;
  hp->flag = so;
  hp->max_n = max_n;
  hp->heap_size = max_n < HEAP_SIZE_L ? max_n : HEAP_SIZE_L;
  hp->n_entries = 0;
  hp->err_msg = NULL;
  hp->heap = (union item_u *)calloc(hp->heap_size, sizeof(union item_u *) + 1);
  if (!hp->heap) {
    free((void *)hp);
    return -3;
  }
  *hpp = hp;
  return 0;
}

int
al_free_heap(struct al_heap_t *hp)
{
  free((void *)hp->heap);
  free((void *)hp);
  return 0;
}

int
hp_empty_p(struct al_heap_t *hp)
{
  if (!hp) return -3;
  return hp->n_entries == 0 ? 0 : -1;
}

long
hp_n_entries(struct al_heap_t *hp)
{
  if (!hp) return -3;
  return hp->n_entries;
}

struct al_heap_iter_t {
  struct al_heap_t *hp_p; // parent
  unsigned int hp_flag;
  unsigned int index;
};

int
hp_iter_init(struct al_heap_t *hp, struct al_heap_iter_t **iterp, int flag)
{
  if (!hp || !iterp) return -3;
  if ((flag & ~(AL_ITER_AE|AL_ITER_POP|AL_FLAG_NONE)) != 0)
    return -7;
  struct al_heap_iter_t *itr = (struct al_heap_iter_t *)malloc(sizeof(struct al_heap_iter_t));
  if (!itr) return -2;
  itr->hp_p = hp;
  itr->index = 1;
  itr->hp_flag = flag & (AL_ITER_AE|AL_ITER_POP);
  *iterp = itr;
  return 0;
}

/* advance iterator */
int
hp_iter(struct al_heap_iter_t *iterp, pq_value_t *ret_v)
{
  int ret = -3;
  if (!iterp)
    return -3;
  if (!iterp->hp_p)
    return -4;
  ret = iterp->index <= iterp->hp_p->n_entries ? 0 : -1;
  if (ret < 0) {
    if (!(iterp->hp_flag & ITER_FLAG_AE)) return ret;
    if (ret == -1) {
      hp_iter_end(iterp);
    } else {
      const char *msg = "";
      if (iterp->hp_p->err_msg)
        msg = iterp->hp_p->err_msg;
      fprintf(stderr, "hp_iter %s advance error (code=%d)\n", msg, ret);
    }
    return ret;
  }
  if (iterp->hp_flag & AL_ITER_POP) {
    al_pop_heap(iterp->hp_p, ret_v);
  } else {
    if (ret_v)
      *ret_v = iterp->hp_p->heap[iterp->index].value;
    iterp->index++;
  }
  return 0;
}

int
hp_iter_end(struct al_heap_iter_t *iterp)
{
  if (!iterp) return -3;
  free((void *)iterp);
  return 0;
}

int
hp_rewind_iter(struct al_heap_iter_t *iterp)
{
  if (!iterp) return -3;
  if (iterp->hp_flag & AL_ITER_POP)
    return -7;
  iterp->index = 1;
  return 0;
}

/*
 * first find k
 */

static inline void *
med3(void *xp, void *yp, void *zp, int (*compar)(const void *, const void *))
{
  return (*compar)(xp, yp) < 0 ?
        ((*compar)(yp, zp) < 0 ? yp : ((*compar)(xp, zp) < 0 ? zp : xp ))
       :((*compar)(yp, zp) > 0 ? yp : ((*compar)(xp, zp) < 0 ? xp : zp ));
}

/* swap element macro, based on FreeBSD qsort.c */

#define swapcode(TYPE, parmi, parmj, n) {       \
        long i = (n) / sizeof (TYPE);           \
        TYPE *pi = (TYPE *) (parmi);            \
        TYPE *pj = (TYPE *) (parmj);            \
        do {                                    \
          TYPE t = *pi;                         \
          *pi++ = *pj;                          \
          *pj++ = t;                            \
        } while (--i > 0);                      \
}

#define SWAPINIT(a, es) swaptype = ((char *)a - (char *)0) % sizeof(long) || \
        es % sizeof(long) ? 2 : es == sizeof(long)? 0 : 1;

static inline void
swapfunc(char *a, char *b, int n, int swaptype)
{
  if (swaptype <= 1)
    swapcode(long, a, b, n)
  else
    swapcode(char, a, b, n)
}

#define swap(a, b)                      \
  if (swaptype == 0) {                  \
    long t = *(long *)(a);              \
    *(long *)(a) = *(long *)(b);        \
    *(long *)(b) = t;                   \
  } else                                \
    swapfunc(a, b, esize, swaptype)

void
al_ffk(void *base, long nel, unsigned long esize,
       int (*compar)(const void *, const void *),
       long topn)
{
  void *lv = base;
  void *rv = base + esize * (nel - 1);
  void *topnp = base + esize * (topn - 1);
  int swaptype;

  while (lv < rv) {
    SWAPINIT(lv, esize);
    long w = (rv - lv) / esize;

    if (7 < w) {
      void *lt  = lv;
      void *mid = lt + (w / 2) * esize;
      void *rt  = rv;
      if (40  < w) {
        long d = (w / 8) * esize;
        lt  = med3(lt, lt + d, lt + 2 * d, compar);
        mid = med3(mid - d, mid, mid + d, compar);
        rt  = med3(rt - 2 * d, rt - d, rt, compar);
      }
      void *pvi = med3(lt, mid, rt, compar);
      swap(pvi, rv);
    }
    void *lp = lv - esize;
    void *rp = rv;
    for (;;) {
      do { lp += esize; } while ((*compar)(lp, rv) < 0);
      do { rp -= esize; } while (lp < rp && (*compar)(rp, rv) > 0);
      if (lp >= rp) break;
      swap(lp, rp);
    }
    swap(rv, lp);

    if (lp == topnp) break;

    if (topnp <= lp) rv = lp - esize;
    if (topnp >= lp) lv = lp + esize;
  }
}
#undef SWAPINIT
#undef swap

#ifndef HAVE_STRLCPY
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
                        *d = '\0';              /* NUL-terminate dst */
                while (*s++)
                        ;
        }

        return(s - src - 1);    /* count does not include NUL */
}
/*****************************************************************/
#endif

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
al_split_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, char *str, const char *dels)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    if (tmp_cp != str) {
      size_t ret = strlcpy(tmp_cp, str, tmp_size);
      if (tmp_size <= ret) return -8;
    }
    while ((*ap = strsep(&tmp_cp, dels)) != NULL) {
      if (++ap >= &elms[size]) break;
    }
  }
  if (ap < &elms[size])
    *ap = NULL;
  return ap - &elms[0];
}

int
al_split_n_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, char *str, const char *dels, int n)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    if (tmp_cp != str) {
      size_t ret = strlcpy(tmp_cp, str, tmp_size);
      if (tmp_size <= ret) return -8;
    }
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
al_split_nn_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, char *str, const char *dels)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    if (tmp_cp != str) {
      size_t ret = strlcpy(tmp_cp, str, tmp_size);
      if (tmp_size <= ret) return -8;
    }
    while ((*ap = strsep(&tmp_cp, dels)) != NULL) {
      if (**ap != '\0' && ++ap >= &elms[size]) break;
    }
  }
  if (ap < &elms[size])
    *ap = NULL;
  return ap - &elms[0];
}

int
al_split_nn_n_impl(char **elms, unsigned int size, char *tmp_cp, unsigned int tmp_size, char *str, const char *dels, int n)
{
  char **ap = elms;
  if (!elms || !tmp_cp) return -3;
  if (str) {
    if (tmp_cp != str) {
      size_t ret = strlcpy(tmp_cp, str, tmp_size);
      if (tmp_size <= ret) return -8;
    }
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

int
n_elements(const char *str, const char *del)
{
  if (!str || !del) return -3;
  int count = 0;
  if (!*str) return count; /* empty string */

  size_t cp = 0;
  for (;;) {
    cp += strcspn(str + cp, del);
    count++;
    if (!str[cp]) break;
    cp++;
  }
  return count;
}

int
n_elements_nn(const char *str, const char *del)
{
  if (!str || !del) return -3;

  int count = 0;
  if (!*str) return count; /* empty string */

  size_t cp = 0;
  for (;;) {
    size_t np = strcspn(str + cp, del);
    if (np != 0) {
      cp += np;
      count++;
    }
    if (!str[cp]) break;
    cp++;
  }
  return count;
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
    if (tmp_size <= sret) return -8;    // tmp_cp overflow
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
    if (tmp_size <= sret) return -8;    // tmp_cp overflow
    if (n <= i + 1) break;
    if (i < elms_size - 1 && elms[i + 1] == NULL) break;
    tmp_cp += sret;
    tmp_size -= sret;
    sret = strlcpy(tmp_cp, del, tmp_size);
    if (tmp_size <= sret) return -8;    // tmp_cp overflow
    tmp_cp += sret;
    tmp_size -= sret;
  }
  return 0;
}
#endif

int
al_readline(FILE *fd, char **line_p, int *line_size_p)
{
  if (!fd || !line_p || !line_size_p) return -3;
  int len = -1;
  char *line = *line_p;
  int size = *line_size_p;
  int off = 0;
  int loff = 0;
  char *rp = fgets(line, size, fd);

  while (rp) {
    len = loff + strlen(line + loff);
    if (line[len - 1] == '\n') {  // read complete line
      line[--len] = '\0';
      break;
    }
    if (feof(fd)) /* EOF, skip realloc on EOF */
      break;

    /* read more */
    off += AL_LINE_INC;
    if (!(line = (char *)realloc(line, size + off))) return -2;
    loff = size + off - AL_LINE_INC - 1; // start at last '\0' pos
    rp = fgets(line + loff, AL_LINE_INC + 1, fd);
  }

  if (off) {
    *line_p = line;
    *line_size_p = size + off;
  }
  return len;
}

/****************************/
