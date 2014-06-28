#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define AL_HASH_O
#include "hash.h"
#undef AL_HASH_O

/* type of linked value */
typedef  struct lvt_ {struct lvt_ *link; link_value_t value;} link_t;

#define MEAN_CHAIN_LENGTH 2

/* hash entry, with unique key */

struct item {
  struct item *chain;
  char *key;
  union {
    value_t value;
    link_t *link;
  } u;
};

#define HASH_FLAG_SCALAR 0x1
#define HASH_FLAG_LINKED 0x2

#define hash_size(n) (1<<(n))

struct al_hash_iter_t;

struct al_hash_t {
  unsigned int  hash_bit;
  unsigned int  n_rehashing;
  unsigned long n_entries;	/* number of items in hash_table */
  unsigned long n_entries_old;	/* number of items in hash_table_old */
  unsigned long n_cancel_rehashing;

  int rehashing;		/* 1: under re-hashing */
  unsigned int rehashing_front;
  long moving_unit;

  unsigned int hash_mask;
  unsigned int hash_mask_old;

  struct item **hash_table;	/* main hash table */
  struct item **hash_table_old;
  struct al_hash_iter_t *iterators;	/* iterators attached to me */
  unsigned int flag;
};

/* iterator to hash table */
struct al_hash_iter_t {
  /*
   * if 0 <= index < hash_size(ht->hash_bit - 1)
   *   ht->hash_table_old[index]
   * else
   *   ht->hash_table[index - hash_size(ht->hash_table - 1)]
   */
  struct al_hash_t *ht;
  unsigned int index;
  unsigned int oindex;	// max index when sort_key
  struct item **pplace;
  struct item **place;
  struct item *to_be_free;
  struct item **sorted;
  struct al_hash_iter_t *chain;
  unsigned int flag;	/* copy of ht->flag */
};

struct al_linked_value_iter_t {
  link_t *link, *current;
  link_t **sorted;
  unsigned int index;
  unsigned int max_index;
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

static
uint32_t
hash_fn_i(char *cp)
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
  if (!hash_table) {
    return -2;
  }
  ht->hash_table = hash_table;
  ht->hash_bit = bit;
  ht->hash_mask = hash_size(bit)-1;
  ht->hash_mask_old = hash_size(bit-1)-1;

  return 0;
}

static int
init_hash(int bit, struct al_hash_t **htp)
{
  if (!htp) return -3;
  *htp = NULL;

  struct al_hash_t *al_hash = (struct al_hash_t *)calloc(1, sizeof(struct al_hash_t));
  if (!al_hash) return -2;

  if (bit == 0)
    bit = DEFAULT_HASH_BIT;

  int ret = resize_hash(bit, al_hash);
  if (ret) return ret;

  al_hash->moving_unit = 2 * bit;
  *htp = al_hash;
  return 0;
}

int
al_init_hash(int bit, struct al_hash_t **htp)
{
  int ret = init_hash(bit, htp);
  if (!ret) (*htp)->flag = HASH_FLAG_SCALAR;
  return ret;
}

int
al_init_linked_hash(int bit, struct al_hash_t **htp)
{
  int ret = init_hash(bit, htp);
  if (!ret) (*htp)->flag = HASH_FLAG_LINKED;
  return ret;
}

static void
free_hash(struct item **itp, unsigned int start, unsigned int size)
{
  unsigned int i;
  for (i = start; i < size; i++) {
    struct item *it = itp[i];
    while (it) {
      struct item *next = it->chain;
      free((void *)it->key);
      free((void *)it);
      it = next;
    }
  }
}

static void
free_link_value(link_t *lp)
{
  while (lp) {
    link_t *nextp = lp->link;
    al_free_link_value(lp->value);
    free((void *)lp);
    lp = nextp;
  }
}

static void
free_link_hash(struct item **itp, unsigned int start, unsigned int size)
{
  unsigned int i;
  for (i = start; i < size; i++) {
    struct item *it = itp[i];
    while (it) {
      struct item *next = it->chain;
      free_link_value(it->u.link);
      free((void *)it->key);
      free((void *)it);
      it = next;
    }
  }
}

int
al_free_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  if (ht->rehashing) {
    if (ht->flag & HASH_FLAG_SCALAR)
      free_hash(ht->hash_table_old, ht->rehashing_front, hash_size(ht->hash_bit - 1));
    else if (ht->flag & HASH_FLAG_LINKED)
      free_link_hash(ht->hash_table_old, ht->rehashing_front, hash_size(ht->hash_bit - 1));
    else
      abort();
    free((void *)ht->hash_table_old);

  }
  if (ht->flag & HASH_FLAG_SCALAR)
    free_hash(ht->hash_table, 0, hash_size(ht->hash_bit));
  else if (ht->flag & HASH_FLAG_LINKED)
    free_link_hash(ht->hash_table, 0, hash_size(ht->hash_bit));
  else
    abort();
  free((void *)ht->hash_table);

  struct al_hash_iter_t *ip;
  for (ip = ht->iterators; ip; ip = ip->chain)
    ip->ht = NULL;
  free((void *)ht);
  return 0;
}

