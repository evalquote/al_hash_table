#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

#define MEAN_CHAIN_LENGTH 2

struct item {
  struct item *chain;
  char *key;
  value_t value;
};

#define hash_size(n) (1<<(n))

struct al_hash_iter_t;

struct al_hash_t {
  unsigned int  hash_bit;
  unsigned long n_items;
  unsigned long n_items_old;
  unsigned long n_cancel_rehashing;
  unsigned int  n_rehashing;

  int rehashing;
  unsigned int rehashing_front;
  long moving_unit;

  unsigned int hash_mask;
  unsigned int hash_mask_old;

  struct item **hash_table;
  struct item **hash_table_old;
  struct al_hash_iter_t *iterators;
};

struct al_hash_iter_t {
  /*
   * if 0 <= index < hash_size(ht->hash_bit - 1)
   *   ht->hash_table_old[index]
   * else
   *   ht->hash_table[index - hash_size(ht->hash_table - 1)]
   */
  struct al_hash_t *ht;
  unsigned int index;
  unsigned int oindex;
  struct item **pplace;
  struct item **place;
  struct item *to_be_free;
  struct al_hash_iter_t *chain;
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

int
al_init_hash(int bit, struct al_hash_t **htp)
{
  int ret;
  struct al_hash_t *al_hash;

  if (!htp) return -3;
  al_hash = (struct al_hash_t *)calloc(1, sizeof(struct al_hash_t));
  if (!al_hash) {
    return -2;
  }
  if (bit == 0)
    bit = DEFAULT_HASH_BIT;

  ret = resize_hash(bit, al_hash);
  if (ret)
    return ret;

  al_hash->moving_unit = 2 * bit;
  *htp = al_hash;
  return 0;
}

static void
free_hash(struct item **itp, unsigned int start, unsigned int size)
{
  unsigned int i;
  for (i = start; i < size; i++) {
    struct item *it = itp[i];
    while (it) {
      struct item *next = it->chain;
      free(it->key);
      free(it);
      it = next;
    }
  }
}

static int
attach_iter(struct al_hash_t *ht, struct al_hash_iter_t *iterp)
{
  if (!ht)
    return -4;
  iterp->chain = ht->iterators;
  ht->iterators = iterp;

  struct al_hash_iter_t *ip = ht->iterators;
  while (ip) {
    ip = ip->chain;
  }
  return 0;
}

static void
detach_iter(struct al_hash_t *ht, struct al_hash_iter_t *iterp)
{
  if (!ht || !ht->iterators)
    return;

  struct al_hash_iter_t **pp = &ht->iterators;
  struct al_hash_iter_t *ip = ht->iterators;

  while (ip && ip != iterp) {
    pp = &ip->chain;
    ip = ip->chain;
  }
  if (!ip)
    return;
  *pp = ip->chain;
}

static void
count_chain(al_chain_lenght_t acl,
	    struct item **itp, unsigned int start, unsigned int size)
{
  unsigned int i;
  for (i = start; i < size; i++) {
    int count = 0;
    struct item *it = itp[i];
    while (it) {
      count++;
      it = it->chain;
    }
    if (count < 10)
      acl[count]++;
    else
      acl[10]++;
  }
}

int
al_free_hash(struct al_hash_t *ht)
{
  if (!ht) return -3;
  if (ht->rehashing) {
    free_hash(ht->hash_table_old, ht->rehashing_front, hash_size(ht->hash_bit - 1));
    free(ht->hash_table_old);
  }
  free_hash(ht->hash_table, 0, hash_size(ht->hash_bit));
  free(ht->hash_table);

  struct al_hash_iter_t *ip = ht->iterators;
  while (ip) {
    ip->ht = NULL;
    ip = ip->chain;
  }

  free(ht);
  return 0;
}

int
al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp)
{
  if (!ht || !iterp) return -3;
  unsigned int index;
  unsigned int old_size = hash_size(ht->hash_bit - 1);
  unsigned int total_size = old_size + hash_size(ht->hash_bit);
  struct item **place = NULL;

  struct al_hash_iter_t *ip = (struct al_hash_iter_t *)
                               calloc(1, sizeof(struct al_hash_iter_t));
  if (!ip)
    return -2;

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
  ip->ht = ht;
  *iterp = ip;
  attach_iter(ht, ip);
  return 0;
}

