/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dlfcn.h>
#include <stdlib.h>
#include "hooks.h"
#include "profiler.h"
#include "vmStructs.h"


// This should be called only after all other statics are initialized.
// Therefore, put it in the last file in the alphabetic order.
class LateInitializer {
  public:
    LateInitializer() {
        try {
            initialize();
        } catch (...) {
            // Ensure no exceptions escape from static initialization
            Log::error("Exception during async-profiler initialization");
        }
    }

  private:
    void initialize() {
        // Prevent DSO unloading on non-musl systems
        if (!OS::isMusl()) {
            preventDsoUnloading();
        }

        // Check if JVM is already loaded and initialize accordingly
        if (!checkJvmLoaded()) {
            const char* command = getenv("ASPROF_COMMAND");
            if (command != nullptr && OS::checkPreloaded() && Hooks::init(false)) {
                startProfiler(command);
            }
        }
    }

    void preventDsoUnloading() {
        Dl_info dl_info;
        if (dladdr(reinterpret_cast<const void*>(Hooks::init), &dl_info) &&
            dl_info.dli_fname != nullptr) {
            // Make sure async-profiler DSO cannot be unloaded, since it contains JVM callbacks.
            // This is not relevant for musl, where dlclose() is no-op.
            // Can't use ELF NODELETE flag because of https://sourceware.org/bugzilla/show_bug.cgi?id=20839
            void* handle = dlopen(dl_info.dli_fname, RTLD_LAZY | RTLD_NODELETE);
            if (handle == nullptr) {
                Log::debug("Failed to prevent DSO unloading: %s", dlerror());
            }
        }
    }

    bool checkJvmLoaded() {
        Profiler* profiler = Profiler::instance();
        if (profiler == nullptr) {
            return false;
        }

        profiler->updateSymbols(false);

        const char* jvm_lib_name = OS::isLinux() ? "libjvm.so" : "libjvm.dylib";
        CodeCache* libjvm = profiler->findLibraryByName(jvm_lib_name);
        if (libjvm == nullptr) {
            return false;
        }

        if (libjvm->findSymbol("AsyncGetCallTrace") == nullptr) {
            return false;
        }

        VMStructs::init(libjvm);

        // heap is already created => this is dynamic attach
        if (CollectedHeap::created()) {
            JVMFlag* flag = JVMFlag::find("EnableDynamicAgentLoading");
            if (flag != nullptr && flag->isDefault()) {
                flag->setCmdline();
            }
        }

        return true;
    }

    void startProfiler(const char* command) {
        if (command == nullptr) {
            Log::error("ASPROF_COMMAND is null");
            return;
        }

        Error error = _global_args.parse(command);
        _global_args._preloaded = true;

        Log::open(_global_args);

        if (error) {
            Log::error("Failed to parse profiler arguments: %s", error.message());
            return;
        }

        error = Profiler::instance()->run(_global_args);
        if (error) {
            Log::error("Failed to start profiler: %s", error.message());
        }
    }
};

// Global static initializer - ensures initialization happens after all other statics
static LateInitializer _late_initializer;
