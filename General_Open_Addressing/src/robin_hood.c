/**
 * @file    open_addressing.c
 * @brief   A modular open addressing hash table implementation for
 *          testing and benchmarking.
 * @author  J.W Moolman
 * @date    2024-10-23
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "open_addressing.h"
#include "debug_hashtab.h"

#define PRINT_BUFFER_SIZE 1024

/* An entry in the hash table */
struct htentry {
    uint32_t hash_key;   /* Cached hash code for quicker comparison      */
    uint32_t psl;        /* Probe sequence length                        */
    void *key;           /* Pointer to key data                          */
    void *value;         /* Pointer to value data                        */
};

/* a hash table container */
struct hashtab {
    HTentry *table;      /* Underlying array of entries (slots)          */
    uint32_t size;       /* Current size (capacity) of the table         */
    uint32_t used;       /* Number of non-empty entries (active+deleted) */
    uint32_t active;     /* Number of active (non-deleted) entries       */

    float load_factor;       /* Max load factor before resizing          */
    float min_load_factor;   /* Min load factor to consider downsizing    */

    uint32_t (*hash_func)(void *key, size_t len);
	int (*cmp_func)(const void *a, const void *b);
    uint32_t (*p)(uint32_t k, uint32_t i, uint32_t m);

    void (*freekey)(void *k);
    void (*freeval)(void *v);
};

/* --- function prototypes -------------------------------------------------- */

static uint32_t default_hash_func(void *key, size_t len);
static int default_cmp_func(const void *a, const void *b);
static uint32_t default_probe_func(uint32_t k, uint32_t i, uint32_t m);

static int insert_entry(HashTab *ht, uint32_t hash_key, void *key, void *value);
static void free_entry(HashTab *ht, HTentry *entry);
static void rehash_entries(HashTab *ht, HTentry *old_table, uint32_t old_size);
static void resize(HashTab *ht, uint32_t new_size);

/* --- hash table interface ------------------------------------------------- */

HashTab *init_ht(
        float load_factor,
        float min_load_factor,
        float inactive_factor,
        uint32_t (*hash_func)(void *key, size_t len),
        int (*cmp_func)(const void *a, const void *b),
        uint32_t (*p)(uint32_t k, uint32_t i, uint32_t m),
        void (*freekey)(void *k),
        void (*freeval)(void *v)
) {
    HashTab *self;

    DBG_start("init_ht_");

    self = (HashTab *)malloc(sizeof(HashTab));
    if (!self) {
        fprintf(stderr, "Hashtable allocation failed");
        exit(EXIT_FAILURE);
    }

    /* Initialize load tracking variables */
    self->size = 2;
    self->used = 0;
    self->active = 0;
    
    /* Initialize load factors with defaults if zero */
    self->load_factor = (load_factor > 0) ? load_factor : DEFAULT_LOAD_FACTOR;
    self->min_load_factor = (min_load_factor > 0) ? min_load_factor : DEFAULT_MIN_LOAD_FACTOR;
    //self->inactive_factor = (inactive_factor > 0) ? inactive_factor : DEFAULT_INACTIVE_FACTOR;

    /* Initialize function ptrs withe defaults if NULL */
    self->hash_func = hash_func ? hash_func : default_hash_func;
    self->cmp_func = cmp_func ? cmp_func : default_cmp_func;
    self->p = p ? p : default_probe_func;
    self->freekey = freekey ? freekey : NULL;
    self->freeval = freeval ? freeval : NULL;

    self->table = (HTentry *)calloc(self->size, sizeof(HTentry));
	if (self->table == NULL) {
		fprintf(stderr, "Hashtable allocation failed");
		exit(EXIT_FAILURE);
	}

    DBG_end("_init_ht");

	return self;
}

