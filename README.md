Yet another reinventing the hash table library, sorry.

in C, NOT thread safe, with iterator.

API are as follows. (from hash.h)
    
    int al_init_hash(int bit, struct al_hash_t **htp)
    int al_free_hash(struct al_hash_t *ht)
    int al_hash_stat(struct al_hash_t *ht,
    		 struct al_hash_stat_t *statp,
    		 al_chain_lenght_t acl)
    int al_hash_n_iterators(struct al_hash_t *ht)
    int item_set(struct al_hash_t *ht, char *key, value_t v)
    int item_set_pv(struct al_hash_t *ht, char *key, value_t v, value_t *pv)
    int item_key(struct al_hash_t *ht, char *key)
    int item_get(struct al_hash_t *ht, char *key, value_t *ret_v)
    int item_replace(struct al_hash_t *ht, char *key, value_t v)
    int item_replace_pv(struct al_hash_t *ht, char *key, value_t v, value_t *ret_pv)
    int item_delete(struct al_hash_t *ht, char *key)
    int item_delete_pv(struct al_hash_t *ht, char *key, value_t *ret_pv)
    int item_inc(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
    int item_inc_init(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
    int al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp)
    int al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v)
    int al_hash_iter_end(struct al_hash_iter_t *iterp)
    int item_replace_iter(struct al_hash_iter_t *iterp, value_t v)
    int item_delete_iter(struct al_hash_iter_t *iterp)
    struct al_hash_t *al_hash_iter_ht(struct al_hash_iter_t *iterp)
