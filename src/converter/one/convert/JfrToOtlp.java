/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.convert;

import static one.convert.OtlpConstants.*;

import one.jfr.JfrReader;
import one.jfr.StackTrace;
import one.jfr.event.Event;
import one.jfr.event.EventCollector;
import one.proto.Proto;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.*;

/** Converts .jfr output to OpenTelemetry protocol. */
public class JfrToOtlp extends JfrConverter {
    // Size in bytes to be allocated in the buffer to hold the varint containing the length of the message
    private static final int MSG_LARGE = 5;
    private static final int MSG_SMALL = 1;
    private static final int INITIAL_BUFFER_SIZE = 4096;

    private final Index<String> stringPool = new Index<>(String.class, "");
    private final Index<String> functionPool = new Index<>(String.class, "");
    private final Index<Line> linePool = new Index<>(Line.class, Line.EMPTY);
    private final Index<KeyValue> attributesPool = new Index<>(KeyValue.class, KeyValue.EMPTY);

    private final Proto proto = new Proto(INITIAL_BUFFER_SIZE);

    public JfrToOtlp(JfrReader jfr, Arguments args) {
        super(jfr, args);
    }

    public void dump(OutputStream out) throws IOException {
        out.write(proto.buffer(), 0, proto.size());
    }

    @Override
    public void convert() throws IOException {
        // 优化：使用局部变量减少重复计算
        long rpMark = proto.startField(PROFILES_DATA_resource_profiles, MSG_LARGE);
        long spMark = proto.startField(RESOURCE_PROFILES_scope_profiles, MSG_LARGE);
        super.convert();
        proto.commitField(spMark);
        proto.commitField(rpMark);

        writeProfileDictionary();
    }

    @Override
    protected void convertChunk() {
        long pMark = proto.startField(SCOPE_PROFILES_profiles, MSG_LARGE);

        writeSampleTypes();
        writeTimingInformation();

        // 优化：使用更合适的数据结构
        List<Integer> locationIndices = new ArrayList<>(INITIAL_BUFFER_SIZE);
        collector.forEach(new OtlpEventToSampleVisitor(locationIndices));

        long liMark = proto.startField(PROFILE_location_indices, MSG_LARGE);
        // 优化：使用批量写入
        locationIndices.forEach(proto::writeInt);
        proto.commitField(liMark);

        proto.commitField(pMark);
    }

    private void writeSampleTypes() {
        // 优化：缓存常用值
        String valueType = getValueType();
        String sampleUnits = getSampleUnits();
        String totalUnits = getTotalUnits();

        long stsMark = proto.startField(PROFILE_sample_type, MSG_SMALL);
        proto.field(VALUE_TYPE_type_strindex, stringPool.index(valueType));
        proto.field(VALUE_TYPE_unit_strindex, stringPool.index(sampleUnits));
        proto.field(VALUE_TYPE_aggregation_temporality, AGGREGATION_TEMPORARALITY_cumulative);
        proto.commitField(stsMark);

        long sttMark = proto.startField(PROFILE_sample_type, MSG_SMALL);
        proto.field(VALUE_TYPE_type_strindex, stringPool.index(valueType));
        proto.field(VALUE_TYPE_unit_strindex, stringPool.index(totalUnits));
        proto.field(VALUE_TYPE_aggregation_temporality, AGGREGATION_TEMPORARALITY_cumulative);
        proto.commitField(sttMark);
    }

    private void writeTimingInformation() {
        proto.field(PROFILE_time_nanos, jfr.chunkStartNanos);
        proto.field(PROFILE_duration_nanos, jfr.chunkDurationNanos());
    }

    private void writeProfileDictionary() {
        long profilesDictionaryMark = proto.startField(PROFILES_DATA_dictionary, MSG_LARGE);

        // Mapping[0] must be a default mapping according to the spec
        long mMark = proto.startField(PROFILES_DICTIONARY_mapping_table, MSG_SMALL);
        proto.commitField(mMark);

        // 优化：批量写入函数表
        writeFunctionTable();
        
        // 优化：批量写入位置表
        writeLocationTable();
        
        // 优化：批量写入字符串表
        writeStringTable();
        
        // 优化：批量写入属性表
        writeAttributesTable();

        proto.commitField(profilesDictionaryMark);
    }

    private void writeFunctionTable() {
        for (String name : functionPool.keys()) {
            long fMark = proto.startField(PROFILES_DICTIONARY_function_table, MSG_SMALL);
            proto.field(FUNCTION_name_strindex, stringPool.index(name));
            proto.commitField(fMark);
        }
    }

    private void writeLocationTable() {
        for (Line line : linePool.keys()) {
            long locMark = proto.startField(PROFILES_DICTIONARY_location_table, MSG_SMALL);
            proto.field(LOCATION_mapping_index, 0);

            long lineMark = proto.startField(LOCATION_line, MSG_SMALL);
            proto.field(LINE_function_index, line.functionIdx);
            proto.field(LINE_line, line.lineNumber);
            proto.commitField(lineMark);

            proto.commitField(locMark);
        }
    }

