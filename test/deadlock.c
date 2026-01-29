#include "timeout.h"
#include "test.h"

#define NUM_THREADS 15

struct lock *lock1, *lock2;
struct cv *cv1;

static int
simple_deadlock_1(Tid *tid)
{
    int ret;
    lock_acquire(lock1);
    thread_yield(*tid);
    ret = lock_acquire(lock2);
    // If we acquired the lock, make sure we release it.
    if (ret > 0)
        lock_release(lock2);
    lock_release(lock1);
    return ret;
}

static int
simple_deadlock_2(Tid *tid)
{
    int ret;
    lock_acquire(lock2);
    thread_yield(*tid);
    ret = lock_acquire(lock1);
    // If we acquired the lock, make sure we release it.
    if (ret > 0)
        lock_release(lock1);
    lock_release(lock2);
    return ret;
}

static int
thread_waiter(Tid *tid)
{
    int ret = 0;
    ret = thread_wait(*tid, NULL);
    return ret;
}

static int
cv_waiter(struct cv *cv)
{
    int ret = lock_acquire(lock1);
    assert(ret == 0);
    ret = cv_wait(cv);
    if (ret >= 0)
        lock_release(lock1);
    return ret;
}

static int
lock_acquirer(struct lock *lock)
{
    int ret = lock_acquire(lock);
    if (ret >= 0)
        lock_release(lock);
    return ret;
}

static int
cv_deadlocker(Tid *tid)
{
    int ret = lock_acquire(lock1);
    assert(ret == 0);
    cv_signal(cv1);
    int exit_code;
    ret = thread_wait(*tid, &exit_code);
    assert(ret == *tid);
    lock_release(lock1);
    return exit_code;
}

static int
lock_yield_wait(Tid *tid)
{
    lock_acquire(lock1);
    thread_yield(THREAD_ANY);
    int ret = thread_wait(*tid, NULL);
    lock_release(lock1);
    return ret;
}

static int
test_circular_lock_holding(void)
{
    long tid1, tid2;
    int exit_code1, exit_code2;
    Tid ret1, ret2;

    // Testing deadlock caused by lock holding.
    lock1 = lock_create();
    lock2 = lock_create();
    tid1 = thread_create((thread_entry_f)simple_deadlock_1, (void*)&tid2);
    tid2 = thread_create((thread_entry_f)simple_deadlock_2, (void*)&tid1);

    ret1 = thread_wait(tid1, &exit_code1);
    assert(ret1 == tid1);
    ret2 = thread_wait(tid2, &exit_code2);
    assert(ret2 == tid2);

    // Only one of the threads should deadlock and fail, not both.
    assert((exit_code1 == THREAD_DEADLOCK) != (exit_code2 == THREAD_DEADLOCK));
    return 0;
}

static int
test_circular_wait(void)
{
    long tid1, tid2;
    int exit_code1, exit_code2;
    Tid ret1, ret2;

    // Testing deadlock caused by simple circular wait.
    tid1 = thread_create((thread_entry_f)thread_waiter, (void*)&tid2);
    tid2 = thread_create((thread_entry_f)thread_waiter, (void*)&tid1);

    ret1 = thread_wait(tid1, &exit_code1);
    assert(ret1 == tid1);
    ret2 = thread_wait(tid2, &exit_code2);
    assert(ret2 == tid2);

    // Only one of the threads should deadlock and fail, not both.
    assert((exit_code1 == THREAD_DEADLOCK) != (exit_code2 == THREAD_DEADLOCK));
    return 0;
}

static int
test_extensive_circular_wait(void)
{
    Tid ret;
    Tid tids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS - 1; i++) {
        // Make all threads wait for the next thread in the list.
        tids[i] = thread_create((thread_entry_f)thread_waiter, &tids[i + 1]);
    }

    int tmp = thread_id();
    tids[NUM_THREADS - 1] = thread_create((thread_entry_f)thread_waiter, &tmp);

    // Make all the threads start waiting.
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_yield(tids[i]);
    }

    int num_deadlock = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        ret = thread_wait(tids[i], NULL);
        if (ret == THREAD_DEADLOCK)
            num_deadlock++;
    }

    // All the threads should have caused a deadlock, because the current
    // thread waiting on them should cause a circular wait (because starting
    // from thread x, x waits on x+1, x+1 waits on x+2... x+n waits on 0).
    assert(num_deadlock == NUM_THREADS);
    return 0;
}

static int
test_wait_on_lock_waiter(void)
{
    long tid1, tid2;
    int exit_code1, exit_code2;
    Tid ret1, ret2;

    // Testing deadlock caused by waiting on a thread that is waiting on a lock
    // that you hold.
    lock1 = lock_create();
    tid1 = thread_create((thread_entry_f)lock_yield_wait, (void*)&tid2);
    tid2 = thread_create((thread_entry_f)thread_waiter, (void*)&tid1);

    ret1 = thread_wait(tid1, &exit_code1);
    assert(ret1 == tid1);
    ret2 = thread_wait(tid2, &exit_code2);
    assert(ret2 == tid2);

    // Only one of the threads should deadlock and fail, not both.
    assert((exit_code1 == THREAD_DEADLOCK) != (exit_code2 == THREAD_DEADLOCK));
    return 0;
}

