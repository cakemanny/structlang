#include <limits.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "table.h"

#define T Table_T

struct Table {
    int size;
    int (*cmp)(const void* x, const void* y);
    unsigned (*hash)(const void* key);
    int length;
    unsigned timestamp;

    struct binding {
        const void* key;
        void* value;
    } *buckets;
};

static int cmpatom(const void *x, const void* y)
{
    return x != y;
}
static unsigned hashatom(const void* key)
{
    return (unsigned long)key>>2;
}

T Table_new(
        int hint,
        int cmp(const void* x, const void* y),
        unsigned hash(const void* key))
{
    T table;
    int size;

    assert(hint >= 0);
    for (size = 128; size < hint; size <<= 1)
        ;

    table = calloc(1, sizeof *table);
    table->buckets = calloc(size, sizeof table->buckets[0]);
    table->size = size;
    table->cmp = cmp ? cmp : cmpatom;
    table->hash = hash ? hash : hashatom;
    table->length = 0;
    table->timestamp = 0;
    return table;
}

void* Table_get(T table, const void* key)
{
    int i;
    struct binding *p, *end;

    assert(table);
    assert(key);

    end = table->buckets + table->size;
    i = table->hash(key) & (table->size - 1);
    for (p = table->buckets + i;
            // probe at most 12 more pockets for space
            p < end && p->key && p < table->buckets + i + 12;
            p++) {
        if (table->cmp(key, p->key) == 0) {
            return p->value;
        }
    }
    return NULL;
}

static void rehash(T table)
{
    struct binding *p, *q, *end = table->buckets + table->size;
    int new_size = table->size * 2;
    struct binding* new_buckets = calloc(new_size, sizeof *new_buckets);

    for (p = table->buckets; p < end; p++) {
        int i = table->hash(p->key) & (new_size - 1);

        for (q = new_buckets + i;
                q < new_buckets + new_size && q->key;
                q++);
        // Because we doubled the table size.
        // Should have left a nice gap next to each slot
        assert(q < new_buckets + new_size);
        assert(q < new_buckets + i + 12);

        q->key = p->key;
        q->value = p->value;
    }
    free(table->buckets);
    table->buckets = new_buckets;
    table->size = new_size;
}

void* Table_put(T table, const void* key, void* value)
{
    int i;
    struct binding *p, *q = NULL, *end;
    void* prev;

    assert(table);
    assert(key);
    assert(value);

    end = table->buckets + table->size;
    i = table->hash(key) & (table->size - 1);
    for (p = table->buckets + i;
            // probe at most 12 more pockets for space
            p < end && p->key && p < table->buckets + i + 12;
            p++) {
        if (table->cmp(key, p->key) == 0) {
             q = p; // found
        }
    }

    if (q == NULL) {
        if (p < end && p->key == NULL) {
            q = p;
            // spare slot next to us
            q->key = key;
            table->length++;
        } else {
            rehash(table);
            return Table_put(table, key, value);
        }
    }
    prev = q->value;
    q->value = value;
    table->timestamp++;
    return prev;
}

int Table_length(T table)
{
    assert(table);
    return table->length;
}

void Table_map(
        T table,
        void apply(const void* key, void** value, void* cl),
        void* cl)
{
    int i;
    unsigned stamp;
    struct binding* p;

    assert(table);
    assert(apply);
    stamp = table->timestamp;

    for (i = 0; i < table->size; i++) {
        p = table->buckets + i;
        if (p->key) {
            apply(p->key, &p->value, cl);
            assert(p->value);
            assert(table->timestamp == stamp);
        }
    }
}

void* Table_remove(T table, const void* key)
{
    int i;
    struct binding *p, *end;

    assert(table);
    assert(key);
    table->timestamp++;

    end = table->buckets + table->size;
    i = table->hash(key) & (table->size - 1);
    for (p = table->buckets + i;
            // probe at most 12 more pockets for space
            p < end && p->key && p < table->buckets + i + 12;
            p++) {
        if (table->cmp(key, p->key) == 0) {
            void* value = p->value;
            p->key = NULL;
            p->value = NULL;
            return value;
        }
    }
    return NULL;
}

void** Table_toArray(T table, void *end)
{
    int i, j = 0;
    void** array;
    struct binding* p;

    assert(table);
    array = malloc((2 * table->length + 1) * sizeof *array);
    for (i = 0; i < table->size; i++) {
        p = table->buckets + i;
        if (p->key) {
            array[j++] = (void*)p->key;
            array[j++] = p->value;
        }
    }
    array[j] = end;
    return array;
}

void Table_free(T* table)
{
    assert(table);
    assert(*table);

    free((*table)->buckets); (*table)->buckets = NULL;
    free(*table); *table = NULL;
}

