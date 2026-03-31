/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.convert;

import one.jfr.JfrReader;
import one.jfr.StackTrace;
import one.jfr.event.Event;
import one.proto.Proto;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.zip.GZIPOutputStream;

/**
 * Converts .jfr output to <a href="https://github.com/google/pprof">pprof</a>.
 */
public class JfrToPprof extends JfrConverter {
    private static final int INITIAL_BUFFER_SIZE = 100000;
    private static final int SAMPLE_BUFFER_SIZE = 100;
    private static final int GZIP_BUFFER_SIZE = 4096;
    private static final int PROTO_BUFFER_SIZE = 16;

    private final Proto profile = new Proto(INITIAL_BUFFER_SIZE);
    private final Index<String> strings = new Index<>(String.class, "");
    private final Index<String> functions = new Index<>(String.class, "");
    private final Index<Long> locations = new Index<>(Long.class, 0L);

    public JfrToPprof(JfrReader jfr, Arguments args) {
        super(jfr, args);

        // 优化：预计算常用值
        String valueType = getValueType();
        String units = args.total ? getTotalUnits() : getSampleUnits();
        
        profile.field(1, valueType(valueType, units))
                .field(13, strings.index("Produced by async-profiler"));
    }

    @Override
    protected void convertChunk() {
        final Proto sampleProto = new Proto(SAMPLE_BUFFER_SIZE);
        collector.forEach(new AggregatedEventVisitor() {
            @Override
            public void visit(Event event, long value) {
                profile.field(2, sample(sampleProto, event, value));
                sampleProto.reset();
            }
        });
    }

    public void dump(OutputStream out) throws IOException {
        // 优化：按顺序写入，减少重复计算
        writeMapping();
        writeLocations();
        writeFunctions();
        writeStrings();
        writeTiming();

        out.write(profile.buffer(), 0, profile.size());
    }

    private void writeMapping() {
        profile.field(3, mapping(1, 0, Long.MAX_VALUE, "async-profiler"));
    }

    private void writeLocations() {
        Long[] locationsArray = this.locations.keys();
        for (int i = 1; i < locationsArray.length; i++) {
            profile.field(4, location(i, locationsArray[i]));
        }
    }

    private void writeFunctions() {
        String[] functionsArray = this.functions.keys();
        for (int i = 1; i < functionsArray.length; i++) {
            profile.field(5, function(i, functionsArray[i]));
        }
    }

    private void writeStrings() {
        String[] stringsArray = this.strings.keys();
        for (String string : stringsArray) {
            profile.field(6, string);
        }
    }

    private void writeTiming() {
        profile.field(9, jfr.startNanos)
                .field(10, jfr.durationNanos());
    }

    private Proto sample(Proto s, Event event, long value) {
        long packedLocations = s.startField(1, 3);

        long classId = event.classId();
        if (classId != 0) {
            int function = functions.index(getClassName(classId));
            s.writeInt(locations.index((long) function << 16));
        }

        StackTrace stackTrace = jfr.stackTraces.get(event.stackTraceId);
        if (stackTrace != null) {
            long[] methods = stackTrace.methods;
            byte[] types = stackTrace.types;
            int[] lines = stackTrace.locations;
            int length = methods.length;
            
            // 优化：减少重复计算
            for (int i = 0; i < length; i++) {
                String methodName = getMethodName(methods[i], types[i]);
                int function = functions.index(methodName);
                s.writeInt(locations.index((long) function << 16 | (lines[i] >>> 16)));
            }
        }

        s.commitField(packedLocations);
        s.field(2, value);

        // 优化：减少重复的条件判断
        if (args.threads && event.tid != 0) {
            s.field(3, label("thread", getThreadName(event.tid)));
        }
        if (args.classify && stackTrace != null) {
            s.field(3, label("category", getCategory(stackTrace).title));
        }

        return s;
    }

    private Proto valueType(String type, String unit) {
        return new Proto(PROTO_BUFFER_SIZE)
                .field(1, strings.index(type))
                .field(2, strings.index(unit));
    }

    private Proto label(String key, String str) {
        return new Proto(PROTO_BUFFER_SIZE)
                .field(1, strings.index(key))
                .field(2, strings.index(str));
    }

    private Proto mapping(int id, long start, long limit, String fileName) {
        return new Proto(PROTO_BUFFER_SIZE)
                .field(1, id)
                .field(2, start)
                .field(3, limit)
                .field(5, strings.index(fileName));
    }

    private Proto location(int id, long location) {
        return new Proto(PROTO_BUFFER_SIZE)
                .field(1, id)
                .field(4, line((int) (location >>> 16), (int) location & 0xffff));
    }

    private Proto line(int functionId, int line) {
        return new Proto(PROTO_BUFFER_SIZE)
                .field(1, functionId)
                .field(2, line);
    }

    private Proto function(int id, String name) {
        return new Proto(PROTO_BUFFER_SIZE)
                .field(1, id)
                .field(2, strings.index(name));
    }

    public static void convert(String input, String output, Arguments args) throws IOException {
        // 优化：减少资源打开和关闭次数
        try (JfrReader jfr = new JfrReader(input);
             FileOutputStream fos = new FileOutputStream(output);
             OutputStream out = args.output.endsWith("gz") ? new GZIPOutputStream(fos, GZIP_BUFFER_SIZE) : fos) {
            JfrToPprof converter = new JfrToPprof(jfr, args);
            converter.convert();
            converter.dump(out);
        }
    }
}
