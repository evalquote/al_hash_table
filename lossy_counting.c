/* 
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"
#include "lossy_counting.h"

struct lc {
  struct al_hash_t *ht_count;
  long nitem;
  double epsilon;
  long b_current;
  long bunit;
  long n_del_item;
  int ntrim;
};

#define LC_VBIT 24
#define com(b, v) (((v)<<LC_VBIT)|((b)&((1UL<<LC_VBIT)-1)))
#define sep(b, v) do{(b)=(v)&((1UL<<LC_VBIT)-1); (v)>>=LC_VBIT;}while(0)

int
lc_make_counter(double eps, struct lc **pret)
{
  int ret;
  if (!pret) return -3;

  struct lc *lcp = (struct lc *)calloc(1, sizeof(struct lc));
  if (!lcp) return -2;

  ret = al_init_hash(HASH_TYPE_SCALAR, AL_DEFAULT_HASH_BIT, &lcp->ht_count);
  if (ret < 0) {
    free((void *)lcp);
    return ret;
  }
  al_set_hash_err_msg(lcp->ht_count, "ht_count:" _AL_FLS);

  lcp->epsilon = eps;
  lcp->bunit = (long)(1.0 / eps);
  lcp->b_current = 1;
  *pret = lcp;

  return 0;
}

int
lc_free_counter(struct lc *lcp)
{
  int ret;
  if (!lcp) return -3;

  ret = al_free_hash(lcp->ht_count);
  free((void *)lcp);
  return ret;
}

static void
trim(struct lc *lcp)
{
  int ret;
  const char *key;
  value_t v, b;

  struct al_hash_iter_t *itr;
  ret = al_hash_iter_init(lcp->ht_count, &itr, AL_SORT_NO|AL_ITER_AE);
  if (ret < 0)
    fprintf(stderr, "trim itr init %d\n", ret);
  lcp->ntrim++;
  while (0 <= (ret = al_hash_iter(itr, &key, &v))) {
    sep(b, v);
    if (v <= lcp->b_current - b) {
      ret = item_delete_iter(itr);
      if (ret < 0)
	fprintf(stderr, "trim delete %d\n", ret);
      lcp->n_del_item++;
    }
  }
}

int
lc_count_up(struct lc *lcp, char *line, value_t *ret_v)
{
  int ret = 0;
  if (!lcp || !line) return -3;

  value_t v = -1;
  lcp->nitem++;

  ret = item_inc_init(lcp->ht_count, line, (1U<<LC_VBIT), &v);
  if (ret < 0) return ret;
  if (v == -1) { /* first appearance */
    item_replace(lcp->ht_count, line, com((lcp->b_current - 1), 1));
    v = 1;
  }

  if ((lcp->nitem % lcp->bunit) == 0) {
    trim(lcp);
    lcp->b_current++;
  }
  if (ret_v) {
    value_t b = 0;
    sep(b, v);
    *ret_v = v;
  }
  return 0;
}

struct lc_iter {
  struct al_hash_iter_t *hitr;
  int tc;
  int lc_flag;
};

int
lc_counter_iter_init(struct lc *lcp, int tc, struct lc_iter **ret_iter, int sort)
{
  int ret = 0;
  struct al_hash_iter_t *hitr = NULL;

  if (!lcp || !ret_iter) return -3;
  if (tc < 0) return -7;

  int est = (int)(lcp->epsilon * lcp->nitem);

  trim(lcp);

  struct lc_iter *itr= (struct lc_iter *)malloc(sizeof(struct lc_iter));
  if (!itr) return -2;

  ret = al_hash_iter_init(lcp->ht_count, &hitr, sort & ~AL_ITER_AE);
  if (ret < 0) {
    free((void *)itr);
    return -2;
  }

  // when tc == 0, return all
  if (0 < tc && tc <= est) {
    int ntc = est + 1;
    // fprintf(stderr, "small tc %d < %d set to %d\n", est, tc, ntc);
    tc = ntc;
  }
  itr->hitr = hitr;
  itr->tc = tc;
  itr->lc_flag = sort;
  *ret_iter = itr;
  return 0;
}

int
lc_counter_iter(struct lc_iter *itr, const char **ikey, value_t *retv, value_t *retb)
{
  int ret = -3;
  const char *k = NULL;
  value_t v = 0;
  value_t b = 0;

  if (retv) *retv = 0;
  if (retb) *retb = 0;
  *ikey = NULL;
  if (itr) {
    while (0 <= (ret = al_hash_iter(itr->hitr, &k, &v))) {
      b = 0;
      sep(b, v);
      if (itr->tc <= v + b) { // b: maximum error.
	*ikey = k;
	if (retv) *retv = v;
	if (retb) *retb = b;
	break;
      }
    }
  }
  if (ret < 0 && (itr->lc_flag & AL_ITER_AE)) {
    if (ret == -1)
      lc_counter_iter_end(itr);
    else
      fprintf(stderr, "lc_counter_iter advance error (code=%d)\n", ret);
  }
  return ret;
}

int
lc_counter_delete_iter(struct lc_iter *itr)
{
  if (!itr) return -3;
  return item_delete_iter(itr->hitr);
}

int
lc_counter_iter_end(struct lc_iter *itr)
{
  if (!itr) return -3;
  al_hash_iter_end(itr->hitr);
  free((void *)itr);
  return 0;
}

int
lc_stat(struct lc *lcp)
{
  if (!lcp) return -3;
  fprintf(stderr, "bunit %ld  nitem %ld  b_current %ld  ntrim %d  del_item %ld\n",
	  lcp->bunit, lcp->nitem, lcp->b_current, lcp->ntrim, lcp->n_del_item);
  return al_out_hash_stat(lcp->ht_count, "lossy counter hash");
}

void
print_counter(struct lc *lcp, int tc)
{
  int ret = 0;
  const char *ikey;
  value_t v, b;
  struct lc_iter *itr = NULL;
  
  ret = lc_counter_iter_init(lcp, tc, &itr,
			     AL_SORT_COUNTER_DIC|AL_SORT_VALUE|AL_SORT_NUMERIC|AL_ITER_AE);
  if (ret < 0) fprintf(stderr, "print counter iter init %d\n", ret);
  
  while (0 <= (ret = lc_counter_iter(itr, &ikey, &v, &b)))
    printf("%s\t%ld\t%ld\n", ikey, v, b);

  // lc_stat(lcp);
}

int
main()
{
  int ret = 0;
  char line[1024];
  struct lc *lcp = NULL;

  ret = lc_make_counter(1e-6, &lcp);
  if (ret < 0) {
    fprintf(stderr, "lc_make_counter %d\n", ret);
    exit(1);
  }

  while (fgets(line, sizeof(line), stdin)) {
    int len = strlen(line);
    if (0 < len) line[len - 1] = '\0';
#if 0
    ret = lc_count_up(lcp, line, NULL);
    if (ret < 0)
      fprintf(stderr, "count_up exit %d\n", ret);
#else
    char *ap, *tmp_cp;
    tmp_cp = line;
    while ((ap = strsep(&tmp_cp, " \t")) != NULL) {
      if (*ap == '\0') continue;
      ret = lc_count_up(lcp, ap, NULL);
      if (ret < 0)
	fprintf(stderr, "count_up exit %d\n", ret);
    }
#endif
  }

  print_counter(lcp, 20);

  lc_free_counter(lcp);
  exit(0);
}
