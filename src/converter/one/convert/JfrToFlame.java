/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.convert;

import one.jfr.JfrReader;
import one.jfr.StackTrace;
import one.jfr.event.AllocationSample;
import one.jfr.event.Event;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintStream;

import static one.convert.Frame.*;

/**
 * Converts .jfr output to HTML Flame Graph.
 */
public class JfrToFlame extends JfrConverter {
    private final FlameGraph fg;
    private final ThreadLocal<CallStack> stackPool = ThreadLocal.withInitial(CallStack::new);

    public JfrToFlame(JfrReader jfr, Arguments args) {
        super(jfr, args);
        this.fg = new FlameGraph(args);
    }

    @Override
    protected void convertChunk() {
        final boolean showThreads = args.threads;
        final boolean classify = args.classify;
        final boolean showLines = args.lines;
        final boolean showBci = args.bci;
        
        collector.forEach(new AggregatedEventVisitor() {
            @Override
            public void visit(Event event, long value) {
                StackTrace stackTrace = jfr.stackTraces.get(event.stackTraceId);
                if (stackTrace == null) {
                    return;
                }

                CallStack stack = stackPool.get();
                try {
                    processStackTrace(stack, stackTrace, event, value, showThreads, classify, showLines, showBci);
                } finally {
                    stack.clear();
                }
            }
        });
    }
    
    private void processStackTrace(CallStack stack, StackTrace stackTrace, Event event, long value,
                                   boolean showThreads, boolean classify, boolean showLines, boolean showBci) {
        long[] methods = stackTrace.methods;
        byte[] types = stackTrace.types;
        int[] locations = stackTrace.locations;

        // 优化：减少重复的条件检查
        if (showThreads) {
            stack.push(getThreadName(event.tid), TYPE_NATIVE);
        }
        
        if (classify) {
            Classifier.Category category = getCategory(stackTrace);
            stack.push(category.title, category.type);
        }
        
        // 优化：反向遍历方法数组
        for (int i = methods.length - 1; i >= 0; i--) {
            String methodName = getMethodName(methods[i], types[i]);
            
            // 优化：减少重复计算和条件检查
            if (showLines || showBci) {
                int location = locations[i];
                if (showLines && (location >>> 16) != 0) {
                    methodName += ":" + (location >>> 16);
                } else if (showBci && (location & 0xffff) != 0) {
                    methodName += "@" + (location & 0xffff);
                }
            }
            
            stack.push(methodName, types[i]);
        }
        
        // 优化：减少类型转换和条件判断
        long classId = event.classId();
        if (classId != 0) {
            byte frameType = TYPE_INLINED;
            if (event instanceof AllocationSample) {
                AllocationSample allocSample = (AllocationSample) event;
                if (allocSample.tlabSize == 0) {
                    frameType = TYPE_KERNEL;
                }
            }
            stack.push(getClassName(classId), frameType);
        }

        fg.addSample(stack, value);
    }

    public void dump(OutputStream out) throws IOException {
        try (PrintStream ps = new PrintStream(out, false, "UTF-8")) {
            fg.dump(ps);
        }
    }

    public static void convert(String input, String output, Arguments args) throws IOException {
        try (JfrReader jfr = new JfrReader(input);
             FileOutputStream out = new FileOutputStream(output)) {
            JfrToFlame converter = new JfrToFlame(jfr, args);
            converter.convert();
            converter.dump(out);
        }
    }
}
