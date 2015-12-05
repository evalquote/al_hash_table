/* example */
/* cc -O3 -o count count.c hash.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alhash.h"

struct al_hash_t *ht_count;

int
main() {
  int ret;
  int line_size = AL_LINE_SIZE; // default initial size
  char *line = (char *)malloc(line_size);
  int len;

  ret = al_init_hash(HASH_TYPE_SCALAR, AL_DEFAULT_HASH_BIT, &ht_count);
  if (ret < 0) {
    fprintf(stderr, "init ht_count %d\n", ret);
    exit(1);
  }
  al_set_hash_err_msg(ht_count, "ht_count:");

  while (0 <= (len = al_readline(stdin, &line, &line_size))) {
#if 0
    ret = item_inc_init(ht_count, line, (value_t)1, NULL);
    if (ret < 0) {
      fprintf(stderr, "inc %d\n", ret);
    }
#else
    int nelm = n_elements_nn(line, " \t");  // one more for last NULL
    char *elms[nelm];
    int retn = al_split_nn_impl(elms, nelm, line, line_size, line, " \t");
    if (retn < 0) {
      fprintf(stderr, "al_split_nn %d\n", ret);
      continue;
    }
    for (int i = 0; i < retn; i++) {
      ret = item_inc_init(ht_count, elms[i], (value_t)1, NULL);
      if (ret < 0) {
	fprintf(stderr, "inc %d\n", ret);
      }
    }
#endif
  }
  // al_out_hash_stat(ht_count, "ht_count");

  {
    struct al_hash_iter_t *itr;
    const char *ikey;
    value_t v;

    ret = al_hash_iter_init(ht_count, &itr, AL_SORT_COUNTER_DIC|AL_SORT_VALUE|AL_ITER_AE);
#if 0
    ret = al_hash_topk_iter_init(ht_count, &itr,
				 AL_SORT_COUNTER_DIC|AL_SORT_VALUE|AL_ITER_AE,
				 AL_FFK_HALF);
    ret = al_hash_iter_init(ht_count, &itr, AL_SORT_DIC|AL_ITER_AE);
    ret = al_hash_iter_init(ht_count, &itr, AL_SORT_NO|AL_ITER_AE);
#endif
    if (ret < 0)
      fprintf(stderr, "itr init %d\n", ret);

    while (0 <= (ret = al_hash_iter(itr, &ikey, &v))) {
      /*
      if (v < 2) { // exit loop before end of iteration, invoke _end() later.
	break;
      }
      */
      printf("%ld\t%s\n", v, ikey);
    }
    if (ret == 0) { // invoke _end() manually
      al_hash_iter_end(itr);
    }
  }

  ret = al_free_hash(ht_count);
  if (ret < 0)
    fprintf(stderr, "free ht_count %d\n", ret);
  free(line);

  exit(0);
}
