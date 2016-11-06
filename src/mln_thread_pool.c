
/*
 * Copyright (C) Niklaus F.Schen.
 */
#include <stdlib.h>
#include "mln_thread_pool.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "mln_log.h"

/*
 * There is a problem in linux.
 * I don't know whether it's a bug or not.
 * If I fork a child process in the callback function
 * 'process_handler' which is running on child thread,
 * there is always 288-byte(64-bit)/144-byte(32-bit)
 * block leak in child process.
 * I checked I had already freed all blocks which were
 * allocated by malloc(). But it still existed.
 * I remember that there is a 288-byte(64-bit)/144-byte(32-bit)
 * block allocated during calling pthread_create().
 * But in child process, I can not access that
 * 288-byte(64-bit)/144-byte(32-bit) memory in user mode,
 * which means I can't free this memory block.
 *
 * And there is another thing which is that FreeBSD 10.0 i386
 * can not support fork() in thread perfectly.
 * The child process will be blocked mostly.
 * GDB can not attach that blocked process.
 * Command 'truss' can not see which syscall it encountered.
 * So if someone can explain it, I wiil be so grateful while
 * you contact me immediately.
 * Now, I just wanna say, fork() in thread is NOT recommended.
 */

__thread mln_thread_pool_member_t *mThreadPoolSelf = NULL;

static void *child_thread_launcher(void *arg);
static void mln_thread_pool_free(mln_thread_pool_t *tp);

MLN_CHAIN_FUNC_DECLARE(mln_child, \
                       mln_thread_pool_member_t, \
                       static inline void, \
                       __NONNULL3(1,2,3));

/*
 * thread_pool_member
 */
static inline mln_thread_pool_member_t *
mln_thread_pool_member_new(mln_thread_pool_t *tpool, mln_u32_t child)
{
    mln_thread_pool_member_t *tpm;
    if ((tpm = (mln_thread_pool_member_t *)malloc(sizeof(mln_thread_pool_member_t))) == NULL) {
        return NULL;
    }
    tpm->data = NULL;
    tpm->pool = tpool;
    tpm->idle = 1;
    tpm->locked = 0;
    tpm->forked = 0;
    tpm->child = child;
    tpm->prev = tpm->next = NULL;
    return tpm;
}

static inline void mln_thread_pool_member_free(mln_thread_pool_member_t *member)
{
    if (member == NULL) return;
    if (member->data != NULL && member->pool->free_handler != NULL)
        member->pool->free_handler(member->data);
    free(member);
}

static mln_thread_pool_member_t *
mln_thread_pool_member_join(mln_thread_pool_t *tp, mln_u32_t child)
{
    /*
     * @ mutex must be locked by caller.
     * and mThreadPoolSelf will be set later.
     * This function only can be called by main thread.
     */
    mln_thread_pool_member_t *tpm;
    if ((tpm = mln_thread_pool_member_new(tp, child)) == NULL) {
        return NULL;
    }
    tp->counter++;
    tp->idle++;
    mln_child_chain_add(&(tp->childHead), &(tp->childTail), tpm);
    return tpm;
}

static void mln_thread_pool_member_exit(void *arg)
{
    if (arg == NULL) {
        mln_log(error, "Fatal error, thread messed up.\n");
        abort();
    }
    mln_thread_pool_member_t *tpm = (mln_thread_pool_member_t *)arg;
    mln_u32_t forked = tpm->forked, child = tpm->child;
    mln_thread_pool_t *tpool = tpm->pool;
    if (tpm->locked == 0) {
        tpm->locked = 1;
        pthread_mutex_lock(&(tpool->mutex));
    }
    mln_child_chain_del(&(tpool->childHead), &(tpool->childTail), tpm);
    tpool->counter--;
    if (tpm->idle) tpool->idle--;
    pthread_mutex_unlock(&(tpool->mutex));
    mln_thread_pool_member_free(tpm);
    if (forked && child) {
        mln_thread_pool_free(tpool);
    }
}

/*
 * thread_pool
 */
static void mln_thread_pool_prepare(void)
{
    if (mThreadPoolSelf == NULL) return;
    if (!mThreadPoolSelf->locked)
        pthread_mutex_lock(&(mThreadPoolSelf->pool->mutex));
}

static void mln_thread_pool_parent(void)
{
    if (mThreadPoolSelf == NULL) return;
    if (!mThreadPoolSelf->locked)
        pthread_mutex_unlock(&(mThreadPoolSelf->pool->mutex));
}

static void mln_thread_pool_child(void)
{
    if (mThreadPoolSelf == NULL) return;
    mln_thread_pool_t *tpool = mThreadPoolSelf->pool;
    if (!mThreadPoolSelf->locked)
        pthread_mutex_unlock(&(tpool->mutex));
    mThreadPoolSelf->forked = 1;
    mln_thread_pool_member_t *tpm = tpool->childHead, *fr;
    while (tpm != NULL) {
        fr = tpm;
        tpm = tpm->next;
        if (fr == mThreadPoolSelf) continue;
        mln_thread_pool_member_exit(fr);
    }
}