int search_ht(
        HashTab *self,
        void *key,
        size_t key_len
) {
    uint32_t i, hash_key, index;
    HTentry *entry;

    DBG_info("search_ht_");

    if (!self ) { //|| !key) {
        DBG_info("_search_ht [HT_INVALID_ARG]");
        return HT_INVALID_ARG;
    }
    
    hash_key = self->hash_func(key, key_len);

    for (i = 0; i < self->size; i++) {
        /* calculate index to probe */
        index = self->p(hash_key, i, self->size);
        entry = &self->table[index];
        if (entry->key == NULL) {
            /* empty bucket key not in table */
            return HT_KEY_NOT_FOUND;
        }
        if (entry->hash_key == hash_key && self->cmp_func(entry->key, key) == 0) {
            /* key found return index */
            return index;
        }
        /* if the current entries psl is less the i(probe length) ,the entry
         * would have been swapped earlier if if was present */
        if (entry->psl < i) {
            return HT_KEY_NOT_FOUND;
        }
    }

    DBG_info("_search_ht [HT_INVALID_STATE]");
    return HT_INVALID_STATE;
    
}

void *fetch_ht(
        HashTab *self,
        uint32_t index
) {
    if (!self || index >= self->size) {
        return NULL;
    }
    return self->table[index].value;
}

int insert_ht(
        HashTab *self,
        void *key,
        size_t key_len,
        void *value
) {
    int flag;
    uint32_t i, index, hash_key;

    if (!self ) {
        return HT_INVALID_ARG;
    }
    if (search_ht(self, key, key_len) >= 0) {
        return HT_KEY_EXISTS;
    }
    if (self->active + 1 > self->size * self->load_factor) {
        resize(self, self->size * 2);// use bit shift
    }
    hash_key = self->hash_func(key, key_len);
    return insert_entry(
        self,
        hash_key,
        key,
        value
    );
}

int remove_ht(
        HashTab *self,
        void *key,
        size_t key_len
) {
    uint32_t i, index, next_index, hash_key;
    HTentry *entry;

    if (!self || key == NULL) {
        return HT_INVALID_ARG;
    }
    hash_key = self->hash_func(key, key_len);
    
    for (i = 0; i < self->size; i++) {
        index = self->p(hash_key, i, self->size);
        entry = &self->table[index];
        if (entry->key == NULL) {
            return HT_KEY_NOT_FOUND;
        }
        /* if key found */
        if (entry->hash_key == hash_key && self->cmp_func(self->table[index].key, key) == 0) {
            /* free resources if functions provided */
            if (self->freekey) {
                self->freekey(entry->key);
            }
            if (self->freeval) {
                self->freeval(entry->value);
            }
            /* remove key by setting to NULL */
            entry->key = NULL;
            entry->value = NULL;

            /* backward shift subsequent entries to fill gap */
            next_index = self->p(hash_key, ++i, self->size);
            /* continue until until key with PSL 0 is found or empty entrie */
            while (self->table[next_index].key != NULL && self->table[next_index].psl > 0) {
                self->table[index] = self->table[next_index];
                self->table[index].psl--; // adjust probe sequence length
                index = next_index;
                next_index = self->p(hash_key, ++i, self->size);
            }
            /* mark last shifted bucket as empty */
            self->table[index].key = NULL;
            self->active--;
            /* resize in needed */
            if (self->active < (float)self->size * self->min_load_factor) {
                resize(self, self->size / 2);
            }
            return HT_SUCCESS;
        }
        /* terminate if psl less that i(probe length) since if the entry was present
         * it would aready have been encountered */
        if (entry->psl < i) {
            return HT_KEY_NOT_FOUND;
        }
        index = self->p(hash_key, i, self->size);
    }
    return HT_KEY_NOT_FOUND;
}

int free_ht(
		HashTab *self
) {
    unsigned int i;

    /* TODO:
     * -check free succesfull and return HT_FAILURE
     */

	if (self == NULL) {
		return HT_INVALID_ARG;
	}
    
    for (i = 0; i < self->size; i++) {
        if (self->table[i].key != NULL) {
            free_entry(self, &self->table[i]);
        }
    }
	free(self->table);
	self->table = NULL;
	self->hash_func = NULL;
	self->cmp_func = NULL;
    self->p = NULL;
	free(self);

	return HT_SUCCESS;
}

