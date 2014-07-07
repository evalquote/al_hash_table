Yet another reinventing the hash table library, sorry.

in C, NOT thread safe, with iterator.

API are as follows. (from hash.h, not updated)
    
    int al_init_hash(int bit, struct al_hash_t **htp)
    int al_init_linked_hash(int bit, struct al_hash_t **htp)  // save value part to link
    int al_init_pqueue_hash(int bit, struct al_hash_t **htp,  // save value part to priority queue
                            int sort_order, unsigned long max_n)
    int al_free_hash(struct al_hash_t *ht)
    int al_hash_stat(struct al_hash_t *ht,
                     struct al_hash_stat_t *statp,
                     al_chain_lenght_t acl)
    int al_hash_n_iterators(struct al_hash_t *ht)
    int item_set(struct al_hash_t *ht, char *key, value_t v)
    int item_set_pv(struct al_hash_t *ht, char *key, value_t v, value_t *pv)
    int item_add_value(struct al_hash_t *ht, char *key, link_value_t v) // add to link/pqueue
    int item_key(struct al_hash_t *ht, char *key)
    int item_get(struct al_hash_t *ht, char *key, value_t *ret_v)
    int item_get_str(struct al_hash_t *ht, char *key, link_value_t *v)
    int item_replace(struct al_hash_t *ht, char *key, value_t v)
    int item_replace_pv(struct al_hash_t *ht, char *key, value_t v, value_t *ret_pv)
    int item_replace_str(struct al_hash_t *ht, char *key, link_value_t v)
    int item_delete(struct al_hash_t *ht, char *key)
    int item_delete_pv(struct al_hash_t *ht, char *key, value_t *ret_pv)
    int al_linked_hash_get(struct al_hash_t *ht, char *key,
		           struct al_linked_value_iter_t **v_iterp, int flag)
    int al_pqueue_hash_get(struct al_hash_t *ht, char *key,
		           struct al_pqueue_value_iter_t **v_iterp, int flag)
    int item_inc(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
    int item_inc_init(struct al_hash_t *ht, char *key, long off, value_t *ret_v)
    int al_hash_iter_init(struct al_hash_t *ht, struct al_hash_iter_t **iterp, int sort_key)
    int al_hash_iter(struct al_hash_iter_t *iterp, const char **key, value_t *ret_v)
    int al_hash_iter_end(struct al_hash_iter_t *iterp)
    int item_replace_iter(struct al_hash_iter_t *iterp, value_t v)
    int item_delete_iter(struct al_hash_iter_t *iterp)
    struct al_hash_t *al_hash_iter_ht(struct al_hash_iter_t *iterp)

    int al_linked_hash_iter(struct al_hash_iter_t *iterp, const char **key,
                            struct al_linked_value_iter_t **v_iterp, int sort_value)
    int al_linked_value_iter(struct al_linked_value_iter_t *v_iterp, link_value_t *ret_v)
    int al_linked_value_iter_end(struct al_linked_value_iter_t *v_iterp)
    int al_linked_hash_nvalue(struct al_linked_value_iter_t *v_iterp)
    int al_linked_hash_rewind_value(struct al_linked_value_iter_t *v_iterp)

    int al_pqueue_hash_iter(struct al_hash_iter_t *iterp, const char **key,
                            struct al_pqueue_value_iter_t **v_iterp, int flag)
    int al_pqueue_value_iter_end(struct al_pqueue_value_iter_t *v_iterp)
    int al_pqueue_value_iter(struct al_pqueue_value_iter_t *v_iterp,
                             link_value_t *keyp, value_t *ret_count)
    int al_pqueue_hash_nvalue(struct al_pqueue_value_iter_t *v_iterp)
    int al_pqueue_hash_rewind_value(struct al_pqueue_value_iter_t *v_iterp)

    int al_create_skiplist(struct al_skiplist_t **slp, int sort_order)
    int al_free_skiplist(struct al_skiplist_t *sl)
    int sl_empty_p(struct al_skiplist_t *sl)
    unsigned long sl_n_entries(struct al_skiplist_t *sl)
    int sl_skiplist_stat(struct al_skiplist_t *sl)
    int sl_set(struct al_skiplist_t *sl, pq_key_t key, value_t v)
    int sl_set_n(struct al_skiplist_t *sl, pq_key_t key, value_t v, unsigned long max_n)
    int sl_delete(struct al_skiplist_t *sl, pq_key_t key)
    int sl_delete_last_node(struct al_skiplist_t *sl)
    int sl_key(struct al_skiplist_t *sl, pq_key_t key)
    int sl_get(struct al_skiplist_t *sl, pq_key_t key, value_t *ret_v)
    int sl_inc_init(struct al_skiplist_t *sl, pq_key_t key, long off, value_t *ret_v)
    int sl_inc_init_n(struct al_skiplist_t *sl, pq_key_t key, long off, value_t *ret_v, unsigned long max_n)
    int al_sl_iter_init(struct al_skiplist_t *sl, struct al_skiplist_iter_t **iterp)
    int al_sl_iter_end(struct al_skiplist_iter_t *iterp)
    int al_sl_iter(struct al_skiplist_iter_t *iterp, pq_key_t *keyp, value_t *ret_v)
    int al_sl_rewind_iter(struct al_skiplist_iter_t *iterp)
