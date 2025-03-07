/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.jfr.event;

import java.lang.reflect.Field;

public abstract class Event implements Comparable<Event> {
    public final long time;
    public final int tid;
    public final int stackTraceId;
    public long traceId = 0;
    public long spanId = 0;

    protected Event(long time, int tid, int stackTraceId) {
        this.time = time;
        this.tid = tid;
        this.stackTraceId = stackTraceId;
    }

    protected Event(long time, int tid, int stackTraceId, long traceId, long spanId) {
        this.time = time;
        this.tid = tid;
        this.stackTraceId = stackTraceId;
        this.traceId = traceId;
        this.spanId = spanId;
    }

    @Override
    public int compareTo(Event o) {
        return Long.compare(time, o.time);
    }

    @Override
    public int hashCode() {
        return stackTraceId;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder(getClass().getSimpleName())
                .append("{time=").append(time)
                .append(",tid=").append(tid)
                .append(",stackTraceId=").append(stackTraceId);
        for (Field f : getClass().getDeclaredFields()) {
            try {
                sb.append(',').append(f.getName()).append('=').append(f.get(this));
            } catch (ReflectiveOperationException e) {
                break;
            }
        }
        return sb.append('}').toString();
    }

    public boolean sameGroup(Event o) {
        return getClass() == o.getClass();
    }

    public long value() {
        return 1;
    }
}
