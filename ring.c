#include "ring.h"
#include <string.h>
#include <stdlib.h>

int 
ring_init(struct ring_t* r, unsigned count, unsigned flags)
{
    /* init the ring structure */
    memset(r, 0, sizeof(*r));

    r->flags = flags;
    r->prod.watermark = count;
    r->prod.sp_enqueue = !!(flags & RING_F_SP_ENQ);
    r->cons.sc_dequeue = !!(flags & RING_F_SC_DEQ);
    r->prod.size = r->cons.size = count;
    r->prod.mask = r->cons.mask = count - 1;
    r->prod.head = r->cons.head = 0;
    r->prod.tail = r->cons.tail = 0;

    return RING_ERROR_OK;
}

struct ring_t* 
ring_create(unsigned count, unsigned flags)
{
    struct ring_t *r;

    r = malloc(sizeof(struct ring_t) + sizeof(void *) * count);
    ring_init(r, count, flags);

    return r;
}

void 
ring_free(struct ring_t* r)
{
    free(r);
}

int 
ring_set_water_mark(struct ring_t* r, unsigned count)
{
    if (count >= r->prod.size)
        return RING_ERROR_INVAL;

    /* if count is 0, disable the watermarking */
    if (count == 0)
        count = r->prod.size;

    r->prod.watermark = count;
    return 0;
}