static int
test_cv_wait_on_waiter(void)
{
    long tid1, tid2;
    int exit_code2;
    Tid ret;

    // cv_wait causing deadlock (other thread calls signal, waits for the
    // thread waiting on the cv before freeing the lock).
    lock1 = lock_create();
    cv1 = cv_create(lock1);
    tid1 = thread_create((thread_entry_f)cv_waiter, (void*)cv1);
    tid2 = thread_create((thread_entry_f)cv_deadlocker, (void*)&tid1);

    // Make tid1 wait for the cv.
    ret = thread_yield(tid1);
    assert(ret == tid1);

    // Sleep waiting for tid2. This should cause the deadlock.
    ret = thread_wait(tid2, &exit_code2);
    assert(ret == tid2);

    // tid1 should have been reaped by tid2
    ret = thread_wait(tid1, NULL);
    assert(ret == THREAD_INVALID);

    // Only the second thread should fail.
    assert(exit_code2 == THREAD_DEADLOCK);
    return 0;
}

static int
test_cv_wait_no_runnable(void)
{
    long tid1;

    // 2 cv_wait resulting in THREAD_NONE
    lock1 = lock_create();
    cv1 = cv_create(lock1);
    tid1 = thread_create((thread_entry_f)cv_waiter, (void*)cv1);

    thread_yield(tid1);
    lock_acquire(lock1);

    // This should fail, because we would have no threads to run.
    int ret = cv_wait(cv1);
    cv_signal(cv1);

    assert((ret == THREAD_NONE));
    return 0;
}

static int
test_lock_no_runnable(void)
{
    long tid1, tid2;
    int exit_code1, exit_code2;
    Tid ret1, ret2;

    lock1 = lock_create();
    lock2 = lock_create();
    cv1 = cv_create(lock1);

    // Make tid1 wait for the cv.
    tid1 = thread_create((thread_entry_f)cv_waiter, (void*)cv1);
    ret1 = thread_yield(tid1);
    assert(ret1 == tid1);

    // Wait for tid2 to run.
    tid2 = thread_create((thread_entry_f)lock_acquirer, (void*)lock2);
    lock_acquire(lock2);
    ret2 = thread_wait(tid2, &exit_code2);
    lock_release(lock2);
    assert(ret2 == tid2);

    // Signal tid1 and make it come back.
    cv_signal(cv1);
    ret1 = thread_wait(tid1, &exit_code1);

    // The second thread should have caught the deadlock.
    assert((exit_code1 == 0) && (exit_code2 == THREAD_DEADLOCK));
    return 0;
}

static int
test_wait_no_runnable(void)
{
    long tid1, tid2;
    int exit_code2;
    Tid ret1, ret2;

    // 1 cv_wait and 1 thread_wait resulting in THREAD_NONE
    lock1 = lock_create();
    cv1 = cv_create(lock1);
    tid1 = thread_create((thread_entry_f)cv_waiter, (void*)cv1);
    tid2 = thread_create((thread_entry_f)thread_waiter, (void*)&tid1);

    ret1 = thread_yield(tid1);
    assert(ret1 == tid1);
    ret2 = thread_wait(tid2, &exit_code2);
    assert(ret2 == tid2);

    // The second thread we created should catch the deadlock.
    assert((exit_code2 == THREAD_NONE));
    return 0;
}

testcase_t test_case[] = {
    { "Circular Lock Holding", test_circular_lock_holding },
    { "Circular Wait", test_circular_wait },
    { "Extensive Circular Wait", test_extensive_circular_wait },
    { "Wait on Waiter of Your Lock", test_wait_on_lock_waiter },
    { "CV Wait on Waiter", test_cv_wait_on_waiter },
    { "CV Wait - No Runnable Threads", test_cv_wait_no_runnable },
    { "Lock Acquire - No Runnable Threads", test_lock_no_runnable },
    { "Thread Wait - No Runnable Threads", test_wait_no_runnable },
};

int nr_cases = sizeof(test_case) / sizeof(struct _tc);
const int timeout_secs = 5;

int
run_test_case(int test_id)
{
    struct config config = { 
        .sched_name = "rand", .preemptive = false, .verbose = false
    };

    ut369_start(&config);
    return test_case[test_id].func();
}

void
wait_process(pid_t child_pid)
{
    int status = selfpipe_waitpid(child_pid, timeout_secs);
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            printf("PASSED\n");
        }
        else {
            printf("FAILED (-%d)\n", code);
        }
    } else if (WIFSIGNALED(status)) {
        int signum = WTERMSIG(status);
        if (signum == SIGKILL) {
            printf("TIMEOUT\n");
        }
        else {
            printf("FAILED (%d)\n", signum);
        }
    }
    else {
        printf("UNKNOWN\n");
    }
}

int
main(int argc, const char * argv[])
{  
    return main_process("deadlock", argc, argv);
}