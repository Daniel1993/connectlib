#ifndef UTIL_SYNC_H_GUARD_
#define UTIL_SYNC_H_GUARD_
    
#define UTIL_SYNC_QUEUE_SIZE 1024 // 1<<10 has to be power2
#define UTIL_SYNC_QUEUE_MASK 1023 // 0x3FF

typedef struct util_thrd_ *util_thrd_s;
typedef struct util_thrd_work_ *util_thrd_work_s;
struct util_thrd_work_
{
    void (*callback)(void*);
    void *args;
};

util_thrd_s util_thrd_start_pool(int nb_thrs);
void util_thrd_stop_pool(util_thrd_s);

// it does block if queue is full!
// work item is memcpy'ed to queue
void util_thrd_push_work(util_thrd_s, util_thrd_work_s);

#endif /* UTIL_SYNC_H_GUARD_ */
