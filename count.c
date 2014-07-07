/* example */
/* cc -O2 -o count count.c hash.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

struct al_hash_t *ht_count;

int
main() {
  int ret;
  char line[512];

  ret = al_init_hash(HASH_TYPE_SCALAR, AL_DEFAULT_HASH_BIT, &ht_count);
  if (ret) {
    fprintf(stderr, "init ht_count %d\n", ret);
    exit(1);
  }
  al_set_hash_err_msg(ht_count, "ht_count:");

  while (fgets(line, sizeof(line), stdin)) {
    int len = strlen(line);
    if (0 < len)
      line[len - 1] = '\0';
#if 0
    ret = item_inc_init(ht_count, line, (value_t)1, NULL);
    if (ret)
      fprintf(stderr, "inc %d\n", ret);
#else
    char *ap, *tmp_cp;
    tmp_cp = line;
    while ((ap = strsep(&tmp_cp, " \t")) != NULL) {
      if (*ap == '\0') continue;
      ret = item_inc_init(ht_count, ap, (value_t)1, NULL);
      if (ret)
	fprintf(stderr, "inc %d\n", ret);
    }
#endif
  }

  {
    struct al_hash_iter_t *itr;
    const char *ikey;
    value_t v;

    ret = al_hash_iter_init(ht_count, &itr, AL_SORT_COUNTER_DIC|AL_SORT_VALUE|AL_ITER_AE);
    if (ret)
      fprintf(stderr, "itr init %d\n", ret);

    while (!(ret = al_hash_iter(itr, &ikey, &v))) {
      if (v < 2) { // exit loop before end of iteration, invoke _end() later.
	break;
      }
      printf("%ld\t%s\n", v, ikey);
    }
    if (ret == 0) { // invoke _end() manually
      al_hash_iter_end(itr);
    }
  }

  ret = al_free_hash(ht_count);
  if (ret)
    fprintf(stderr, "free ht_count %d\n", ret);

  exit(0);
}