static int
attach_iter(struct al_hash_t *ht, struct al_hash_iter_t *iterp)
{
  if (!ht) return -4;
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
add_chain_to_array(struct item **it_array, unsigned int index,
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

int
al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp, int sort_key)
{
  if (!ht || !iterp) return -3;
  *iterp = NULL;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip) return -2;
  if (sort_key < 0 || 2 < sort_key) return -7;

  if (sort_key) {
    long sidx = 0;
    struct item **it_array = NULL;

    it_array = (struct item **)malloc(sizeof(struct item *) *
				      (ht->n_entries + ht->n_entries_old));
    if (!it_array) {
      free((void *)ip);
      return -2;
    }

    if (ht->rehashing) {
      sidx = add_chain_to_array(it_array, sidx, ht->hash_table_old,
				ht->rehashing_front, hash_size(ht->hash_bit - 1),
				ht->n_entries_old);
      if (sidx != ht->n_entries_old) {
	free((void *)it_array);
	free((void *)ip);
	return -99;
      }
    }
    sidx = add_chain_to_array(it_array, sidx, ht->hash_table,
			      0, hash_size(ht->hash_bit), ht->n_entries);
    if (sidx != ht->n_entries + ht->n_entries_old) {
      free((void *)it_array);
      free((void *)ip);
      return -99;
    }
    if (sort_key == 1)
      qsort((void *)it_array, sidx, sizeof(struct item *), it_cmp);
    else
      qsort((void *)it_array, sidx, sizeof(struct item *), itn_cmp);
    ip->sorted = it_array;
    ip->oindex = sidx;	// max index
  } else {

    unsigned int index;
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

  ip->flag = ht->flag;
  ip->ht = ht;
  *iterp = ip;
  attach_iter(ht, ip);
  return 0;
}

static int
advance_iter(struct al_hash_iter_t *iterp, struct item **it)
{
  if (iterp->to_be_free) {
    free((void *)iterp->to_be_free->key);
    if (iterp->flag & HASH_FLAG_LINKED)
      free_link_value(iterp->to_be_free->u.link);
    free((void *)iterp->to_be_free);
    iterp->to_be_free = NULL;
  }

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

int
al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v)
{
  struct item *it;
  if (!iterp || !key) return -3;

  *key = NULL;
  int ret = advance_iter(iterp, &it);
  if (!ret) {
    *key = it->key;
    if (ret_v) *ret_v = it->u.value;
  }
  return ret;
}

int
al_linked_hash_nvalue(struct al_linked_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->max_index) return vip->max_index;
  unsigned int nvalue = 0;
  link_t *lp;
  for (lp = vip->link; lp; lp = lp->link)
    nvalue++;
  vip->max_index = nvalue;
  return nvalue;
}

int
al_linked_hash_rewind_value(struct al_linked_value_iter_t *vip)
{
  if (!vip) return -3;
  if (!vip->sorted || vip->max_index == 1)
    vip->current = vip->link;
  else
    vip->index = 0;

  return 0;
}

static int
v_cmp(const void *a, const void *b)
{
  return al_link_value_cmp((*(link_t **)a)->value, (*(link_t **)b)->value);
}

static int
vn_cmp(const void *a, const void *b)
{
  return -al_link_value_cmp((*(link_t **)a)->value, (*(link_t **)b)->value);
}

int
al_linked_hash_iter(struct al_hash_iter_t *iterp, const char **key,
		    struct al_linked_value_iter_t **v_iterp, int sort_value)
{
  if (!iterp || !key) return -3;

  *key = NULL;
  if (!(iterp->flag & HASH_FLAG_LINKED)) return -6;
  if (sort_value < 0 || 2 < sort_value) return -7;

  struct item *it;
  int ret = advance_iter(iterp, &it);
  if (ret) return ret;

  *key = it->key;

  if (!v_iterp) return 0;
  *v_iterp = NULL;

  struct al_linked_value_iter_t *vip =
    (struct al_linked_value_iter_t *)calloc(1, sizeof(struct al_linked_value_iter_t));
  if (!vip) return -2;

