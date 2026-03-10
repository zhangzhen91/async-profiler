/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "allocTracer.h"
#include "profiler.h"
#include "stackFrame.h"
#include "tsc.h"
#include "vmStructs.h"
#include <cstdint>

// Static assertions for type safety
static_assert(sizeof(u64) == 8, "u64 must be 64-bit");
static_assert(sizeof(uintptr_t) >= sizeof(void*), "uintptr_t must be pointer-sized");
static_assert(sizeof(uintptr_t) <= sizeof(u64), "uintptr_t should not exceed u64 size");


int AllocTracer::_trap_kind = 0;
Trap AllocTracer::_in_new_tlab(0);
Trap AllocTracer::_outside_tlab(1);

u64 AllocTracer::_interval = 0;
volatile u64 AllocTracer::_allocated_bytes = 0;


/**
 * Handler for allocation breakpoint traps.
 * This function is called when JVM hits one of our installed breakpoints
 * for memory allocation monitoring.
 *
 * @param signo Signal number
 * @param siginfo Signal information
 * @param ucontext User context containing CPU registers
 */
void AllocTracer::trapHandler(int signo, siginfo_t* siginfo, void* ucontext) {
    // Fast path: quick check if tracer is enabled
    if (!_enabled) {
        Profiler::instance()->trapHandler(signo, siginfo, ucontext);
        return;
    }

    // Create stack frame from user context to access function arguments
    StackFrame frame(ucontext);
    
    // Pre-declare variables to reduce branching
    EventType event_type;
    uintptr_t total_size;
    uintptr_t instance_size = 0;
    uintptr_t klass;

    // Determine which allocation trap was triggered based on program counter
    const uintptr_t pc = frame.pc();
    if (_in_new_tlab.covers(pc)) {
        // Allocation in new TLAB (Thread Local Allocation Buffer)
        event_type = ALLOC_SAMPLE;
        
        // Extract arguments based on JDK version (branch prediction friendly)
        if (_trap_kind == 1) {
            total_size = frame.arg2();     // tlab_size
            instance_size = frame.arg3();  // alloc_size
            klass = frame.arg0();          // klass
        } else {
            total_size = frame.arg1();     // tlab_size
            instance_size = frame.arg2();  // alloc_size
            klass = frame.arg0();          // klass
        }
    } else if (_outside_tlab.covers(pc)) {
        // Allocation outside TLAB
        event_type = ALLOC_OUTSIDE_TLAB;
        
        // Extract allocation size based on JDK version
        if (_trap_kind == 1) {
            total_size = frame.arg2();     // alloc_size
            klass = frame.arg0();          // klass
        } else {
            total_size = frame.arg1();     // alloc_size
            klass = frame.arg0();          // klass
        }
    } else {
        // Not our trap - delegate to profiler's handler
        Profiler::instance()->trapHandler(signo, siginfo, ucontext);
        return;
    }

    // Leave the trapped function by simulating "ret" instruction
    frame.ret();

    // Check sampling interval before recording (fast path optimization)
    if (updateCounter(_allocated_bytes, total_size, _interval)) {
        recordAllocation(ucontext, event_type, klass, total_size, instance_size);
    }
}

/**
 * Records a memory allocation event.
 *
 * @param ucontext User context containing CPU registers at allocation time
 * @param event_type Type of allocation event (ALLOC_SAMPLE or ALLOC_OUTSIDE_TLAB)
 * @param rklass Raw class handle from JVM
 * @param total_size Total memory allocated (including overhead)
 * @param instance_size Size of the actual object instance
 */
void AllocTracer::recordAllocation(void* ucontext, EventType event_type, uintptr_t rklass,
                                   uintptr_t total_size, uintptr_t instance_size) {
    // Create allocation event structure on stack (no heap allocation)
    AllocEvent event;
    event._start_time = TSC::ticks();  // Capture high-resolution timestamp
    event._class_id = 0;               // Initialize class ID to 0
    event._total_size = total_size;    // Store total allocation size
    event._instance_size = instance_size;  // Store object instance size

    // Fast path: try to resolve class name if VM structures are available
    if (VMStructs::hasClassNames() && rklass != 0) {
        // Get class name from JVM internal structures
        VMKlass* klass = VMKlass::fromHandle(rklass);
        if (klass != nullptr) {
            VMSymbol* symbol = klass->name();
            if (symbol != nullptr && symbol->body() != nullptr) {
                // Look up or create class ID in profiler's class map
                event._class_id = Profiler::instance()->classMap()->lookup(symbol->body(), symbol->length());
            }
        }
    }

    // Record the sample in profiler's event buffer
    Profiler::instance()->recordSample(ucontext, total_size, event_type, &event);
}

