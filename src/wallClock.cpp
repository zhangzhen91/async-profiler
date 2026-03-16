/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "wallClock.h"
#include "profiler.h"
#include "stackFrame.h"
#include "tsc.h"
#include "context.h"

// Maximum number of threads sampled in one iteration. This limit serves as a throttle
// when generating profiling signals. Otherwise applications with too many threads may
// suffer from a big profiling overhead. Also, keeping this limit low enough helps
// to avoid contention on a spin lock inside Profiler::recordSample().
const int THREADS_PER_TICK = 8;

// Set the hard limit for thread walking interval to 100 microseconds.
// Smaller intervals are practically unusable due to large overhead.
const long long MIN_INTERVAL = 100000;  // 100 microseconds in nanoseconds

// How much CPU time a thread can spend after being sampled as idle
// until it is considered runnable.
const u64 RUNNABLE_THRESHOLD_NS = 10000;  // 10 microseconds in nanoseconds

// How many skipped idle samples can be recorded in a single WallClock event.
const u32 MAX_IDLE_BATCH = 1000;

// Static assertions for compile-time validation
static_assert(THREADS_PER_TICK > 0, "THREADS_PER_TICK must be positive");
static_assert(MIN_INTERVAL > 0, "MIN_INTERVAL must be positive");
static_assert(RUNNABLE_THRESHOLD_NS > 0, "RUNNABLE_THRESHOLD_NS must be positive");
static_assert(MAX_IDLE_BATCH > 0, "MAX_IDLE_BATCH must be positive");


/**
 * Tracks the sleep state of a thread for wall clock profiling.
 * This structure maintains information about when a thread started sleeping,
 * its last known CPU time, and associated trace/span information.
 */
struct ThreadSleepState {
    u64 start_time;      ///< Timestamp when thread started sleeping (in ticks)
    u64 last_cpu_time;   ///< Last recorded CPU time for this thread (in nanoseconds)
    u32 call_trace_id;   ///< ID of the call trace when thread went to sleep
    int64_t trace_id;    ///< Current trace ID for distributed tracing
    int64_t span_id;     ///< Current span ID for distributed tracing
    int64_t extend;      ///< Extended trace information
    u32 counter;         ///< Number of consecutive idle samples
};

/** Map of thread ID to thread sleep state */
typedef std::map<int, ThreadSleepState> ThreadSleepMap;

/**
 * Stores CPU time information for a thread.
 * Used in the ring buffer to track thread CPU usage.
 */
struct ThreadCpuTime {
    u64 cpu_time;  ///< CPU time in nanoseconds
    u64 trace;     ///< Encoded thread ID and call trace ID
};

/**
 * Multi-Producer Single-Consumer (MPSC) ring buffer for storing thread CPU time information.
 * This lock-free data structure is used to efficiently transfer CPU time data from
 * signal handler context to the main timer thread.
 */
class ThreadCpuTimeBuffer {
  private:
    enum {
        RINGBUF_SIZE = 256,  ///< Must be power of 2 for efficient modulo operations
        PAD_SIZE = 128       ///< Padding to prevent false sharing between fields
    };

    // Padding to prevent false sharing between producer and consumer
    char _pad0[PAD_SIZE];           ///< Padding before write pointer
    volatile u32 _write_ptr;        ///< Producer index (modified by signal handlers)
    char _pad1[PAD_SIZE - sizeof(u32)];  ///< Padding between write and read pointers
    u32 _read_ptr;                  ///< Consumer index (modified by timer thread)
    char _pad2[PAD_SIZE - sizeof(u32)];  ///< Padding before ring buffer data
    ThreadCpuTime _ringbuf[RINGBUF_SIZE];  ///< Ring buffer storage

  public:
    /**
     * Constructor initializes the ring buffer to zero state.
     */
    ThreadCpuTimeBuffer() : _ringbuf(), _write_ptr(0), _read_ptr(0) {
    }

    /**
     * Reset the ring buffer to initial state.
     * Called when wall clock profiling starts.
     */
    void reset() {
        memset(_ringbuf, 0, sizeof(_ringbuf));
        _read_ptr = 0;
        __atomic_store_n(&_write_ptr, 0, __ATOMIC_RELEASE);
    }