  if (!sort_value || !it->u.link->link) {
    vip->link = vip->current = it->u.link;
    vip->max_index = sort_value ? 1 : 0;
  } else {
    unsigned int nvalue = vip->max_index;
    link_t *lp;
    if (nvalue == 0) { // al_linked_hash_nvalue() set vip->max_index as correct value
      for (lp = it->u.link; lp; lp = lp->link)
	nvalue++;	   
    }
    link_t **sarray = (link_t **)malloc(nvalue * sizeof(link_t *));
    if (!sarray) {
      free((void *)vip);
      return -2;
    }
    vip->max_index = nvalue;
    for (nvalue = 0, lp = it->u.link; lp; lp = lp->link)
      sarray[nvalue++] = lp;
    if (sort_value == 1)
      qsort((void *)sarray, nvalue, sizeof(link_t *), v_cmp);
    else
      qsort((void *)sarray, nvalue, sizeof(link_t *), vn_cmp);
    vip->sorted = sarray;
  }
  *v_iterp = vip;

  return 0;
}

int
al_linked_value_iter(struct al_linked_value_iter_t *v_iterp, link_value_t *retv)
{	
  if (!v_iterp) return -3;
  link_t *rp = NULL;

  if (v_iterp->sorted) {
    if (v_iterp->max_index <= v_iterp->index) return -1;
    rp = v_iterp->sorted[v_iterp->index++];
  } else {
    if (!v_iterp->current) return -1;
    rp = v_iterp->current;
    v_iterp->current = rp->link;
  }
  if (retv) *retv = rp->value;

  return 0;
}

int
al_linked_value_iter_end(struct al_linked_value_iter_t *vip)
{
  if (!vip) return -3;
  if (vip->sorted) free((void *)vip->sorted);
  free(vip);
  return 0;
}

int
al_hash_iter_end(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;

  if (iterp->to_be_free) {
    free((void *)iterp->to_be_free->key);
    if (iterp->flag & HASH_FLAG_LINKED)
      free_link_value(iterp->to_be_free->u.link);
    free((void *)iterp->to_be_free);
    iterp->to_be_free = NULL;
  }

  detach_iter(iterp->ht, iterp);

  if (iterp->sorted) free((void *)iterp->sorted);
  free(iterp);

  return 0;
}

int
al_hash_n_iterators(struct al_hash_t *ht)
{
  if (!ht) return -3;
  int ret = 0;
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

int
al_is_link_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  return ht->flag & HASH_FLAG_LINKED ? 0 : -1;
}

int
al_is_link_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  return iterp->flag & HASH_FLAG_LINKED ? 0 : -1;
}

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
  int ret;
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

static void
moving(struct al_hash_t *ht)
{
  if (ht->iterators) return;
  long i;
  for (i = 0; i < ht->moving_unit; i++) {
    struct item *it = ht->hash_table_old[ht->rehashing_front];
    while (it) {
      unsigned int hindex = hash_fn_i(it->key) & ht->hash_mask;
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
start_rehashing(struct al_hash_t *ht)
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

  struct item *ret = NULL;
  while (it) {
    if (strcmp(key, it->key) == 0) {
      ret = it;
      break;
    }
    it = it->chain;
  }
  return ret;
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
    if (ht->hash_mask * (MEAN_CHAIN_LENGTH + 1) < ht->n_entries) {
      ht->n_cancel_rehashing++;
    }
  } else if (!ht->rehashing && !ht->iterators &&
	     ht->hash_mask * (MEAN_CHAIN_LENGTH + 1) < ht->n_entries) {
    ret = start_rehashing(ht);
  }
  return ret;
}

static int
hash_v_insert(struct al_hash_t *ht, unsigned int hv, char *key, value_t v)
{
  struct item *it = (struct item *)malloc(sizeof(struct item));
  int ret;

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
  struct item **place = NULL;
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

/************************/

/* either scalar and linked ht acceptable */

int
item_key(struct al_hash_t *ht, char *key)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *ret = hash_find(ht, key, hv);
  return ret ? 0 : -1;
}

int
item_get(struct al_hash_t *ht, char *key, value_t *v)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *ret = hash_find(ht, key, hv);

  if (ret) {
    if (v) *v = ret->u.value;
    return 0;
  }
  return -1;
}

int
item_set(struct al_hash_t *ht, char *key, value_t v)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);

  if (it) {
    it->u.value = v;
    return 0;
  }
  return hash_v_insert(ht, hv, key, v);
}

int
item_set_pv(struct al_hash_t *ht, char *key, value_t v, value_t *ret_pv)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    if (ret_pv) *ret_pv = it->u.value;
    it->u.value = v;
    return 0;
  }
  return hash_v_insert(ht, hv, key, v);
}

