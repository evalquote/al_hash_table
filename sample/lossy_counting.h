/* lossy_counting.h
 *
 *  Motwani, R; Manku, G.S (2002). "Approximate frequency counts over
 *  data streams". VLDB '02 Proceedings of the 28th international
 *  conference on Very Large Data Bases: pp. 346-357. 
 */

/* 
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 */

#ifndef AL_LC_H
#define AL_LC_H

struct lc;
int lc_make_counter(double eps, struct lc **lcp);
int lc_count_up(struct lc *lcp, char *line, value_t *ret_v);
int lc_free_counter(struct lc *lcp);
int lc_stat(struct lc *lcp);

struct lc_iter;
int lc_counter_iter_init(struct lc *lcp, int tc, struct lc_iter **ret_iter, int sort);
int lc_counter_iter(struct lc_iter *itr, const char **ikey, value_t *retv, value_t *ret_b);
int lc_counter_delete_iter(struct lc_iter *itr);
int lc_counter_iter_end(struct lc_iter *itr);

#endif