int
al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v)
{
  if (!iterp || !key) return -3;

  struct al_hash_t *ht = iterp->ht;

  if (!ht || !ht->iterators) return -4;

  unsigned int old_size = hash_size(ht->hash_bit - 1);
  unsigned int total_size = old_size + hash_size(ht->hash_bit);
  unsigned int index = iterp->index;
  struct item **place = iterp->place;

  if (iterp->to_be_free) {
    free(iterp->to_be_free->key);
    free(iterp->to_be_free);
    iterp->to_be_free = NULL;
  }

  if (!place || !*place) {
    *key = NULL;
    iterp->pplace = iterp->place = NULL;
    return -1;
  }

  struct item *it = *place;
  *key = it->key;
  if (ret_v)
    *ret_v = it->value;

  iterp->oindex = index;
  iterp->pplace = iterp->place;
  place = &it->chain;

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
al_hash_iter_end(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  if (iterp->to_be_free) {
    free(iterp->to_be_free->key);
    free(iterp->to_be_free);
    iterp->to_be_free = NULL;
  }

  struct al_hash_t *ht = iterp->ht;
  detach_iter(ht, iterp);
  free(iterp);
  return 0;
}

int
al_hash_stat(struct al_hash_t *ht,
	     struct al_hash_stat_t *statp,
	     al_chain_lenght_t acl) {
  if (!ht || !statp) return -3;

  statp->al_hash_bit = ht->hash_bit;
  statp->al_n_items = ht->n_items;
  statp->al_n_items_old = ht->n_items_old;
  statp->al_n_cancel_rehashing = ht->n_cancel_rehashing;
  statp->al_n_rehashing = ht->n_rehashing;

  if (!acl)
    return 0;

  memset((void *)acl, 0, sizeof(al_chain_lenght_t));

  if (ht->rehashing) {
    count_chain(acl, ht->hash_table_old, ht->rehashing_front,
		hash_size(ht->hash_bit - 1));
  }
  count_chain(acl, ht->hash_table, 0, hash_size(ht->hash_bit));

  return 0;
}

static void
moving(struct al_hash_t *ht)
{
  if (ht->iterators)
    return;
  long i;
  for (i = 0; i < ht->moving_unit; i++) {
    struct item *it = ht->hash_table_old[ht->rehashing_front];
    while (it) {
      unsigned int hindex = hash_fn_i(it->key) & ht->hash_mask;
      struct item *next = it->chain;
      it->chain = ht->hash_table[hindex];
      ht->hash_table[hindex] = it;
      ht->n_items_old--;
      ht->n_items++;
      it = next;
    }
    ht->hash_table_old[ht->rehashing_front] = NULL;
    ht->rehashing_front++;
    if (hash_size(ht->hash_bit-1) <= ht->rehashing_front) {
      free(ht->hash_table_old);
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
    ht->n_items_old = ht->n_items;
    ht->n_items = 0;
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

  if (ht->rehashing && ht->rehashing_front <= (hindex = (hv & ht->hash_mask_old))) {
    it = ht->hash_table_old[hindex];
  } else {
    it = ht->hash_table[hv & ht->hash_mask];
  }

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
    ht->n_items_old++;
  } else {
    it->chain = ht->hash_table[hv & ht->hash_mask];
    ht->hash_table[hv & ht->hash_mask] = it;
    ht->n_items++;
  }
  if (ht->rehashing) {
    moving(ht);
    if (ht->hash_mask * (MEAN_CHAIN_LENGTH + 1) < ht->n_items) {
      ht->n_cancel_rehashing++;
    }
  } else if (!ht->rehashing && !ht->iterators &&
	     ht->hash_mask * (MEAN_CHAIN_LENGTH + 1) < ht->n_items) {
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
  it->value = v;
  it->key = strdup(key);
  if (!it->key) {
    free(it);
    return -2;
  }
  ret = hash_insert(ht, hv, key, it);
  if (ret) {
    free(it->key);
    free(it);
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
  while (it) {
    if (strcmp(key, it->key) == 0) {
       break;
    }
    place = &it->chain;
    it = it->chain;
  }
  if (!it)
    return NULL;

  *place = it->chain;

  if (old)
    ht->n_items_old--;
  else
    ht->n_items--;

  return it;
}

/************************/

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
    if (v)
      *v = ret->value;
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
    it->value = v;
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
    if (ret_pv)
      *ret_pv = it->value;
    it->value = v;
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
    it->value = v;
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
    if (ret_pv)
      *ret_pv = it->value;
    it->value = v;
    return 0;
  }
  return -1;
}

int
item_delete(struct al_hash_t *ht, char *key)
{
  if (!ht || !key) return -3;
  if (ht->iterators) return -5;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_delete(ht, key, hv);
  if (it) {
    free(it->key);
    free(it);
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
    if (ret_pv)
      *ret_pv = it->value;
    free(it->key);
    free(it);
    return 0;
  }
  return -1;
}

int
item_inc(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->value += off;
    if (ret_v)
      *ret_v = it->value;
    return 0;
  }
  return -1;
}

int
item_inc_init(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
{
  if (!ht || !key) return -3;
  unsigned int hv = hash_fn_i(key);
  struct item *it = hash_find(ht, key, hv);
  if (it) {
    it->value += off;
    if (ret_v)
      *ret_v = it->value;
    return 0;
  }
  return hash_v_insert(ht, hv, key, (value_t)off);
}

int
item_replace_iter(struct al_hash_iter_t *iterp, value_t v)
{
  if (!iterp) return -3;
  if (!iterp->ht || !iterp->ht->iterators) return -4;
  if (!iterp->pplace) return -1;

  (*iterp->pplace)->value = v;
  return 0;
}

int
item_delete_iter(struct al_hash_iter_t *iterp)
{
  if (!iterp) return -3;
  if (!iterp->ht || !iterp->ht->iterators) return -4;
  if (!iterp->pplace) return -1;

  struct item *p_it = *iterp->pplace;
  unsigned int old_size = hash_size(iterp->ht->hash_bit - 1);

  if ((void *)p_it == (void *)iterp->place)
    iterp->place = iterp->pplace;

  *iterp->pplace = p_it->chain;

  iterp->to_be_free = p_it;
  iterp->pplace = NULL; /* avoid double free */
  if (iterp->oindex < old_size)
    iterp->ht->n_items_old--;
  else
    iterp->ht->n_items--;
  return 0;
}
