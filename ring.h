#ifndef _RING_H_INCLUDED
#define _RING_H_INCLUDED

#include <stdint.h>
#include "compat.h"

#define DEFINE_ERR_MAP(XX)  \
    XX(OK, 0)   \
    XX(NOBUFS, -1)  \
    XX(DQUOT, -2)   \
    XX(NOENT, -3)   \
    XX(INVAL, -4)

enum ring_error_t {
#define XX(e, n)    RING_ERROR_##e = n,
    DEFINE_ERR_MAP(XX)
#undef XX
};

enum ring_queue_behavior_t {
    RING_QUEUE_FIXED = 0,
    RING_QUEUE_VARIABLE
};

struct ring_t {
    int flags;

    struct prod_t {
        uint32_t watermark;
        uint32_t sp_enqueue;
        uint32_t size;
        uint32_t mask;
        volatile uint32_t head;
        volatile uint32_t tail;
    } prod;

    struct cons_t {
        uint32_t sc_dequeue;
        uint32_t size;
        uint32_t mask;
        volatile uint32_t head;
        volatile uint32_t tail;
    } cons;

    void *ring[];
};

#define RING_F_SP_ENQ 0x0001
#define RING_F_SC_DEQ 0x0002
#define RING_QUOT_EXCEED (1 << 31)
#define RING_SZ_MASK (unsigned)(0x0fffffff)
#define RING_PAUSE_REP_COUNT 3

int ring_init(struct ring_t* r, unsigned count, unsigned flags);

struct ring_t* ring_create(unsigned count, unsigned flags);

void ring_free(struct ring_t* r);

int ring_set_water_mark(struct ring_t* r, unsigned count);

#define ENQUEUE_PTRS() do { \
    const uint32_t size = r->prod.size; \
    uint32_t idx = prod_head & mask; \
    if (likely(idx + n < size)) { \
        for (i = 0; i < (n & ((~(unsigned)0x3))); i+=4, idx+=4) { \
            r->ring[idx] = obj_table[i]; \
            r->ring[idx+1] = obj_table[i+1]; \
            r->ring[idx+2] = obj_table[i+2]; \
            r->ring[idx+3] = obj_table[i+3]; \
        } \
        switch (n & 0x3) { \
            case 3: r->ring[idx++] = obj_table[i++]; \
            case 2: r->ring[idx++] = obj_table[i++]; \
            case 1: r->ring[idx++] = obj_table[i++]; \
        } \
    } else { \
        for (i = 0; idx < size; i++, idx++)\
            r->ring[idx] = obj_table[i]; \
        for (idx = 0; i < n; i++, idx++) \
            r->ring[idx] = obj_table[i]; \
    } \
} while(0)

#define DEQUEUE_PTRS() do { \
    uint32_t idx = cons_head & mask; \
    const uint32_t size = r->cons.size; \
    if (likely(idx + n < size)) { \
        for (i = 0; i < (n & (~(unsigned)0x3)); i+=4, idx+=4) {\
            obj_table[i] = r->ring[idx]; \
            obj_table[i+1] = r->ring[idx+1]; \
            obj_table[i+2] = r->ring[idx+2]; \
            obj_table[i+3] = r->ring[idx+3]; \
        } \
        switch (n & 0x3) { \
            case 3: obj_table[i++] = r->ring[idx++]; \
            case 2: obj_table[i++] = r->ring[idx++]; \
            case 1: obj_table[i++] = r->ring[idx++]; \
        } \
    } else { \
        for (i = 0; idx < size; i++, idx++) \
            obj_table[i] = r->ring[idx]; \
        for (idx = 0; i < n; i++, idx++) \
            obj_table[i] = r->ring[idx]; \
    } \
} while (0)


static inline int
__ring_mp_do_enqueue(struct ring_t *r, void * const *obj_table, unsigned n, enum ring_queue_behavior_t behavior)
{
    uint32_t prod_head, prod_next;
    uint32_t cons_tail, free_entries;
    const unsigned max = n;
    int success;
    unsigned i, rep = 0;
    uint32_t mask = r->prod.mask;
    int ret;

    if (n == 0)
        return 0;

    do {
        n = max;

        prod_head = r->prod.head;
        cons_tail = r->cons.tail;

        free_entries = (mask + cons_tail - prod_head);

        if (unlikely(n > free_entries)) {
            if (behavior == RING_QUEUE_FIXED) {
                return RING_ERROR_NOBUFS;
            }
            else {
                if (unlikely(free_entries == 0)) {
                    return RING_ERROR_OK;
                }

                n = free_entries;
            }
        }

        prod_next = prod_head + n;
        success = atomic_cmpset32(&r->prod.head, prod_head,
            prod_next);
    } while (unlikely(success == 0));

    ENQUEUE_PTRS();
    atomic_smp_wmb();

    if (unlikely(((mask + 1) - free_entries + n) > r->prod.watermark)) {
        ret = (behavior == RING_QUEUE_FIXED) ? RING_ERROR_DQUOT :
            (int)(n | RING_QUOT_EXCEED);
    }
    else {
        ret = (behavior == RING_QUEUE_FIXED) ? 0 : n;
    }

    while (unlikely(r->prod.tail != prod_head)) {
        atomic_pause();

        if (RING_PAUSE_REP_COUNT &&
            ++rep == RING_PAUSE_REP_COUNT) {
            rep = 0;
            sched_yield();
        }
    }
    r->prod.tail = prod_next;
    return ret;
}

