/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.convert;

import java.util.HashMap;

public class Frame extends HashMap<Integer, Frame> {
    public static final byte TYPE_INTERPRETED = 0;
    public static final byte TYPE_JIT_COMPILED = 1;
    public static final byte TYPE_INLINED = 2;
    public static final byte TYPE_NATIVE = 3;
    public static final byte TYPE_CPP = 4;
    public static final byte TYPE_KERNEL = 5;
    public static final byte TYPE_C1_COMPILED = 6;

    private static final int TYPE_SHIFT = 28;
    private static final int TITLE_INDEX_MASK = (1 << TYPE_SHIFT) - 1;
    private static final int MAX_DEPTH_CACHE_SIZE = 1000; // 限制深度缓存大小

    final int key;
    long total;
    long self;
    long inlined, c1, interpreted;
    private int cachedDepth = -1; // 缓存计算过的深度值

    private Frame(int key) {
        this.key = key;
    }

    Frame(int titleIndex, byte type) {
        this(titleIndex | type << TYPE_SHIFT);
    }

    Frame getChild(int titleIndex, byte type) {
        // 优化：使用位运算创建key，避免重复计算
        int childKey = titleIndex | type << TYPE_SHIFT;
        return super.computeIfAbsent(childKey, Frame::new);
    }

    int getTitleIndex() {
        // 优化：使用预计算的掩码，提高位运算效率
        return key & TITLE_INDEX_MASK;
    }

    byte getType() {
        // 优化：减少重复计算，提前返回条件
        if (inlined * 3 >= total) {
            return TYPE_INLINED;
        }
        if (c1 * 2 >= total) {
            return TYPE_C1_COMPILED;
        }
        if (interpreted * 2 >= total) {
            return TYPE_INTERPRETED;
        }
        return (byte) (key >>> TYPE_SHIFT);
    }

    int depth(long cutoff) {
        // 优化：使用缓存避免重复计算
        if (cachedDepth >= 0 && size() <= MAX_DEPTH_CACHE_SIZE) {
            return cachedDepth;
        }
        
        int depth = 0;
        if (!isEmpty()) {
            // 优化：直接遍历entrySet，避免values()的额外开销
            for (Entry<Integer, Frame> entry : entrySet()) {
                Frame child = entry.getValue();
                if (child.total >= cutoff) {
                    depth = Math.max(depth, child.depth(cutoff));
                }
            }
        }
        
        int result = depth + 1;
        // 只对小树缓存结果，避免内存过度使用
        if (size() <= MAX_DEPTH_CACHE_SIZE) {
            cachedDepth = result;
        }
        return result;
    }
    
    /**
     * 重置深度缓存，当帧数据发生变化时调用
     */
    void invalidateDepthCache() {
        cachedDepth = -1;
    }
    
    /**
     * 获取原始类型（不考虑运行时统计信息）
     */
    byte getRawType() {
        return (byte) (key >>> TYPE_SHIFT);
    }
}