static mln_thread_pool_t *
mln_thread_pool_new(struct mln_thread_pool_attr *tpattr, int *err)
{
    int rc;
    mln_thread_pool_t *tp;
    if ((tp = (mln_thread_pool_t *)malloc(sizeof(mln_thread_pool_t))) == NULL) {
        *err = ENOMEM;
        return NULL;
    }
    if ((rc = pthread_mutex_init(&(tp->mutex), NULL)) != 0) {
        free(tp);
        *err = rc;
        return NULL;
    }
    if ((rc = pthread_cond_init(&(tp->cond), NULL)) != 0) {
        pthread_mutex_destroy(&(tp->mutex));
        free(tp);
        *err = rc;
        return NULL;
    }
    if ((rc = pthread_attr_init(&(tp->attr))) != 0) {
        pthread_cond_destroy(&(tp->cond));
        pthread_mutex_destroy(&(tp->mutex));
        free(tp);
        *err = rc;
        return NULL;
    }
    if ((rc = pthread_attr_setdetachstate(&(tp->attr), PTHREAD_CREATE_DETACHED)) != 0) {
        pthread_attr_destroy(&(tp->attr));
        pthread_cond_destroy(&(tp->cond));
        pthread_mutex_destroy(&(tp->mutex));
        free(tp);
        *err = rc;
        return NULL;
    }
    tp->resChainHead = tp->resChainTail = NULL;
    tp->childHead = tp->childTail = NULL;
    tp->idle = tp->counter = 0;
    tp->quit = 0;
    tp->condTimeout = tpattr->condTimeout;
    tp->process_handler = tpattr->child_process_handler;
    tp->free_handler = tpattr->free_handler;
    tp->max = tpattr->max;
#ifdef MLN_USE_UNIX98
    if (tpattr->concurrency) pthread_setconcurrency(tpattr->concurrency);
#endif
    if ((rc = pthread_atfork(mln_thread_pool_prepare, \
                             mln_thread_pool_parent, \
                             mln_thread_pool_child)) != 0)
    {
        pthread_attr_destroy(&(tp->attr));
        pthread_cond_destroy(&(tp->cond));
        pthread_mutex_destroy(&(tp->mutex));
        free(tp);
        *err = rc;
        return NULL;
    }
    if ((mThreadPoolSelf = mln_thread_pool_member_join(tp, 0)) == NULL) {
        pthread_attr_destroy(&(tp->attr));
        pthread_cond_destroy(&(tp->cond));
        pthread_mutex_destroy(&(tp->mutex));
        free(tp);
        *err = ENOMEM;
        return NULL;
    }
    return tp;
}

static void mln_thread_pool_free(mln_thread_pool_t *tp)
{
    if (tp == NULL) return;
    mThreadPoolSelf = NULL;
    mln_thread_pool_resource_t *tpr;
    while ((tpr = tp->resChainHead) != NULL) {
        tp->resChainHead = tp->resChainHead->next;
        if (tp->free_handler != NULL) tp->free_handler(tpr->data);
        free(tpr);
    }
    if (tp->childHead != NULL || tp->counter || tp->idle) {
        mln_log(error, "fatal error, thread pool messed up.\n");
        abort();
    }
    pthread_mutex_destroy(&(tp->mutex));
    pthread_cond_destroy(&(tp->cond));
    pthread_attr_destroy(&(tp->attr));
    free(tp);
}


/*
 * resource
 */
int mln_thread_pool_addResource(void *data)
{
    /*
     * Only main thread can call this function
     */
    if (mThreadPoolSelf == NULL) {
        mln_log(error, "Fatal error, thread messed up.\n");
        abort();
    }
    mln_thread_pool_resource_t *tpr;
    mln_thread_pool_t *tpool = mThreadPoolSelf->pool;

    if ((tpr = (mln_thread_pool_resource_t *)malloc(sizeof(mln_thread_pool_resource_t))) == NULL) {
        return ENOMEM;
    }
    tpr->data = data;
    tpr->next = NULL;

    mThreadPoolSelf->locked = 1;
    pthread_mutex_lock(&(tpool->mutex));

    if (tpool->resChainHead == NULL) {
        tpool->resChainHead = tpool->resChainTail = tpr;
    } else {
        tpool->resChainTail->next = tpr;
        tpool->resChainTail = tpr;
    }

    if (tpool->idle <= 1 && tpool->counter < tpool->max+1) {
        int rc;
        pthread_t threadid;
        mln_thread_pool_member_t *tpm;
        if ((tpm = mln_thread_pool_member_join(tpool, 1)) == NULL) {
            pthread_mutex_unlock(&(tpool->mutex));
            mThreadPoolSelf->locked = 0;
            return ENOMEM;
        }
        if ((rc = pthread_create(&threadid, &(tpool->attr), child_thread_launcher, tpm)) != 0) {
            pthread_mutex_unlock(&(tpool->mutex));
            mThreadPoolSelf->locked = 0;
            return rc;
        }
    }
    pthread_cond_signal(&(tpool->cond));

    pthread_mutex_unlock(&(tpool->mutex));
    mThreadPoolSelf->locked = 0;
    return 0;
}