static inline int
__ring_sp_do_enqueue(struct ring_t *r, void * const *obj_table, unsigned n, enum ring_queue_behavior_t behavior)
{
    uint32_t prod_head, cons_tail;
    uint32_t prod_next, free_entries;
    unsigned i;
    uint32_t mask = r->prod.mask;
    int ret;

    prod_head = r->prod.head;
    cons_tail = r->cons.tail;

    free_entries = mask + cons_tail - prod_head;

    if (unlikely(n > free_entries)) {
        if (behavior == RING_QUEUE_FIXED) {
            return RING_ERROR_NOBUFS;
        }
        else {
            /* No free entry available */
            if (unlikely(free_entries == 0)) {
                return RING_ERROR_OK;
            }

            n = free_entries;
        }
    }

    prod_next = prod_head + n;
    r->prod.head = prod_next;

    ENQUEUE_PTRS();
    atomic_smp_wmb();

    if (unlikely(((mask + 1) - free_entries + n) > r->prod.watermark)) {
        ret = (behavior == RING_QUEUE_FIXED) ? RING_ERROR_DQUOT :
            (int)(n | RING_QUOT_EXCEED);
    }
    else {
        ret = (behavior == RING_QUEUE_FIXED) ? 0 : n;
    }

    r->prod.tail = prod_next;
    return ret;
}

static inline int
__ring_mc_do_dequeue(struct ring_t *r, void **obj_table, unsigned n, enum ring_queue_behavior_t behavior)
{
    uint32_t cons_head, prod_tail;
    uint32_t cons_next, entries;
    const unsigned max = n;
    int success;
    unsigned i, rep = 0;
    uint32_t mask = r->prod.mask;

    if (n == 0)
        return 0;

    do {
        n = max;

        cons_head = r->cons.head;
        prod_tail = r->prod.tail;

        entries = (prod_tail - cons_head);

        if (n > entries) {
            if (behavior == RING_QUEUE_FIXED) {
                return RING_ERROR_NOENT;
            }
            else {
                if (unlikely(entries == 0)) {
                    return 0;
                }

                n = entries;
            }
        }

        cons_next = cons_head + n;
        success = atomic_cmpset32(&r->cons.head, cons_head,
            cons_next);
    } while (unlikely(success == 0));

    DEQUEUE_PTRS();
    atomic_smp_rmb();

    while (unlikely(r->cons.tail != cons_head)) {
        atomic_pause();

        if (RING_PAUSE_REP_COUNT &&
            ++rep == RING_PAUSE_REP_COUNT) {
            rep = 0;
            sched_yield();
        }
    }

    r->cons.tail = cons_next;

    return behavior == RING_QUEUE_FIXED ? 0 : n;
}

static inline int
__ring_sc_do_dequeue(struct ring_t *r, void **obj_table, unsigned n, enum ring_queue_behavior_t behavior)
{
    uint32_t cons_head, prod_tail;
    uint32_t cons_next, entries;
    unsigned i;
    uint32_t mask = r->prod.mask;

    cons_head = r->cons.head;
    prod_tail = r->prod.tail;

    entries = prod_tail - cons_head;

    if (n > entries) {
        if (behavior == RING_QUEUE_FIXED) {
            return RING_ERROR_NOENT;
        }
        else {
            if (unlikely(entries == 0)) {
                return 0;
            }

            n = entries;
        }
    }

    cons_next = cons_head + n;
    r->cons.head = cons_next;

    DEQUEUE_PTRS();
    atomic_smp_rmb();

    r->cons.tail = cons_next;

    return behavior == RING_QUEUE_FIXED ? 0 : n;
}

static inline int
ring_mp_enqueue_bulk(struct ring_t *r, void * const *obj_table, unsigned n)
{
    return __ring_mp_do_enqueue(r, obj_table, n, RING_QUEUE_FIXED);
}

