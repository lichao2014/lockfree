#include <stdio.h>
#include <string.h>
#include "ring.h"
#include "stack.h"

#define TEST_READ_THREAD_NUM 8
#define TEST_WRITE_THREAD_NUM 1

struct test_stack_node_t {
    struct stack_node_t node;
    int value;
};

#ifdef _WIN32
static unsigned __stdcall
#else
static void *
#endif
ring_read_thread(void *arg)
{
    struct ring_t *r = arg;
    void *p;
    int ret;

    while (1) {
        ret = ring_dequeue(r, &p);
        if (RING_ERROR_OK == ret) {
            printf("ring_recv_thread ret=%d,p=%p\n", ret, p);
        }
        else {
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
        }
    }

    return 0;
}

#ifdef _WIN32
static unsigned __stdcall
#else
static void *
#endif
ring_write_thread(void *arg)
{
    struct ring_t *r = arg;
    int i;

    for (i = 0; i < 1000; ++i) {
        ring_enqueue(r, (void *)i);

#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }

    return 0;
}

int main()
{
    int i;
    int ret;
    pthread_t tg[TEST_READ_THREAD_NUM + TEST_WRITE_THREAD_NUM];
    struct ring_t *r;
    stack_t stack;
    struct test_stack_node_t data;
    struct test_stack_node_t *result;


    memset(tg, 0, sizeof tg);
    r = NULL;
    stack = NULL;

    data.value = 1;
    stack_push(&stack, &data.node);
    stack_pop(&stack, &result);

    do {
        r = ring_create(1024, 0);
        if (!r) {
            break;
        }

        for (i = 0; i < TEST_READ_THREAD_NUM; ++i) {
            ret = pthread_create(tg + i, NULL, &ring_read_thread, r);
            if (0 != ret) {
                break;
            }
        }

        if (0 != ret) {
            break;
        }

        for (; i < TEST_READ_THREAD_NUM + TEST_WRITE_THREAD_NUM; ++i) {
            ret = pthread_create(tg + i, NULL, &ring_write_thread, r);
            if (0 != ret) {
                break;
            }
        }

        if (0 != ret) {
            break;
        }

    } while (0);

    for (i = 0; i < TEST_READ_THREAD_NUM + TEST_WRITE_THREAD_NUM; ++i) {
        if (tg[i]) {
            pthread_join(tg[i], NULL);
        }
    }

    ring_free(r);
    return 0;
}