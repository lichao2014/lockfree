#ifndef _COMPAT_H_INCLUDED
#define _COMPAT_H_INCLUDED

#include <stdint.h>

#ifdef _WIN32
    #include <wtypes.h>
    #include <process.h>
 
    static int inline
    atomic_cmpset32(volatile uint32_t *ptr, uint32_t cmp, uint32_t new_value)
    {
        return cmp == _InterlockedCompareExchange(ptr, new_value, cmp);
    }

    static int inline
    atomic_cmpset(volatile void **ptr, void *cmp, void *new_value)
    {
        return cmp == _InterlockedCompareExchangePointer(ptr, new_value, cmp);
    }

    #define __builtin_expect(opt, value)    (opt)

    static void inline
    sched_yield(void)
    {
        Sleep(0);
    }

    typedef struct {
        unsigned stack_size;
        unsigned create_flag;
    } pthread_attr_t;

    typedef HANDLE pthread_t;

    static int 
    pthread_create(pthread_t *thread, pthread_attr_t *attr, unsigned (__stdcall *start_routine)(void  *), void *arg)
    {
        HANDLE h = (HANDLE)_beginthreadex(NULL, attr ? attr->stack_size : 0, start_routine, arg, attr ? attr->create_flag : 0, NULL);
        if (!h) {
            return -1;
        }

        *thread = h;
        return 0;
    }

    static int
    pthread_join(pthread_t thread, void **pret)
    {
        DWORD dw = WaitForSingleObject(thread, INFINITE);
        if (0 == dw) {
            return -1;
        }

        if (pret) {
            GetExitCodeThread(thread, &dw);
            *pret = (void *)dw;
        }

        CloseHandle(thread);
        return 0;
    }

    #define atomic_pause() _mm_pause()
    #define atomic_mb() _mm_mfence()
    #define atomic_wmb() _mm_sfence()
    #define atomic_rmb() _mm_lfence()
#else
    #include <pthread.h>
    #include <unistd.h>

    #define atomic_cmpset32(ptr, cmp, new_value) __sync_bool_compare_and_swap(ptr, cmp,  new_value)

    #define atomic_cmpset(ptr, cmp, new_value) __sync_bool_compare_and_swap(ptr, cmp,  new_value)

    #define atomic_pause()  asm volatile("pause":::"memory") 
    #define atomic_mb() asm volatile("mfence":::"memory")
    #define atomic_wmb() asm volatile("sfence":::"memory")
    #define atomic_rmb() asm volatile("lfence":::"memory")
#endif

#define likely(f) __builtin_expect(!!(f), 1)
#define unlikely(f) __builtin_expect(!!(f), 0)

#define atomic_smp_mb() atomic_mb()
#define atomic_smp_wmb() atomic_wmb()
#define atomic_smp_rmb() atomic_rmb()

#endif //_COMPAT_H_INCLUDED
