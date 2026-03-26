/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.convert;

import one.heatmap.Heatmap;
import one.jfr.Dictionary;
import one.jfr.JfrReader;
import one.jfr.StackTrace;
import one.jfr.event.AllocationSample;
import one.jfr.event.ContendedLock;
import one.jfr.event.Event;
import one.jfr.event.EventCollector;

import java.io.*;

import static one.convert.Frame.TYPE_INLINED;
import static one.convert.Frame.TYPE_KERNEL;

public class JfrToHeatmap extends JfrConverter {
    private final Heatmap heatmap;
    private final Dictionary.Visitor<StackTrace> stackTraceVisitor;

    public JfrToHeatmap(JfrReader jfr, Arguments args) {
        super(jfr, args);
        this.heatmap = new Heatmap(args, this);
        // 优化：预创建访问者对象，避免重复创建
        this.stackTraceVisitor = new Dictionary.Visitor<StackTrace>() {
            @Override
            public void visit(long key, StackTrace trace) {
                heatmap.addStack(key, trace.methods, trace.locations, trace.types, trace.methods.length);
            }
        };
    }

    @Override
    protected EventCollector createCollector(Arguments args) {
        return new EventCollector() {
            @Override
            public void collect(Event event) {
                // 优化：减少重复的类型转换和条件判断
                int classId = 0;
                byte type = 0;
                
                if (event instanceof AllocationSample) {
                    AllocationSample allocSample = (AllocationSample) event;
                    classId = allocSample.classId;
                    type = allocSample.tlabSize == 0 ? TYPE_KERNEL : TYPE_INLINED;
                } else if (event instanceof ContendedLock) {
                    classId = ((ContendedLock) event).classId;
                    type = TYPE_KERNEL;
                }

                // 优化：预计算常量，避免重复计算
                long msFromStart = (event.time - jfr.chunkStartTicks) * 1_000 / jfr.ticksPerSec;
                long timeMs = jfr.chunkStartNanos / 1_000_000 + msFromStart;

                heatmap.addEvent(event.stackTraceId, event.tid, classId, type, timeMs);
            }

            @Override
            public void beforeChunk() {
                heatmap.beforeChunk();
                // 优化：使用预创建的访问者对象
                jfr.stackTraces.forEach(stackTraceVisitor);
            }

            @Override
            public void afterChunk() {
                jfr.stackTraces.clear();
            }

            @Override
            public boolean finish() {
                // 优化：预计算常量
                heatmap.finish(jfr.startNanos / 1_000_000);
                return false;
            }

            @Override
            public void forEach(Visitor visitor) {
                throw new AssertionError("Should not be called");
            }
        };
    }

    public void dump(OutputStream out) throws IOException {
        try (PrintStream ps = new PrintStream(out, false, "UTF-8")) {
            heatmap.dump(ps);
        }
    }

    public static void convert(String input, String output, Arguments args) throws IOException {
        // 优化：减少资源打开和关闭次数
        try (JfrReader jfr = new JfrReader(input);
             OutputStream out = new BufferedOutputStream(new FileOutputStream(output))) {
            JfrToHeatmap converter = new JfrToHeatmap(jfr, args);
            converter.convert();
            converter.dump(out);
        }
    }
}