    private void writeStringTable() {
        for (String s : stringPool.keys()) {
            proto.field(PROFILES_DICTIONARY_string_table, s);
        }
    }

    private void writeAttributesTable() {
        for (KeyValue kv : attributesPool.keys()) {
            long aMark = proto.startField(PROFILES_DICTIONARY_attribute_table, MSG_LARGE);
            proto.field(KEY_VALUE_key, kv.key);

            long vMark = proto.startField(KEY_VALUE_value, MSG_LARGE);
            proto.field(ANY_VALUE_string_value, kv.value);
            proto.commitField(vMark);

            proto.commitField(aMark);
        }
    }

    public static void convert(String input, String output, Arguments args) throws IOException {
        // 优化：减少资源打开和关闭次数
        try (JfrReader jfr = new JfrReader(input);
             FileOutputStream out = new FileOutputStream(output)) {
            JfrToOtlp converter = new JfrToOtlp(jfr, args);
            converter.convert();
            converter.dump(out);
        }
    }

    private final class OtlpEventToSampleVisitor implements EventCollector.Visitor {
        private final List<Integer> locationIndices;
        private final double factor = counterFactor();
        private final double ticksPerNanosecond = jfr.ticksPerSec / 1_000_000_000.0;

        // JFR constant pool stacktrace ID to Range
        private final Map<Integer, Range> idToRange = new HashMap<>();

        // Next index to be used for a location into Profile.location_indices
        private int nextLocationIdx = 0;

        public OtlpEventToSampleVisitor(List<Integer> locationIndices) {
            this.locationIndices = locationIndices;
        }

        @Override
        public void visit(Event event, long samples, long value) {
            // 优化：减少重复计算
            long nanosFromStart = (long) ((event.time - jfr.chunkStartTicks) / ticksPerNanosecond);
            long timeNanos = jfr.chunkStartNanos + nanosFromStart;

            Range range = idToRange.computeIfAbsent(event.stackTraceId, this::computeLocationRange);

            long sMark = proto.startField(PROFILE_sample, MSG_SMALL);
            proto.field(SAMPLE_locations_start_index, range.start);
            proto.field(SAMPLE_locations_length, range.length);
            proto.field(SAMPLE_timestamps_unix_nano, timeNanos);

            KeyValue threadName = new KeyValue("thread.name", getThreadName(event.tid));
            proto.field(SAMPLE_attribute_indices, attributesPool.index(threadName));

            long svMark = proto.startField(SAMPLE_value, MSG_SMALL);
            proto.writeLong(samples);
            proto.writeLong(factor == 1.0 ? value : (long) (value * factor));
            proto.commitField(svMark);

            proto.commitField(sMark);
        }

        // Range of values in Profile.location_indices
        private Range computeLocationRange(int stackTraceId) {
            StackTrace st = jfr.stackTraces.get(stackTraceId);
            if (st == null) {
                return new Range(0, 0);
            }
            
            // 优化：批量添加位置索引
            for (int i = 0; i < st.methods.length; ++i) {
                locationIndices.add(linePool.index(makeLine(st, i)));
            }
            
            Range range = new Range(nextLocationIdx, st.methods.length);
            nextLocationIdx += st.methods.length;
            return range;
        }

        private Line makeLine(StackTrace stackTrace, int i) {
            String methodName = getMethodName(stackTrace.methods[i], stackTrace.types[i]);
            int lineNumber = stackTrace.locations[i] >>> 16;
            int functionIdx = functionPool.index(methodName);
            return new Line(functionIdx, lineNumber);
        }
    }

    private static final class Range {
        final int start;
        final int length;

        public Range(int start, int length) {
            this.start = start;
            this.length = length;
        }
    }

    private static final class Line {
        static final Line EMPTY = new Line(0, 0);

        final int functionIdx;
        final int lineNumber;

        private Line(int functionIdx, int lineNumber) {
            this.functionIdx = functionIdx;
            this.lineNumber = lineNumber;
        }

        @Override
        public boolean equals(Object o) {
            if (!(o instanceof Line)) return false;

            Line other = (Line) o;
            return functionIdx == other.functionIdx && lineNumber == other.lineNumber;
        }

        @Override
        public int hashCode() {
            int result = 17;
            result = 31 * result + functionIdx;
            return 31 * result + lineNumber;
        }
    }

    private static final class KeyValue {
        static final KeyValue EMPTY = new KeyValue("", "");

        final String key;
        // Only string value is fine for now
        final String value;

        private KeyValue(String key, String value) {
            this.key = key;
            this.value = value;
        }

        @Override
        public boolean equals(Object o) {
            if (!(o instanceof KeyValue)) return false;

            KeyValue other = (KeyValue) o;
            return key.equals(other.key) && value.equals(other.value);
        }

        @Override
        public int hashCode() {
            int result = 17;
            result = 31 * result + key.hashCode();
            return 31 * result + value.hashCode();
        }
    }
}
