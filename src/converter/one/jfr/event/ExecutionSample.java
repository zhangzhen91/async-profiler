/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.jfr.event;

public class ExecutionSample extends Event {
    public final int threadState;

    public ExecutionSample(long time, int tid, int stackTraceId, int threadState) {
        super(time, tid, stackTraceId);
        this.threadState = threadState;
    }

    public ExecutionSample(long time, int tid, int stackTraceId, int threadState, long traceId, long spanId) {
        super(time, tid, stackTraceId, traceId, spanId);
        this.threadState = threadState;
    }

    @Override
    public boolean sameGroup(Event o) {
        return traceId == o.traceId && spanId == o.spanId;
    }
}