void print_ht(
        HashTab *self,
        void (*keyval2str)(int flag, void *k, void *v, char *b)
) {
    unsigned int i;
    HTentry p;
    char buffer[PRINT_BUFFER_SIZE];
    /** TODO:
     * - rework to take an function pointer to a function that takes pointer to
     *   HTentry 
     */   
    
    if (self && keyval2str) {
        printf(
                "--- HashTab - size[%d] - entries[%u] - loadfct[%.2f] --- \n",
                self->size,
                self->active,
                self->load_factor
        );

        for (i = 0; i < self->size; i++) {
            p = self->table[i];
            /* Check how this works with different macros */
            //keyval2str(p.flag, p.key, p.value, buffer);
            printf("Index %u: %s\n", i, buffer);
        }
    }

}

size_t size_ht(
        HashTab *self
) {
    return self->size;
}

/* --- utility functions ---------------------------------------------------- */

static int insert_entry(
        HashTab *ht,
        uint32_t hash_key,
        void *key,
        void *value
) {
    uint32_t i, psl, index;
    HTentry *entry, temp;

    HTentry  new_entry = {
        .hash_key = hash_key,
        .psl = 0,
        .key = key,
        .value = value
    };
    i = 0;
    while (i < ht->size) {
        index = ht->p(hash_key, i, ht->size);
        entry = &ht->table[index];
        /* empty buckect found */
        if (entry->key == NULL) {
            *entry = new_entry;
            ht->active++;
            return HT_SUCCESS;
        }
        /* compare probe length */
        if (new_entry.psl > entry->psl) {
            /* swap "poorer" (further element steals the spot. */
            temp = *entry;
            *entry = new_entry;
            new_entry = temp;
        }
        new_entry.psl++;
        i++;
    }

    /* should never occur */
    return HT_FAILURE;
}

static void free_entry(
        HashTab *ht,
        HTentry *entry
) {
    if (ht->freekey) {
        ht->freekey(entry->key);
        entry->key = NULL;
    }
    if (ht->freeval) {
        ht->freeval(entry->value);
        entry->key = NULL;
    }
}

static void rehash_entries(
        HashTab *ht,
        HTentry *old_table,
        uint32_t old_size
) {
    uint32_t i;
    for (i = 0; i < old_size; i++) {
        if (old_table[i].key != NULL) {
            insert_entry(
                ht,
                old_table[i].hash_key,
                old_table[i].key,
                old_table[i].value
            );
        }
    }

}

static void resize(
        HashTab *ht,
        uint32_t new_size
) {
    HTentry *old_table, *new_table;
    uint32_t i, old_size;
    int insert_status;

    old_size = ht->size;
    old_table = ht->table;

    new_table = (HTentry *)calloc(new_size, sizeof(HTentry));

    ht->table = new_table;
    ht->size = new_size;
    ht->active = 0;
    ht->used = 0;

    rehash_entries(ht, old_table, old_size);
    free(old_table);// no good dangling pointers

}
/* --- default functions ---------------------------------------------------- */

/* Default hash function preforms a modified FNV-1a hash on the key bytes */

static uint32_t default_hash_func(void *key, size_t len) {
    const unsigned char *bytes_ptr = (const unsigned char *)key;
    unsigned int hash = 2166136261u; // FNV offset basis
    unsigned int fnv_prime = 16777619u; // FNV prime

    for (size_t i = 0; i < len; i++) {
        hash ^= bytes_ptr[i];       // XOR with the byte
        hash *= fnv_prime;          // Multiply by FNV prime
    }

    return hash;
}

/* Default key comparison function */
static int default_cmp_func(const void *a, const void *b) {
    int int_a = *(const int *)a;
    int int_b = *(const int *)b;
    return (int_a > int_b) - (int_a < int_b);
}

static uint32_t default_probe_func(uint32_t k, uint32_t i, uint32_t m) {
    return (k + i) & (m - 1);
}

