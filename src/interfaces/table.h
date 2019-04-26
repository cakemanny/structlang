#ifndef __TABLE_H__
#define __TABLE_H__

#define T Table_T

typedef struct Table *T;

/*
 * Allocates a new table.
 * hint is optional, but providing an accurate value can improve performance
 * If hash is NULL, the keys are assumed to be atoms
 */
T Table_new(
        int hint,
        int cmp(const void* x, const void* y),
        unsigned hash(const void* key));

void Table_free(T* table);

/*
 * The number of keys in the table.
 */
int Table_length(T table);

/*
 * Adds entry to table. Returns previous value if there was one,
 * otherwise NULL.
 */
void* Table_put(T table, const void* key, void* value);

/*
 * Find and return value for key, or NULL
 */
void* Table_get(T table, const void* key);

/*
 * Remove entry for key and return value, or NULL if not found
 */
void* Table_remove(T table, const void* key);

/*
 * Calls apply for each entry. Note a pointer to value is provided, so
 * it can be changed.
 */
void Table_map(T table,
        void apply(const void* key, void** value, void* cl),
        void* cl);

/*
 * Returns an array of { key0, value0, key1, value1, ..., keyN , valueN, end }
 * Callers must deallocate the array returned.
 */
void** Table_toArray(T table, void* end);

#undef T
#endif /* __TABLE_H__ */