static inline int
ring_sp_enqueue_bulk(struct ring_t *r, void * const *obj_table, unsigned n)
{
    return __ring_sp_do_enqueue(r, obj_table, n, RING_QUEUE_FIXED);
}

static inline int
ring_enqueue_bulk(struct ring_t *r, void * const *obj_table, unsigned n)
{
    if (r->prod.sp_enqueue)
        return ring_sp_enqueue_bulk(r, obj_table, n);
    else
        return ring_mp_enqueue_bulk(r, obj_table, n);
}

static inline int
ring_mp_enqueue(struct ring_t *r, void *obj)
{
    return ring_mp_enqueue_bulk(r, &obj, 1);
}

static inline int
ring_sp_enqueue(struct ring_t *r, void *obj)
{
    return ring_sp_enqueue_bulk(r, &obj, 1);
}

static inline int
ring_enqueue(struct ring_t *r, void *obj)
{
    if (r->prod.sp_enqueue)
        return ring_sp_enqueue(r, obj);
    else
        return ring_mp_enqueue(r, obj);
}

static inline int
ring_mc_dequeue_bulk(struct ring_t *r, void **obj_table, unsigned n)
{
    return __ring_mc_do_dequeue(r, obj_table, n, RING_QUEUE_FIXED);
}

static inline int
ring_sc_dequeue_bulk(struct ring_t *r, void **obj_table, unsigned n)
{
    return __ring_sc_do_dequeue(r, obj_table, n, RING_QUEUE_FIXED);
}

static inline int
ring_dequeue_bulk(struct ring_t *r, void **obj_table, unsigned n)
{
    if (r->cons.sc_dequeue)
        return ring_sc_dequeue_bulk(r, obj_table, n);
    else
        return ring_mc_dequeue_bulk(r, obj_table, n);
}

static inline int
ring_mc_dequeue(struct ring_t *r, void **obj_p)
{
    return ring_mc_dequeue_bulk(r, obj_p, 1);
}

static inline int
ring_sc_dequeue(struct ring_t *r, void **obj_p)
{
    return ring_sc_dequeue_bulk(r, obj_p, 1);
}

static inline int
ring_dequeue(struct ring_t *r, void **obj_p)
{
    if (r->cons.sc_dequeue)
        return ring_sc_dequeue(r, obj_p);
    else
        return ring_mc_dequeue(r, obj_p);
}

static inline int
ring_full(const struct ring_t *r)
{
    uint32_t prod_tail = r->prod.tail;
    uint32_t cons_tail = r->cons.tail;
    return ((cons_tail - prod_tail - 1) & r->prod.mask) == 0;
}

static inline int
ring_empty(const struct ring_t *r)
{
    uint32_t prod_tail = r->prod.tail;
    uint32_t cons_tail = r->cons.tail;
    return !!(cons_tail == prod_tail);
}

static inline unsigned
ring_count(const struct ring_t *r)
{
    uint32_t prod_tail = r->prod.tail;
    uint32_t cons_tail = r->cons.tail;
    return (prod_tail - cons_tail) & r->prod.mask;
}

static inline unsigned
ring_free_count(const struct ring_t *r)
{
    uint32_t prod_tail = r->prod.tail;
    uint32_t cons_tail = r->cons.tail;
    return (cons_tail - prod_tail - 1) & r->prod.mask;
}

static inline unsigned
ring_mp_enqueue_burst(struct ring_t *r, void * const *obj_table,
    unsigned n)
{
    return __ring_mp_do_enqueue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

static inline unsigned
ring_sp_enqueue_burst(struct ring_t *r, void * const *obj_table,
    unsigned n)
{
    return __ring_sp_do_enqueue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

static inline unsigned
ring_enqueue_burst(struct ring_t *r, void * const *obj_table,
    unsigned n)
{
    if (r->prod.sp_enqueue)
        return ring_sp_enqueue_burst(r, obj_table, n);
    else
        return ring_mp_enqueue_burst(r, obj_table, n);
}

static inline unsigned
ring_mc_dequeue_burst(struct ring_t *r, void **obj_table, unsigned n)
{
    return __ring_mc_do_dequeue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

static inline unsigned
ring_sc_dequeue_burst(struct ring_t *r, void **obj_table, unsigned n)
{
    return __ring_sc_do_dequeue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

static inline unsigned
ring_dequeue_burst(struct ring_t *r, void **obj_table, unsigned n)
{
    if (r->cons.sc_dequeue)
        return ring_sc_dequeue_burst(r, obj_table, n);
    else
        return ring_mc_dequeue_burst(r, obj_table, n);
}

#endif //_RING_H_INCLUDED
