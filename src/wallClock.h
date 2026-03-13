/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _WALLCLOCK_H
#define _WALLCLOCK_H

#include <pthread.h>
#include <signal.h>
#include "engine.h"
#include "os.h"


class WallClock : public Engine {
  private:
    enum Mode {
        CPU_ONLY,
        WALL_BATCH,
        WALL_LEGACY
    };

    static long _interval;
    static int _signal;
    static Mode _mode;

    volatile bool _running = false;
    pthread_t _thread;

    void timerLoop();

    static void* threadEntry(void* wall_clock) {
        static_cast<WallClock*>(wall_clock)->timerLoop();
        return nullptr;
    }

    static ThreadState getThreadState(void* ucontext);
    static void signalHandler(int signo, siginfo_t* siginfo, void* ucontext);
    static void recordWallClock(u64 start_time, ThreadState state, u32 samples, int tid,
                                u32 call_trace_id, u64 trace_id, u64 span_id, u64 extend);

  public:
    WallClock() = default;
    WallClock(const WallClock&) = delete;
    WallClock& operator=(const WallClock&) = delete;
    WallClock(WallClock&&) = delete;
    WallClock& operator=(WallClock&&) = delete;

    const char* type() override {
        return "wall";
    }

    const char* title() override {
        return _mode == CPU_ONLY ? "CPU profile" : "Wall clock profile";
    }

    const char* units() override {
        return "ns";
    }

    Error start(Arguments& args) override;
    void stop() override;
};

#endif // _WALLCLOCK_H
