/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "wallClock.h"
#include "profiler.h"
#include "stackFrame.h"


// Maximum number of threads sampled in one iteration. This limit serves as a throttle
// when generating profiling signals. Otherwise applications with too many threads may
// suffer from a big profiling overhead. Also, keeping this limit low enough helps
// to avoid contention on a spin lock inside Profiler::recordSample().
const int THREADS_PER_TICK = 8;

// Set the hard limit for thread walking interval to 100 microseconds.
// Smaller intervals are practically unusable due to large overhead.
const long MIN_INTERVAL = 100000;


long WallClock::_interval;
int WallClock::_signal;
bool WallClock::_sample_idle_threads;

ThreadState WallClock::getThreadState(void* ucontext) {
    StackFrame frame(ucontext);
    uintptr_t pc = frame.pc();

    // Consider a thread sleeping, if it has been interrupted in the middle of syscall execution,
    // either when PC points to the syscall instruction, or if syscall has just returned with EINTR
    if (StackFrame::isSyscall((instruction_t*)pc)) {
        return THREAD_SLEEPING;
    }

    // Make sure the previous instruction address is readable
    uintptr_t prev_pc = pc - SYSCALL_SIZE;
    if ((pc & 0xfff) >= SYSCALL_SIZE || Profiler::instance()->findLibraryByAddress((instruction_t*)prev_pc) != NULL) {
        if (StackFrame::isSyscall((instruction_t*)prev_pc) && frame.checkInterruptedSyscall()) {
            return THREAD_SLEEPING;
        }
    }

    return THREAD_RUNNING;
}

void WallClock::signalHandler(int signo, siginfo_t* siginfo, void* ucontext) {
    ExecutionEvent event;
    event._thread_state = _sample_idle_threads ? getThreadState(ucontext) : THREAD_UNKNOWN;
    Profiler::instance()->recordSample(ucontext, _interval, EXECUTION_SAMPLE, &event);
}

long WallClock::adjustInterval(long interval, int thread_count) {
    if (thread_count > THREADS_PER_TICK) {
        interval /= (thread_count + THREADS_PER_TICK - 1) / THREADS_PER_TICK;
    }
    return interval;
}

Error WallClock::start(Arguments& args) {
    _sample_idle_threads = args._wall >= 0 || strcmp(args._event, EVENT_WALL) == 0;

    _interval = args._wall >= 0 ? args._wall : args._interval;
    if (_interval == 0) {
        // Increase default interval for wall clock mode due to larger number of sampled threads
        _interval = _sample_idle_threads ? DEFAULT_INTERVAL * 5 : DEFAULT_INTERVAL;
    }

    _signal = args._signal == 0 ? SIGVTALRM : ((args._signal >> 8) > 0 ? args._signal >> 8 : args._signal);
    OS::installSignalHandler(_signal, signalHandler);

    _running = true;

    if (pthread_create(&_thread, NULL, threadEntry, this) != 0) {
        return Error("Unable to create timer thread");
    }

    return Error::OK;
}

void WallClock::stop() {
    _running = false;
    pthread_kill(_thread, WAKEUP_SIGNAL);
    pthread_join(_thread, NULL);
}

//std::string getCurrentTimeString() {
//    auto now = std::chrono::system_clock::now();
//    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
//            now.time_since_epoch()) % 1000;
//
//    std::time_t t = std::chrono::system_clock::to_time_t(now);
//    std::tm tm = *std::localtime(&t);
//
//    std::ostringstream oss;
//    oss << std::put_time(&tm, "%Y/%m/%d %H:%M:%S")
//        << "." << std::setfill('0') << std::setw(3) << ms.count();
//    return oss.str();
//}

void WallClock::timerLoop() {
    int self = OS::threadId();
    ThreadFilter* thread_filter = Profiler::instance()->threadFilter();
    bool thread_filter_enabled = thread_filter->enabled();
    bool sample_idle_threads = _sample_idle_threads;

    ThreadList* thread_list = OS::listThreads();
    u64 cycle_start_time = OS::nanotime();

    while (_running) {
        bool enabled = _enabled;

        for (int signaled_threads = 0; signaled_threads < THREADS_PER_TICK && thread_list->hasNext(); ) {
            int thread_id = thread_list->next();
            if (thread_id == self || thread_id <= 0) {
                // On macOS, task_threads() may sporadically return 0 or -1 among thread IDs
                continue;
            }
            if (thread_filter_enabled && !thread_filter->accept(thread_id)) {
                continue;
            }

            if (thread_id == self || (thread_filter_enabled && !thread_filter->accept(thread_id))) {
                continue;
            }
            if (sample_idle_threads || OS::threadState(thread_id) == THREAD_RUNNING) {
                if (enabled && OS::sendSignalToThread(thread_id, _signal)) {
                    signaled_threads++;
                }
            }
        }

        u64 current_time = OS::nanotime();
        if (thread_list->hasNext()) {
            long long sleep_time = cycle_start_time + (u64)_interval * thread_list->index() / thread_list->size() - current_time;
            OS::sleep(sleep_time < MIN_INTERVAL ? MIN_INTERVAL : sleep_time);
        } else {
            // Cycle has ended: prepare for the next cycle
            cycle_start_time += (u64)_interval;
            long long sleep_time = cycle_start_time - current_time;
            if (sleep_time < MIN_INTERVAL) {
                cycle_start_time = current_time + MIN_INTERVAL;
                sleep_time = MIN_INTERVAL;
            }
            OS::sleep(sleep_time);
            thread_list->rewind();
        }

    }

    delete thread_list;
}