static void *mln_thread_pool_removeResource(void)
{
    /*
     * Only child threads can call this function
     * @ the lock will be locked by caller.
     */
    if (mThreadPoolSelf == NULL) {
        mln_log(error, "Fatal error, thread messed up.\n");
        abort();
    }
    mln_thread_pool_resource_t *tpr;
    mln_thread_pool_t *tpool = mThreadPoolSelf->pool;
again:
    if ((tpr = tpool->resChainHead) == NULL) return NULL;
    tpool->resChainHead = tpool->resChainHead->next;
    if (tpool->resChainHead == NULL) tpool->resChainTail = NULL;
    mThreadPoolSelf->data = tpr->data;
    free(tpr);
    if (mThreadPoolSelf->data == NULL) goto again;

    mThreadPoolSelf->idle = 0;
    tpool->idle--;
    return mThreadPoolSelf->data;
}

/*
 * launcher
 */
int mln_thread_pool_run(struct mln_thread_pool_attr *tpattr)
{
    int rc;
    mln_thread_pool_t *tpool;

    if (tpattr->child_process_handler == NULL || \
        tpattr->main_process_handler == NULL)
    {
        return EINVAL;
    }

    if ((tpool = mln_thread_pool_new(tpattr, &rc)) == NULL) {
        return rc;
    }
    rc = tpattr->main_process_handler(tpattr->dataForMain);
    tpool->quit = 1;
    while (1) {
        mThreadPoolSelf->locked = 1;
        pthread_mutex_lock(&(tpool->mutex));
        if (tpool->counter <= 1) {
            pthread_mutex_unlock(&(tpool->mutex));
            mThreadPoolSelf->locked = 0;
            break;
        }
        pthread_cond_broadcast(&(tpool->cond));
        pthread_mutex_unlock(&(tpool->mutex));
        mThreadPoolSelf->locked = 0;
        usleep(50000);
    }
    mln_thread_pool_member_exit(mThreadPoolSelf);
    mThreadPoolSelf = NULL;
    mln_thread_pool_free(tpool);
    return rc;
}

static void *child_thread_launcher(void *arg)
{
    mln_sptr_t rc = 0;
    mln_u32_t forked = 0;
    pthread_cleanup_push(mln_thread_pool_member_exit, arg);

    struct timespec ts;
    mln_u32_t timeout = 0;
    mln_thread_pool_member_t *tpm = (mln_thread_pool_member_t *)arg;
    mln_thread_pool_t *tpool = tpm->pool;

    mThreadPoolSelf = tpm;

    while (1) {
        tpm->locked = 1;
        pthread_mutex_lock(&(tpool->mutex));

again:
        if (tpm->idle <= 0) {
            tpm->idle = 1;
            tpool->idle++;
        }
        if (tpool->quit) break;

        if (mln_thread_pool_removeResource() == NULL) {
            if (timeout) break;

            ts.tv_sec = time(NULL) + tpool->condTimeout / 1000;
            ts.tv_nsec = (tpool->condTimeout % 1000) * 1000000;
            if ((rc = pthread_cond_timedwait(&(tpool->cond), &(tpool->mutex), &ts)) != 0) {
                if (rc == ETIMEDOUT) {
                    timeout = 1;
                    goto again;
                }
                break;
            } else {
                timeout = 0;
                goto again;
            }
        }

        pthread_mutex_unlock(&(tpool->mutex));
        tpm->locked = 0;
        timeout = 0;
        if ((rc = tpool->process_handler(tpm->data)) != 0) {
            mln_log(error, "child process return %d, %s\n", rc, strerror(rc));
        }
        tpm->data = NULL;
    }

    forked = mThreadPoolSelf->forked;
    pthread_cleanup_pop(1);
    mThreadPoolSelf = NULL;
    if (forked) exit(rc);
    return (void *)rc;
}

void mln_thread_quit(void)
{
    if (mThreadPoolSelf == NULL) {
        mln_log(error, "Fatal error, thread messed up.\n");
        abort();
    }
    mln_thread_pool_t *tpool = mThreadPoolSelf->pool;
    mThreadPoolSelf->locked = 1;
    pthread_mutex_lock(&(tpool->mutex));
    tpool->quit = 1;
    pthread_mutex_unlock(&(tpool->mutex));
    mThreadPoolSelf->locked = 0;
}

MLN_CHAIN_FUNC_DEFINE(mln_child, \
                      mln_thread_pool_member_t, \
                      static inline void, \
                      prev, \
                      next);