/**
 * Validates arguments and checks for required JVM symbols.
 * This function determines the appropriate trap kind based on available JVM symbols.
 *
 * @param args Command line arguments
 * @return Error::OK if successful, or an error message
 */
Error AllocTracer::check(Arguments& args) {
    // Validate 'live' option compatibility
    if (args._live && !args._all) {
        // This engine is only selected in Profiler::selectAllocEngine
        // when can_generate_sampled_object_alloc_events is not available (JDK<11)
        return Error("'live' option is supported on OpenJDK 11+");
    }

    // Return early if symbols are already resolved
    if (_in_new_tlab.entry() != 0 && _outside_tlab.entry() != 0) {
        return Error::OK;
    }

    // Get reference to JVM code cache
    CodeCache* libjvm = VMStructs::libjvm();
    if (libjvm == nullptr) {
        return Error("Failed to access JVM code cache");
    }

    const void* ne = nullptr;  // New TLAB allocation symbol
    const void* oe = nullptr;  // Outside TLAB allocation symbol

    // Try different symbol patterns for different JDK versions
    // Pattern 1: JDK 10+ symbols
    if ((ne = libjvm->findSymbolByPrefix("_ZN11AllocTracer27send_allocation_in_new_tlab")) != nullptr &&
        (oe = libjvm->findSymbolByPrefix("_ZN11AllocTracer28send_allocation_outside_tlab")) != nullptr) {
        _trap_kind = 1;  // JDK 10+
    }
    // Pattern 2: JDK 8u262+ symbols with HeapWord parameter
    else if ((ne = libjvm->findSymbolByPrefix("_ZN11AllocTracer33send_allocation_in_new_tlab_eventE11KlassHandleP8HeapWord")) != nullptr &&
             (oe = libjvm->findSymbolByPrefix("_ZN11AllocTracer34send_allocation_outside_tlab_eventE11KlassHandleP8HeapWord")) != nullptr) {
        _trap_kind = 1;  // JDK 8u262+
    }
    // Pattern 3: JDK 7-9 symbols without HeapWord parameter
    else if ((ne = libjvm->findSymbolByPrefix("_ZN11AllocTracer33send_allocation_in_new_tlab_event")) != nullptr &&
             (oe = libjvm->findSymbolByPrefix("_ZN11AllocTracer34send_allocation_outside_tlab_event")) != nullptr) {
        _trap_kind = 2;  // JDK 7-9
    }
    // No matching symbols found
    else {
        return Error("No AllocTracer symbols found. Are JDK debug symbols installed?");
    }

    // Verify that both symbols were found
    if (ne == nullptr || oe == nullptr) {
        return Error("Failed to resolve allocation symbols");
    }

    // Assign symbols to traps and pair them
    _in_new_tlab.assign(ne);
    _outside_tlab.assign(oe);
    _in_new_tlab.pair(_outside_tlab);

    return Error::OK;
}

/**
 * Starts the allocation tracer with specified arguments.
 *
 * @param args Command line arguments containing allocation sampling interval
 * @return Error::OK if successful, or an error message
 */
Error AllocTracer::start(Arguments& args) {
    // Validate arguments and resolve JVM symbols
    Error error = check(args);
    if (error) {
        return error;
    }

    // Set sampling interval (0 means no sampling, record all allocations)
    _interval = args._alloc > 0 ? args._alloc : 0;
    _allocated_bytes = 0;  // Reset allocation counter

    // Install breakpoints for allocation monitoring
    if (!_in_new_tlab.install()) {
        return Error("Failed to install allocation breakpoint for new TLAB");
    }
    
    if (!_outside_tlab.install()) {
        // Rollback first installation if second fails
        _in_new_tlab.uninstall();
        return Error("Failed to install allocation breakpoint for outside TLAB");
    }

    return Error::OK;
}

/**
 * Stops the allocation tracer and removes breakpoints.
 * This function safely uninstalls both allocation breakpoints.
 */
void AllocTracer::stop() {
    // Uninstall breakpoints in reverse order of installation
    _outside_tlab.uninstall();
    _in_new_tlab.uninstall();
}
