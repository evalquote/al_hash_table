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

  if ((ret = al_init_hash(0, &ht_count))) {
    fprintf(stderr, "init ht_count %d\n", ret);
    exit(1);
  }
  while (fgets(line, sizeof(line) - 1, stdin)) {
    int len = strlen(line);
    if (0 < len)
      line[len - 1] = '\0';
    if ((ret = item_inc_init(ht_count, line, (value_t)1, NULL)))
      fprintf(stderr, "inc %d\n", ret);
  }

  struct al_hash_iter_t *itr;
  const char *ikey;
  value_t v;

  ret = al_hash_iter_init(ht_count, &itr, 2);
  if (ret)
    fprintf(stderr, "itr init %d\n", ret);

  while (!(ret = al_hash_iter(itr, &ikey, &v)))
    printf("%ld %s\n", v, ikey);
  if (ret != -1)  /* -1: end of iteration, not abend */
    fprintf(stderr, "itr %d\n", ret);

  if ((ret = al_hash_iter_end(itr)))
    fprintf(stderr, "itr end %d\n", ret);

  if ((ret = al_free_hash(ht_count)))
      fprintf(stderr, "free %d\n", ret);
  exit(0);
}