    /**
     * Add a new CPU time entry to the ring buffer.
     * Called from signal handler context (producer side).
     *
     * @param trace Encoded thread ID and call trace ID
     */
    void add(u64 trace) {
        // Get next available slot in ring buffer
        ThreadCpuTime& t = _ringbuf[atomicInc(_write_ptr) & (RINGBUF_SIZE - 1)];
        t.trace = trace;
        // Store current CPU time with release semantics
        storeRelease(t.cpu_time, OS::threadCpuTime(0));
    }

    /**
     * Drain all available entries from the ring buffer.
     * Called from timer thread context (consumer side).
     *
     * @param thread_sleep_state Map to update with drained CPU time information
     */
    void drain(ThreadSleepMap& thread_sleep_state) {
        u32 read_ptr = _read_ptr;
        const u32 read_limit = read_ptr + RINGBUF_SIZE;

        while (read_ptr < read_limit) {
            ThreadCpuTime& t = _ringbuf[read_ptr & (RINGBUF_SIZE - 1)];
            u64 cpu_time = loadAcquire(t.cpu_time);

            // Exit if no more data available
            if (likely(cpu_time == 0)) {
                break;
            }

            u64 trace = t.trace;
            // Atomically consume the entry with weaker memory ordering than seq_cst CAS
            u64 expected = cpu_time;
            if (__atomic_compare_exchange_n(&t.cpu_time, &expected, 0, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                int thread_id = trace >> 32;
                ThreadSleepState& tss = thread_sleep_state[thread_id];
                tss.last_cpu_time = cpu_time;
                tss.call_trace_id = (u32)trace;
                tss.counter = 0;
                read_ptr++;
            }
        }

        _read_ptr = read_ptr;
    }
};

static ThreadCpuTimeBuffer _thread_cpu_time_buf;


long WallClock::_interval;
int WallClock::_signal;
WallClock::Mode WallClock::_mode;

/**
 * Determines the execution state of a thread based on its program counter.
 * This function analyzes the current instruction to determine if the thread
 * is executing a system call (sleeping) or running user code.
 *
 * @param ucontext User context containing CPU registers
 * @return ThreadState indicating whether the thread is sleeping or running
 */
ThreadState WallClock::getThreadState(void* ucontext) {
    StackFrame frame(ucontext);
    instruction_t* pc = (instruction_t*)frame.pc();

    // Fast path: current PC points to syscall instruction
    if (StackFrame::isSyscall(pc)) {
        return THREAD_SLEEPING;
    }

    // Check if previous instruction was a syscall interrupted with EINTR.
    // Most of the time prev_pc is in the same page; avoid library lookup on this hot path.
    const uintptr_t prev_pc = (uintptr_t)pc - SYSCALL_SIZE;
    instruction_t* prev_insn = (instruction_t*)prev_pc;

    if (likely((((uintptr_t)pc & 0xfff) >= SYSCALL_SIZE))) {
        if (StackFrame::isSyscall(prev_insn) && frame.checkInterruptedSyscall()) {
            return THREAD_SLEEPING;
        }
    } else if (Profiler::instance()->findLibraryByAddress(prev_insn) != NULL &&
               StackFrame::isSyscall(prev_insn) && frame.checkInterruptedSyscall()) {
        return THREAD_SLEEPING;
    }

    return THREAD_RUNNING;
}

/**
 * Signal handler for wall clock profiling.
 * This function is called periodically for each profiled thread to record
 * its current execution state.
 *
 * @param signo Signal number
 * @param siginfo Signal information
 * @param ucontext User context containing CPU registers
 */
void WallClock::signalHandler(int signo, siginfo_t* siginfo, void* ucontext) {
    const Mode mode = _mode;
    const u64 start_time = TSC::ticks();
    Profiler* profiler = Profiler::instance();

    // Handle based on profiling mode
    if (mode == WALL_BATCH) {
        // Batch mode: record detailed wall clock information including thread state
        WallClockEvent event;
        event._start_time = start_time;
        event._thread_state = getThreadState(ucontext);
        event._samples = 1;

        // Record the sample and get encoded thread/trace information
        u64 trace = profiler->recordSample(ucontext, _interval, WALL_CLOCK_SAMPLE, &event);

        // If thread is sleeping, add CPU time info to buffer for batch processing
        if (trace != 0 && event._thread_state == THREAD_SLEEPING) {
            _thread_cpu_time_buf.add(trace);
        }
    } else {
        // Legacy/CPU mode: record execution sample
        ExecutionEvent event(start_time);
        // For CPU-only mode, don't determine thread state to reduce overhead
        event._thread_state = mode == CPU_ONLY ? THREAD_UNKNOWN : getThreadState(ucontext);
        profiler->recordSample(ucontext, _interval, EXECUTION_SAMPLE, &event);
    }
}

/**
 * Records a batch of wall clock samples for a sleeping thread.
 * This function is called when a thread has been idle for multiple sampling intervals,
 * allowing efficient batch recording of idle samples.
 *
 * @param start_time Timestamp when the idle period started
 * @param state Thread state (should be THREAD_SLEEPING)
 * @param samples Number of consecutive idle samples
 * @param tid Thread ID
 * @param call_trace_id Call trace ID when thread went idle
 * @param trace_id Current trace ID for distributed tracing
 * @param span_id Current span ID for distributed tracing
 * @param extend Extended trace information
 */
void WallClock::recordWallClock(u64 start_time, ThreadState state, u32 samples, int tid,
                                u32 call_trace_id, u64 trace_id, u64 span_id, u64 extend) {
    // Create wall clock event with batch information
    WallClockEvent event;
    event._start_time = start_time;    // Timestamp of first idle sample
    event._thread_state = state;       // Thread state (sleeping)
    event._samples = samples;          // Number of batched samples
    event.trace_id = trace_id;         // Distributed trace ID
    event.span_id = span_id;           // Distributed span ID
    event.extend = extend;             // Extended trace information

    // Record the batched samples with total duration
    const u64 duration = (u64)samples * (u64)_interval;
    Profiler* profiler = Profiler::instance();
    profiler->recordExternalSamples(samples, duration, tid,
                                    call_trace_id, WALL_CLOCK_SAMPLE, &event);
}

/**
 * Starts the wall clock profiler with specified arguments.
 *
 * @param args Command line arguments containing profiling configuration
 * @return Error::OK if successful, or an error message
 */
Error WallClock::start(Arguments& args) {
    // Determine profiling mode based on arguments
    const bool wall_requested = args._wall >= 0 || strcmp(args._event, EVENT_WALL) == 0;
    _mode = wall_requested ? (args._nobatch ? WALL_LEGACY : WALL_BATCH) : CPU_ONLY;

    // Set sampling interval
    long interval = args._wall >= 0 ? args._wall : args._interval;
    if (interval == 0) {
        // Use default interval based on mode
        // Increase default for wall clock mode due to larger number of sampled threads
        interval = _mode == CPU_ONLY ? DEFAULT_INTERVAL : DEFAULT_INTERVAL * 5;
    }

    // Validate interval is not too small to avoid excessive overhead
    if (interval < MIN_INTERVAL) {
        interval = MIN_INTERVAL;
    }
    _interval = interval;

    // Set up signal for thread sampling
    const int signal = args._signal;
    if (signal == 0) {
        _signal = OS::getProfilingSignal(1);
    } else {
        int shifted = signal >> 8;
        _signal = shifted > 0 ? shifted : signal;
    }

    // Install signal handler
    OS::installSignalHandler(_signal, signalHandler);

    // Start timer thread
    _running = true;
    if (pthread_create(&_thread, NULL, threadEntry, this) != 0) {
        _running = false;
        return Error("Unable to create timer thread");
    }

    return Error::OK;
}

/**
 * Stops the wall clock profiler and cleans up resources.
 * This function signals the timer thread to stop and waits for it to complete.
 */
void WallClock::stop() {
    // Signal timer thread to stop
    _running = false;
    
    // Wake up the timer thread if it's sleeping
    pthread_kill(_thread, WAKEUP_SIGNAL);
    
    // Wait for timer thread to complete
    pthread_join(_thread, NULL);
}

/**
 * Main timer loop for wall clock profiling.
 * This function runs in a separate thread and periodically samples all threads
 * to determine their execution state (running vs. sleeping).
 */
void WallClock::timerLoop() {
    // Get current thread ID to exclude it from sampling
    const int self = OS::threadId();
    
    // Get thread filter for selective profiling
    ThreadFilter* thread_filter = Profiler::instance()->threadFilter();
    const bool thread_filter_enabled = thread_filter->enabled();
    const Mode mode = _mode;

    // Initialize thread state tracking
    ThreadSleepMap thread_sleep_state;
    ThreadList* thread_list = OS::listThreads();
    _thread_cpu_time_buf.reset();
    u64 cycle_start_time = OS::nanotime();

    // Main profiling loop
    while (_running) {
        const bool enabled = _enabled;

        // Sample threads for this iteration
        for (int signaled_threads = 0; signaled_threads < THREADS_PER_TICK && thread_list->hasNext(); ) {
            int thread_id = thread_list->next();
            
            // Skip invalid thread IDs and self
            if (thread_id == self || thread_id <= 0) {
                // On macOS, task_threads() may sporadically return 0 or -1 among thread IDs
                continue;
            }
            
            // Apply thread filter if enabled
            if (thread_filter_enabled && !thread_filter->accept(thread_id)) {
                continue;
            }

            // Handle based on profiling mode
            if (mode == CPU_ONLY) {
                // CPU-only mode: skip sleeping threads
                if (!enabled || OS::threadState(thread_id) == THREAD_SLEEPING) {
                    continue;
                }
            } else if (mode == WALL_BATCH) {
                // Batch mode: track idle thread batches
                ThreadSleepState& tss = thread_sleep_state[thread_id];
                
                // Check for trace/span changes for distributed tracing
                ThreadContext* currentThreadContext = Context::getInstance().getThreadContext(thread_id);
                if (currentThreadContext != NULL) {
                    // If trace/span has changed, flush any pending idle samples
                    if (tss.trace_id != currentThreadContext->getTraceId() ||
                        tss.span_id != currentThreadContext->getSpanId()) {
                        if (tss.counter != 0) {
                            recordWallClock(tss.start_time, THREAD_SLEEPING, tss.counter,
                                          thread_id, tss.call_trace_id, tss.trace_id,
                                          tss.span_id, tss.extend);
                        }
                        // Reset tracking for new trace/span
                        tss.counter = 0;
                        tss.trace_id = currentThreadContext->getTraceId();
                        tss.span_id = currentThreadContext->getSpanId();
                        tss.extend = currentThreadContext->getExtend();
                    }
                }

                // Check if thread is still idle
                u64 new_thread_cpu_time = enabled ? OS::threadCpuTime(thread_id) : 0;
                if (new_thread_cpu_time != 0 &&
                    new_thread_cpu_time - tss.last_cpu_time <= RUNNABLE_THRESHOLD_NS) {
                    // Thread is still idle, increment counter
                    if (++tss.counter < MAX_IDLE_BATCH) {
                        if (tss.counter == 1) {
                            // First idle sample, record start time
                            tss.start_time = TSC::ticks();
                        }
                        continue;  // Skip signaling this idle thread
                    }
                }
                
                // Thread is no longer idle or batch limit reached, flush samples
                if (tss.counter != 0) {
                    recordWallClock(tss.start_time, THREAD_SLEEPING, tss.counter,
                                  thread_id, tss.call_trace_id, tss.trace_id,
                                  tss.span_id, tss.extend);
                    tss.counter = 0;
                }
            }

            // Send profiling signal to thread if enabled
            if (enabled && OS::sendSignalToThread(thread_id, _signal)) {
                signaled_threads++;
            }
        }

        // Calculate sleep time for next iteration
        const u64 current_time = OS::nanotime();
        if (thread_list->hasNext()) {
            // Still processing threads in current cycle
            // Distribute sleep time evenly across threads to maintain consistent interval
            long long sleep_time = cycle_start_time +
                                 (u64)_interval * thread_list->index() / thread_list->count() -
                                 current_time;
            OS::uninterruptibleSleep(sleep_time < MIN_INTERVAL ? MIN_INTERVAL : sleep_time, &_running);
        } else {
            // Completed current cycle, prepare for next cycle
            cycle_start_time += (u64)_interval;
            long long sleep_time = cycle_start_time - current_time;
            
            // Ensure minimum sleep time to prevent busy waiting
            if (sleep_time < MIN_INTERVAL) {
                cycle_start_time = current_time + MIN_INTERVAL;
                sleep_time = MIN_INTERVAL;
            }
            
            OS::uninterruptibleSleep(sleep_time, &_running);
            
            // Update thread list for next cycle
            thread_list->update();
        }

        // Process CPU time updates from signal handlers
        _thread_cpu_time_buf.drain(thread_sleep_state);
    }

    // Clean up thread list
    delete thread_list;

    // Flush any remaining idle samples
    for (ThreadSleepMap::const_iterator it = thread_sleep_state.begin();
         it != thread_sleep_state.end(); ++it) {
        const ThreadSleepState& tss = it->second;
        if (tss.counter != 0) {
            recordWallClock(tss.start_time, THREAD_SLEEPING, tss.counter,
                          it->first, tss.call_trace_id, tss.trace_id,
                          tss.span_id, tss.extend);
        }
    }
}