int
item_replace(struct al_hash_t *ht, char *key, value_t v)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->u.value = v;
    return 0;
  }
  return -1;
}

int
item_replace_pv(struct al_hash_t *ht, char *key, value_t v, value_t *ret_pv)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    if (ret_pv) *ret_pv = it->u.value;
    it->u.value = v;
    return 0;
  }
  return -1;
}

/* either scalar and linked ht acceptable */
int
item_delete(struct al_hash_t *ht, char *key)
{
  if (!ht || !key) return -3;
  if (ht->iterators) return -5;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_delete(ht, key, hv);
  if (it) {
    if (ht->flag & HASH_FLAG_LINKED)
      free_link_value(it->u.link);
    free((void *)it->key);
    free((void *)it);
    return 0;
  }
  return -1;
}

int
item_delete_pv(struct al_hash_t *ht, char *key, value_t *ret_pv)
{
  if (!ht || !key) return -3;
  if (ht->iterators) return -5;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_delete(ht, key, hv);
  if (it) {
    if (ret_pv) *ret_pv = it->u.value;
    free((void *)it->key);
    free((void *)it);
    return 0;
  }
  return -1;
}

int
item_inc(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
{
  if (!ht || !key) return -3;
  if (!(ht->flag & HASH_FLAG_SCALAR)) {
    fprintf(stderr, "inc link\n");
    abort();
  }
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->u.value += off;

    if (ret_v) *ret_v = it->u.value;
    return 0;
  }
  return -1;
}

int
item_inc_init(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
{
  if (!ht || !key) return -3;
  if (!(ht->flag & HASH_FLAG_SCALAR)) {
    fprintf(stderr, "inc link\n");
    abort();
  }
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->u.value += off;
    if (ret_v) *ret_v = it->u.value;
    return 0;
  }
  return hash_v_insert(ht, hv, key, (value_t)off);
}

int
item_add_value(struct al_hash_t *ht, char *key, link_value_t v)
{
  if (!ht || !key) return -3;
  if (!(ht->flag & HASH_FLAG_LINKED)) return -6;
  link_t *lp = (link_t *)malloc(sizeof(link_t));
  if (!lp) return -2;
  int ret = al_link_value(v, &lp->value);  // ex. strdup(v)
  if (ret) {
    free((void *)lp);
    return ret;
  }

  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) { /* found, add link */
    lp->link = it->u.link;
    it->u.link = lp;
    return 0;
  }

  it = (struct item *)malloc(sizeof(struct item));
  if (!it) {
    al_free_link_value(lp->value);
    free((void *)lp);
    return -2;
  }

  it->key = strdup(key);
  if (!it->key) {
    al_free_link_value(lp->value);
    free((void *)lp);
    free((void *)it);
    return -2;
  }
  lp->link = NULL;
  it->u.link = lp;

  ret = hash_insert(ht, hv, key, it);
  if (ret) {
    al_free_link_value(lp->value);
    free((void *)lp);
    free((void *)it->key);
    free((void *)it);
  }
  return ret;
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
  it = hash_delete(iterp->ht, it->key, hash_fn_i(it->key));
  if (!it) return -1;
  iterp->to_be_free = it;
  return 0;
}

/* either scalar and linked ht acceptable */
int
item_delete_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  if (!iterp->ht || !iterp->ht->iterators) return -4;

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

/***/

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

#if 0
void
al_split_impl(char **elms, int size, char *tmp_cp, int tmp_size, const char *str, char del)
{
  char **ap;
  strncpy(tmp_cp, str, tmp_size);
  for (ap = elms;;) {
    *ap++ = al_gettok(tmp_cp, &tmp_cp, del);
    if (!tmp_cp || &elms[size] <= ap) break;
  }
  while (ap < &elms[size]) *ap++ = NULL;
}
#endif

void
al_split_impl(char **elms, int size, char *tmp_cp, int tmp_size, const char *str, const char *dels)
{
  char **ap;
  strncpy(tmp_cp, str, tmp_size);
  for (ap = elms; (*ap = strsep(&tmp_cp, dels)) != NULL;)
    if (++ap >= &elms[size]) break;
  while (ap < &elms[size]) *ap++ = NULL;
}

void
al_split_nn_impl(char **elms, int size, char *tmp_cp, int tmp_size, const char *str, const char *dels)
{
  char **ap;
  strncpy(tmp_cp, str, tmp_size);
  for (ap = elms; (*ap = strsep(&tmp_cp, dels)) != NULL;)
    if (*ap != '\0' && ++ap >= &elms[size]) break;
  while (ap < &elms[size]) *ap++ = NULL;
}

/****************************/
