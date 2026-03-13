/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>
#include <unistd.h>
#include "vmStructs.h"
#include "vmEntry.h"
#include "j9Ext.h"
#include "safeAccess.h"


CodeCache* VMStructs::_libjvm = NULL;

bool VMStructs::_has_class_names = false;
bool VMStructs::_has_method_structs = false;
bool VMStructs::_has_compiler_structs = false;
bool VMStructs::_has_stack_structs = false;
bool VMStructs::_has_class_loader_data = false;
bool VMStructs::_has_native_thread_id = false;
bool VMStructs::_has_perm_gen = false;
bool VMStructs::_can_dereference_jmethod_id = false;
bool VMStructs::_compact_object_headers = false;

int VMStructs::_klass_name_offset = -1;
int VMStructs::_symbol_length_offset = -1;
int VMStructs::_symbol_length_and_refcount_offset = -1;
int VMStructs::_symbol_body_offset = -1;
int VMStructs::_oop_klass_offset = -1;
int VMStructs::_class_loader_data_offset = -1;
int VMStructs::_class_loader_data_next_offset = -1;
int VMStructs::_methods_offset = -1;
int VMStructs::_jmethod_ids_offset = -1;
int VMStructs::_thread_osthread_offset = -1;
int VMStructs::_thread_anchor_offset = -1;
int VMStructs::_thread_state_offset = -1;
int VMStructs::_thread_vframe_offset = -1;
int VMStructs::_thread_exception_offset = -1;
int VMStructs::_osthread_id_offset = -1;
int VMStructs::_call_wrapper_anchor_offset = -1;
int VMStructs::_comp_env_offset = -1;
int VMStructs::_comp_task_offset = -1;
int VMStructs::_comp_method_offset = -1;
int VMStructs::_anchor_sp_offset = -1;
int VMStructs::_anchor_pc_offset = -1;
int VMStructs::_anchor_fp_offset = -1;
int VMStructs::_blob_size_offset = -1;
int VMStructs::_frame_size_offset = -1;
int VMStructs::_frame_complete_offset = -1;
int VMStructs::_code_offset = -1;
int VMStructs::_data_offset = -1;
int VMStructs::_mutable_data_offset = -1;
int VMStructs::_relocation_size_offset = -1;
int VMStructs::_scopes_pcs_offset = -1;
int VMStructs::_scopes_data_offset = -1;
int VMStructs::_nmethod_name_offset = -1;
int VMStructs::_nmethod_method_offset = -1;
int VMStructs::_nmethod_entry_offset = -1;
int VMStructs::_nmethod_state_offset = -1;
int VMStructs::_nmethod_level_offset = -1;
int VMStructs::_nmethod_metadata_offset = -1;
int VMStructs::_nmethod_immutable_offset = -1;
int VMStructs::_method_constmethod_offset = -1;
int VMStructs::_method_code_offset = -1;
int VMStructs::_constmethod_constants_offset = -1;
int VMStructs::_constmethod_idnum_offset = -1;
int VMStructs::_constmethod_size = -1;
int VMStructs::_pool_holder_offset = -1;
int VMStructs::_array_len_offset = 0;
int VMStructs::_array_data_offset = -1;
int VMStructs::_code_heap_memory_offset = -1;
int VMStructs::_code_heap_segmap_offset = -1;
int VMStructs::_code_heap_segment_shift = -1;
int VMStructs::_heap_block_used_offset = -1;
int VMStructs::_vs_low_bound_offset = -1;
int VMStructs::_vs_high_bound_offset = -1;
int VMStructs::_vs_low_offset = -1;
int VMStructs::_vs_high_offset = -1;
int VMStructs::_flag_name_offset = -1;
int VMStructs::_flag_addr_offset = -1;
int VMStructs::_flag_origin_offset = -1;
const char* VMStructs::_flags_addr = NULL;
int VMStructs::_flag_count = 0;
int VMStructs::_flag_size = 0;
char* VMStructs::_code_heap[3] = {};
const void* VMStructs::_code_heap_low = NO_MIN_ADDRESS;
const void* VMStructs::_code_heap_high = NO_MAX_ADDRESS;
char** VMStructs::_code_heap_addr = NULL;
const void** VMStructs::_code_heap_low_addr = NULL;
const void** VMStructs::_code_heap_high_addr = NULL;
int* VMStructs::_klass_offset_addr = NULL;
char** VMStructs::_narrow_klass_base_addr = NULL;
char* VMStructs::_narrow_klass_base = NULL;
int* VMStructs::_narrow_klass_shift_addr = NULL;
int VMStructs::_narrow_klass_shift = -1;
char** VMStructs::_collected_heap_addr = NULL;
char* VMStructs::_collected_heap = NULL;
int VMStructs::_collected_heap_reserved_offset = -1;
int VMStructs::_region_start_offset = -1;
int VMStructs::_region_size_offset = -1;
int VMStructs::_markword_klass_shift = -1;
int VMStructs::_markword_monitor_value = -1;
int VMStructs::_entry_frame_call_wrapper_offset = -1;
int VMStructs::_interpreter_frame_bcp_offset = 0;
unsigned char VMStructs::_unsigned5_base = 0;
const void** VMStructs::_call_stub_return_addr = NULL;
const void* VMStructs::_call_stub_return = NULL;
const void* VMStructs::_interpreted_frame_valid_start = NULL;
const void* VMStructs::_interpreted_frame_valid_end = NULL;

jfieldID VMStructs::_eetop;
jfieldID VMStructs::_tid;
jfieldID VMStructs::_klass = NULL;
int VMStructs::_tls_index = -1;
intptr_t VMStructs::_env_offset = -1;
void* VMStructs::_java_thread_vtbl[6];

VMStructs::LockFunc VMStructs::_lock_func;
VMStructs::LockFunc VMStructs::_unlock_func;


uintptr_t VMStructs::readSymbol(const char* symbol_name) {
    if (symbol_name == nullptr || _libjvm == nullptr) {
        return 0;
    }
    
    const void* symbol = _libjvm->findSymbol(symbol_name);
    if (symbol == nullptr) {
        // Avoid JVM crash in case of missing symbols
        return 0;
    }
    
    // Use SafeAccess for safe memory access
    return SafeAccess::load((uintptr_t*)symbol, static_cast<uintptr_t>(0));
}

// Run at agent load time
void VMStructs::init(CodeCache* libjvm) {
    if (libjvm != NULL) {
        _libjvm = libjvm;
        initOffsets();
        initJvmFunctions();
    }
}

// Run when VM is initialized and JNI is available
void VMStructs::ready() {
    resolveOffsets();
    patchSafeFetch();
    initThreadBridge();
}

void VMStructs::initOffsets() {
    // Use SafeAccess for safe memory access
    auto safeLoadPtr = [](uintptr_t addr) -> const char* {
        return SafeAccess::load((const char**)addr, (const char*)nullptr);
    };
    
    auto safeLoadInt = [](uintptr_t addr) -> int {
        return SafeAccess::load((int*)addr, 0);
    };
    
    auto safeLoadLong = [](uintptr_t addr) -> long {
        return SafeAccess::load((long*)addr, 0L);
    };
    
    uintptr_t entry = readSymbol("gHotSpotVMStructs");
    uintptr_t stride = readSymbol("gHotSpotVMStructEntryArrayStride");
    uintptr_t type_offset = readSymbol("gHotSpotVMStructEntryTypeNameOffset");
    uintptr_t field_offset = readSymbol("gHotSpotVMStructEntryFieldNameOffset");
    uintptr_t offset_offset = readSymbol("gHotSpotVMStructEntryOffsetOffset");
    uintptr_t address_offset = readSymbol("gHotSpotVMStructEntryAddressOffset");

    if (entry != 0 && stride != 0) {
        for (;; entry += stride) {
            const char* type = safeLoadPtr(entry + type_offset);
            const char* field = safeLoadPtr(entry + field_offset);
            if (type == nullptr || field == nullptr) {
                break;
            }

            // Use a more efficient string comparison approach
            struct FieldMapping {
                const char* type;
                const char* field;
                int* target_offset;
                bool is_address;
            };
            
            static const FieldMapping mappings[] = {
                {"Klass", "_name", &_klass_name_offset, false},
                {"Symbol", "_length", &_symbol_length_offset, false},
                {"Symbol", "_length_and_refcount", &_symbol_length_and_refcount_offset, false},
                {"Symbol", "_body", &_symbol_body_offset, false},
                {"oopDesc", "_metadata._klass", &_oop_klass_offset, false},
                {"Universe", "_narrow_klass._base", nullptr, true},
                {"Universe", "_base", nullptr, true},
                {"CompressedKlassPointers", "_narrow_klass._base", nullptr, true},
                {"CompressedKlassPointers", "_base", nullptr, true},
                {"Universe", "_narrow_klass._shift", nullptr, true},
                {"Universe", "_shift", nullptr, true},
                {"Universe", "_collectedHeap", nullptr, true},
                {"CompressedKlassPointers", "_collectedHeap", nullptr, true},
                {"CollectedHeap", "_reserved", &_collected_heap_reserved_offset, false},
                {"MemRegion", "_start", &_region_start_offset, false},
                {"MemRegion", "_word_size", &_region_size_offset, false},
                {"CompiledMethod", "_method", &_nmethod_method_offset, false},
                {"nmethod", "_method", &_nmethod_method_offset, false},
                {"CompiledMethod", "_verified_entry_offset", &_nmethod_entry_offset, false},
                {"nmethod", "_verified_entry_offset", &_nmethod_entry_offset, false},
                {"CompiledMethod", "_verified_entry_point", &_nmethod_entry_offset, false},
                {"nmethod", "_verified_entry_point", &_nmethod_entry_offset, false},
                {"CompiledMethod", "_state", &_nmethod_state_offset, false},
                {"nmethod", "_state", &_nmethod_state_offset, false},
                {"CompiledMethod", "_comp_level", &_nmethod_level_offset, false},
                {"nmethod", "_comp_level", &_nmethod_level_offset, false},
                {"CompiledMethod", "_metadata_offset", &_nmethod_metadata_offset, false},
                {"nmethod", "_metadata_offset", &_nmethod_metadata_offset, false},
                {"CompiledMethod", "_immutable_data", &_nmethod_immutable_offset, false},
                {"nmethod", "_immutable_data", &_nmethod_immutable_offset, false},
                {"CompiledMethod", "_scopes_pcs_offset", &_scopes_pcs_offset, false},
                {"nmethod", "_scopes_pcs_offset", &_scopes_pcs_offset, false},
                {"CompiledMethod", "_scopes_data_offset", &_scopes_data_offset, false},
                {"nmethod", "_scopes_data_offset", &_scopes_data_offset, false},
                {"CompiledMethod", "_scopes_data_begin", &_scopes_data_offset, false},
                {"nmethod", "_scopes_data_begin", &_scopes_data_offset, false},
                {"Method", "_constMethod", &_method_constmethod_offset, false},
                {"Method", "_code", &_method_code_offset, false},
                {"ConstMethod", "_constants", &_constmethod_constants_offset, false},
                {"ConstMethod", "_method_idnum", &_constmethod_idnum_offset, false},
                {"ConstantPool", "_pool_holder", &_pool_holder_offset, false},
                {"InstanceKlass", "_class_loader_data", &_class_loader_data_offset, false},
                {"InstanceKlass", "_methods", &_methods_offset, false},
                {"InstanceKlass", "_methods_jmethod_ids", &_jmethod_ids_offset, false},
                {"ClassLoaderData", "_next", &_class_loader_data_next_offset, false},
                {"java_lang_Class", "_klass_offset", nullptr, true},
                {"Thread", "_osthread", &_thread_osthread_offset, false},
                {"JavaThread", "_osthread", &_thread_osthread_offset, false},
                {"JavaThread", "_anchor", &_thread_anchor_offset, false},
                {"JavaThread", "_thread_state", &_thread_state_offset, false},
                {"JavaThread", "_vframe_array_head", &_thread_vframe_offset, false},
                {"ThreadShadow", "_exception_file", &_thread_exception_offset, false},
                {"OSThread", "_thread_id", &_osthread_id_offset, false},
                {"CompilerThread", "_env", &_comp_env_offset, false},
                {"ciEnv", "_task", &_comp_task_offset, false},
                {"CompileTask", "_method", &_comp_method_offset, false},
                {"JavaCallWrapper", "_anchor", &_call_wrapper_anchor_offset, false},
                {"JavaFrameAnchor", "_last_Java_sp", &_anchor_sp_offset, false},
                {"JavaFrameAnchor", "_last_Java_pc", &_anchor_pc_offset, false},
                {"JavaFrameAnchor", "_last_Java_fp", &_anchor_fp_offset, false},
                {"CodeBlob", "_size", &_blob_size_offset, false},
                {"CodeBlob", "_frame_size", &_frame_size_offset, false},
                {"CodeBlob", "_frame_complete_offset", &_frame_complete_offset, false},
                {"CodeBlob", "_code_offset", &_code_offset, false},
                {"CodeBlob", "_code_begin", &_code_offset, false},
                {"CodeBlob", "_data_offset", &_data_offset, false},
                {"CodeBlob", "_mutable_data", &_mutable_data_offset, false},
                {"CodeBlob", "_relocation_size", &_relocation_size_offset, false},
                {"CodeBlob", "_name", &_nmethod_name_offset, false},
                {"CodeCache", "_heap", nullptr, true},
                {"CodeCache", "_heaps", nullptr, true},
                {"CodeCache", "_low_bound", nullptr, true},
                {"CodeCache", "_high_bound", nullptr, true},
                {"CodeHeap", "_memory", &_code_heap_memory_offset, false},
                {"CodeHeap", "_segmap", &_code_heap_segmap_offset, false},
                {"CodeHeap", "_log2_segment_size", &_code_heap_segment_shift, false},
                {"HeapBlock::Header", "_used", &_heap_block_used_offset, false},
                {"VirtualSpace", "_low_boundary", &_vs_low_bound_offset, false},
                {"VirtualSpace", "_high_boundary", &_vs_high_bound_offset, false},
                {"VirtualSpace", "_low", &_vs_low_offset, false},
                {"VirtualSpace", "_high", &_vs_high_offset, false},
                {"StubRoutines", "_call_stub_return_address", nullptr, true},
                {"GrowableArrayBase", "_len", &_array_len_offset, false},
                {"GenericGrowableArray", "_len", &_array_len_offset, false},
                {"GrowableArray<int>", "_data", &_array_data_offset, false},
                {"JVMFlag", "_name", &_flag_name_offset, false},
                {"Flag", "name", &_flag_name_offset, false},
                {"JVMFlag", "_addr", &_flag_addr_offset, false},
                {"Flag", "addr", &_flag_addr_offset, false},
                {"JVMFlag", "_flags", &_flag_origin_offset, false},
                {"Flag", "origin", &_flag_origin_offset, false},
                {"Flag", "flags", nullptr, true},
                {"Flag", "numFlags", nullptr, true},
                {"PcDesc", nullptr, nullptr, false},
                {"PermGen", nullptr, nullptr, false}
            };
            
            // Process standard field mappings
            const char type0 = type[0];
            const char field0 = field[0];

            for (const FieldMapping& mapping : mappings) {
                if (mapping.type == nullptr || mapping.type[0] != type0 || strcmp(type, mapping.type) != 0) {
                    continue;
                }

                if (mapping.field == nullptr) {
                    if (type0 == 'P' && strcmp(type, "PermGen") == 0) {
                        _has_perm_gen = true;
                    }
                    continue;
                }

                if (mapping.field[0] != field0 || strcmp(field, mapping.field) != 0) {
                    continue;
                }

                if (mapping.is_address) {
                    // Handle address fields
                    if (strcmp(type, "Universe") == 0 || strcmp(type, "CompressedKlassPointers") == 0) {
                        if (strcmp(field, "_narrow_klass._base") == 0 || strcmp(field, "_base") == 0) {
                            _narrow_klass_base_addr = *(char***)(entry + address_offset);
                        } else if (strcmp(field, "_narrow_klass._shift") == 0 || strcmp(field, "_shift") == 0) {
                            _narrow_klass_shift_addr = *(int**)(entry + address_offset);
                        } else if (strcmp(field, "_collectedHeap") == 0) {
                            _collected_heap_addr = *(char***)(entry + address_offset);
                        }
                    } else if (strcmp(type, "CodeCache") == 0) {
                        if (strcmp(field, "_heap") == 0 || strcmp(field, "_heaps") == 0) {
                            _code_heap_addr = *(char***)(entry + address_offset);
                        } else if (strcmp(field, "_low_bound") == 0) {
                            _code_heap_low_addr = *(const void***)(entry + address_offset);
                        } else if (strcmp(field, "_high_bound") == 0) {
                            _code_heap_high_addr = *(const void***)(entry + address_offset);
                        }
                    } else if (strcmp(type, "java_lang_Class") == 0) {
                        if (strcmp(field, "_klass_offset") == 0) {
                            _klass_offset_addr = *(int**)(entry + address_offset);
                        }
                    } else if (strcmp(type, "StubRoutines") == 0) {
                        if (strcmp(field, "_call_stub_return_address") == 0) {
                            _call_stub_return_addr = *(const void***)(entry + address_offset);
                        }
                    } else if (strcmp(type, "Flag") == 0) {
                        if (strcmp(field, "flags") == 0) {
                            _flags_addr = **(char***)(entry + address_offset);
                        } else if (strcmp(field, "numFlags") == 0) {
                            _flag_count = **(int**)(entry + address_offset);
                        }
                    }
                } else if (mapping.target_offset != nullptr) {
                    // Handle offset fields
                    int value = safeLoadInt(entry + offset_offset);

                    // Special handling for negative offsets
                    if (field0 == '_' &&
                        (strcmp(field, "_verified_entry_point") == 0 ||
                         strcmp(field, "_code_begin") == 0 ||
                         strcmp(field, "_scopes_data_begin") == 0)) {
                        *mapping.target_offset = -value;
                    } else {
                        *mapping.target_offset = value;
                    }
                }

                break; // Found match, move to next entry
            }
        }
    }

    entry = readSymbol("gHotSpotVMTypes");
    stride = readSymbol("gHotSpotVMTypeEntryArrayStride");
    type_offset = readSymbol("gHotSpotVMTypeEntryTypeNameOffset");
    uintptr_t size_offset = readSymbol("gHotSpotVMTypeEntrySizeOffset");

    if (entry != 0 && stride != 0) {
        for (;; entry += stride) {
            const char* type = safeLoadPtr(entry + type_offset);
            if (type == nullptr) {
                break;
            }

            if (strcmp(type, "JVMFlag") == 0 || strcmp(type, "Flag") == 0) {
                _flag_size = safeLoadInt(entry + size_offset);
            } else if (strcmp(type, "ConstMethod") == 0) {
                _constmethod_size = safeLoadInt(entry + size_offset);
            }
        }
    }

    entry = readSymbol("gHotSpotVMLongConstants");
    stride = readSymbol("gHotSpotVMLongConstantEntryArrayStride");
    uintptr_t name_offset = readSymbol("gHotSpotVMLongConstantEntryNameOffset");
    uintptr_t value_offset = readSymbol("gHotSpotVMLongConstantEntryValueOffset");

    if (entry != 0 && stride != 0) {
        for (;; entry += stride) {
            const char* name = safeLoadPtr(entry + name_offset);
            if (name == nullptr) {
                break;
            }

            if (strncmp(name, "markWord::", 10) == 0) {
                if (strcmp(name + 10, "klass_shift") == 0) {
                    _markword_klass_shift = safeLoadLong(entry + value_offset);
                } else if (strcmp(name + 10, "monitor_value") == 0) {
                    _markword_monitor_value = safeLoadLong(entry + value_offset);
                }
            }
        }
    }

    entry = readSymbol("gHotSpotVMIntConstants");
    stride = readSymbol("gHotSpotVMIntConstantEntryArrayStride");
    name_offset = readSymbol("gHotSpotVMIntConstantEntryNameOffset");
    value_offset = readSymbol("gHotSpotVMIntConstantEntryValueOffset");

    if (entry != 0 && stride != 0) {
        for (;; entry += stride) {
            const char* name = safeLoadPtr(entry + name_offset);
            if (name == nullptr) {
                break;
            }

            if (strcmp(name, "frame::entry_frame_call_wrapper_offset") == 0) {
                _entry_frame_call_wrapper_offset = safeLoadInt(entry + value_offset) * static_cast<int>(sizeof(uintptr_t));
                break;  // remove it for reading more constants
            }
        }
    }
}

void VMStructs::resolveOffsets() {
    if (VM::isOpenJ9() || VM::isZing()) {
        return;
    }

    // Use SafeAccess for safe memory access
    auto safeLoadPtr = [](uintptr_t addr) -> uintptr_t {
        return SafeAccess::load((uintptr_t*)addr, static_cast<uintptr_t>(0));
    };
    
    auto safeLoadInt = [](uintptr_t addr) -> int {
        return SafeAccess::load((int*)addr, 0);
    };

    if (_klass_offset_addr != nullptr) {
        _klass = (jfieldID)(uintptr_t)(safeLoadInt((uintptr_t)_klass_offset_addr) << 2 | 2);
    }

    JVMFlag* ccp = JVMFlag::find("UseCompressedClassPointers");
    if (ccp != nullptr && ccp->get() && _narrow_klass_base_addr != nullptr && _narrow_klass_shift_addr != nullptr) {
        _narrow_klass_base = SafeAccess::load(_narrow_klass_base_addr, static_cast<char*>(nullptr));
        _narrow_klass_shift = SafeAccess::load(_narrow_klass_shift_addr, 0);
    }

    JVMFlag* coh = JVMFlag::find("UseCompactObjectHeaders");
    if (coh != nullptr && coh->get()) {
        _compact_object_headers = true;
    }

    // Use helper function for clearer conditional logic
    auto isValidClassNames = [&]() -> bool {
        bool klassValid = _klass_name_offset >= 0;
        bool symbolValid = (_symbol_length_offset >= 0 || _symbol_length_and_refcount_offset >= 0);
        bool bodyValid = _symbol_body_offset >= 0;
        bool compactHeaderValid = _compact_object_headers ?
            (_markword_klass_shift >= 0 && _markword_monitor_value == MONITOR_BIT) :
            (_oop_klass_offset >= 0);
        
        return klassValid && compactHeaderValid && symbolValid && bodyValid && _klass != nullptr;
    };
    
    _has_class_names = isValidClassNames();

    // Use helper function for clearer method struct validation
    auto isValidMethodStructs = [&]() -> bool {
        return _jmethod_ids_offset >= 0 &&
               _nmethod_method_offset >= 0 &&
               _nmethod_entry_offset != -1 &&
               _nmethod_state_offset >= 0 &&
               _method_constmethod_offset >= 0 &&
               _method_code_offset >= 0 &&
               _constmethod_constants_offset >= 0 &&
               _constmethod_idnum_offset >= 0 &&
               _constmethod_size >= 0 &&
               _pool_holder_offset >= 0;
    };
    
    _has_method_structs = isValidMethodStructs();

    _has_compiler_structs = _comp_env_offset >= 0 &&
                           _comp_task_offset >= 0 &&
                           _comp_method_offset >= 0;

    _has_class_loader_data = _class_loader_data_offset >= 0 &&
                            _class_loader_data_next_offset == static_cast<int>(sizeof(uintptr_t) * 8 + 8) &&
                            _methods_offset >= 0 &&
                            _klass != nullptr &&
                            _lock_func != nullptr && _unlock_func != nullptr;

#if defined(__x86_64__) || defined(__i386__)
    _interpreter_frame_bcp_offset = VM::hotspot_version() >= 11 ? -8 : VM::hotspot_version() == 8 ? -7 : 0;
#elif defined(__aarch64__)
    _interpreter_frame_bcp_offset = VM::hotspot_version() >= 11 ? -9 : VM::hotspot_version() == 8 ? -7 : 0;
    // The constant is missing on ARM, but fortunately, it has been stable for years across all JDK versions
    _entry_frame_call_wrapper_offset = -64;
#elif defined(__arm__) || defined(__thumb__)
    _interpreter_frame_bcp_offset = VM::hotspot_version() >= 11 ? -8 : 0;
    _entry_frame_call_wrapper_offset = 0;
#endif

    // JDK-8292758 has slightly changed ScopeDesc encoding
    if (VM::hotspot_version() >= 20) {
        _unsigned5_base = 1;
    }

    if (_call_stub_return_addr != nullptr) {
        _call_stub_return = SafeAccess::load(_call_stub_return_addr, static_cast<const void*>(nullptr));
    }

    // Since JDK 23, _metadata_offset is relative to _data_offset. See metadata()
    if (_nmethod_immutable_offset < 0) {
        _data_offset = 0;
    }

    // Use helper function for clearer stack struct validation
    auto isValidStackStructs = [&]() -> bool {
        return _has_method_structs &&
               _call_wrapper_anchor_offset >= 0 &&
               _entry_frame_call_wrapper_offset != -1 &&
               _interpreter_frame_bcp_offset != 0 &&
               _code_offset != -1 &&
               _data_offset >= 0 &&
               _scopes_data_offset != -1 &&
               _scopes_pcs_offset >= 0 &&
               ((_mutable_data_offset >= 0 && _relocation_size_offset >= 0) || _nmethod_metadata_offset >= 0) &&
               _thread_vframe_offset >= 0 &&
               _thread_exception_offset >= 0 &&
               _constmethod_size >= 0;
    };
    
    _has_stack_structs = isValidStackStructs();

    // Since JDK-8268406, it is no longer possible to get VMMethod* by dereferencing jmethodID
    _can_dereference_jmethod_id = _has_method_structs && VM::hotspot_version() <= 25;

    // Process code heap initialization
    if (_code_heap_addr != nullptr && _code_heap_low_addr != nullptr && _code_heap_high_addr != nullptr) {
        char* code_heaps = SafeAccess::load(_code_heap_addr, static_cast<char*>(nullptr));
        if (code_heaps != nullptr && _array_len_offset >= 0 && _array_data_offset >= 0) {
            unsigned int code_heap_count = SafeAccess::load((unsigned int*)(code_heaps + _array_len_offset), 0U);
            if (code_heap_count <= 3) {
                char* code_heap_array = SafeAccess::load((char**)(code_heaps + _array_data_offset), static_cast<char*>(nullptr));
                if (code_heap_array != nullptr) {
                    memcpy(_code_heap, code_heap_array, code_heap_count * sizeof(_code_heap[0]));
                }
            }
        }
        _code_heap_low = SafeAccess::load(_code_heap_low_addr, static_cast<const void*>(nullptr));
        _code_heap_high = SafeAccess::load(_code_heap_high_addr, static_cast<const void*>(nullptr));
    } else if (_code_heap_addr != nullptr && _code_heap_memory_offset >= 0) {
        _code_heap[0] = SafeAccess::load(_code_heap_addr, static_cast<char*>(nullptr));
        if (_code_heap[0] != nullptr && _vs_low_bound_offset >= 0 && _vs_high_bound_offset >= 0) {
            _code_heap_low = SafeAccess::load((const void**)(_code_heap[0] + _code_heap_memory_offset + _vs_low_bound_offset),
                                              static_cast<const void*>(nullptr));
            _code_heap_high = SafeAccess::load((const void**)(_code_heap[0] + _code_heap_memory_offset + _vs_high_bound_offset),
                                               static_cast<const void*>(nullptr));
        }
    }

    // Invariant: _code_heap[i] != nullptr iff all CodeHeap structures are available
    if (_code_heap[0] != nullptr && _code_heap_segment_shift >= 0) {
        _code_heap_segment_shift = SafeAccess::load((int*)(_code_heap[0] + _code_heap_segment_shift), _code_heap_segment_shift);
    }
    
    // Validate code heap configuration
    bool isValidCodeHeapConfig = _code_heap_memory_offset >= 0 &&
                                _code_heap_segmap_offset >= 0 &&
                                _code_heap_segment_shift >= 0 &&
                                _code_heap_segment_shift <= 16 &&
                                _heap_block_used_offset >= 0;
    
    if (!isValidCodeHeapConfig) {
        memset(_code_heap, 0, sizeof(_code_heap));
    }

    if (_collected_heap_addr != nullptr && _collected_heap_reserved_offset >= 0 &&
        _region_start_offset >= 0 && _region_size_offset >= 0) {
        char* heap_addr = SafeAccess::load(_collected_heap_addr, static_cast<char*>(nullptr));
        if (heap_addr != nullptr) {
            _collected_heap = heap_addr + _collected_heap_reserved_offset;
        }
    }
}

void VMStructs::initJvmFunctions() {
    if (VM::hotspot_version() == 8) {
        _lock_func = (LockFunc)_libjvm->findSymbol("_ZN7Monitor28lock_without_safepoint_checkEv");
        _unlock_func = (LockFunc)_libjvm->findSymbol("_ZN7Monitor6unlockEv");
    }

    if (VM::hotspot_version() > 0) {
        CodeBlob* blob = _libjvm->findBlob("_ZNK5frame26is_interpreted_frame_validEP10JavaThread");
        if (blob != NULL) {
            _interpreted_frame_valid_start = blob->_start;
            _interpreted_frame_valid_end = blob->_end;
        }
    }
}

void VMStructs::patchSafeFetch() {
    if (_libjvm == nullptr) {
        return;
    }
    
    // Workarounds for JDK-8307549 and JDK-8321116
    if (WX_MEMORY && VM::hotspot_version() == 17) {
        void** entry = (void**)_libjvm->findSymbol("_ZN12StubRoutines18_safefetch32_entryE");
        if (entry != nullptr) {
            *entry = (void*)SafeAccess::load32;
        }
    } else if (WX_MEMORY && VM::hotspot_version() == 11) {
        void** entry = (void**)_libjvm->findSymbol("_ZN12StubRoutines17_safefetchN_entryE");
        if (entry != nullptr) {
            *entry = (void*)SafeAccess::load;
        }
    }
}

void VMStructs::initTLS(void* vm_thread) {
    if (vm_thread == nullptr) {
        return;
    }
    
    // Use binary search for better performance
    int low = 0;
    int high = 1023;
    
    while (low <= high) {
        int mid = low + (high - low) / 2;
        void* value = pthread_getspecific((pthread_key_t)mid);
        
        if (value == vm_thread) {
            _tls_index = mid;
            return;
        } else if (value < vm_thread) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    
    // Fallback to linear search if binary search fails
    for (int i = 0; i < 1024; i++) {
        if (pthread_getspecific((pthread_key_t)i) == vm_thread) {
            _tls_index = i;
            break;
        }
    }
}

void VMStructs::initThreadBridge() {
    jthread thread = nullptr;
    if (VM::jvmti() == nullptr || VM::jvmti()->GetCurrentThread(&thread) != 0) {
        return;
    }

    JNIEnv* env = VM::jni();
    if (env == nullptr) {
        return;
    }
    
    jclass thread_class = env->FindClass("java/lang/Thread");
    if (thread_class == nullptr) {
        env->ExceptionClear();
        return;
    }
    
    _tid = env->GetFieldID(thread_class, "tid", "J");
    if (_tid == nullptr) {
        env->ExceptionClear();
        return;
    }

    if (VM::isOpenJ9()) {
        void* j9thread = J9Ext::j9thread_self();
        if (j9thread != nullptr) {
            initTLS(j9thread);
        }
    } else {
        // Get eetop field - a bridge from Java Thread to VMThread
        _eetop = env->GetFieldID(thread_class, "eetop", "J");
        if (_eetop == nullptr) {
            // No such field - probably not a HotSpot JVM
            env->ExceptionClear();
            return;
        }

        VMThread* vm_thread = VMThread::fromJavaThread(env, thread);
        if (vm_thread != nullptr) {
            _has_native_thread_id = _thread_osthread_offset >= 0 && _osthread_id_offset >= 0;
            initTLS(vm_thread);
            _env_offset = reinterpret_cast<intptr_t>(env) - reinterpret_cast<intptr_t>(vm_thread);
            // Use SafeAccess for vtable copy
            const void** vtable = vm_thread->vtable();
            if (vtable != nullptr) {
                memcpy(_java_thread_vtbl, vtable, sizeof(_java_thread_vtbl));
            }
        }
    }
}

VMThread* VMThread::current() {
    return _tls_index >= 0 ? static_cast<VMThread*>(pthread_getspecific(static_cast<pthread_key_t>(_tls_index))) : nullptr;
}

int VMThread::nativeThreadId(JNIEnv* jni, jthread thread) {
    if (jni == nullptr || thread == nullptr) {
        return -1;
    }
    
    if (_has_native_thread_id) {
        VMThread* vm_thread = fromJavaThread(jni, thread);
        return vm_thread != nullptr ? vm_thread->osThreadId() : -1;
    }
    return VM::isOpenJ9() ? J9Ext::GetOSThreadID(thread) : -1;
}

int VMThread::osThreadId() {
    if (_thread_osthread_offset < 0 || _osthread_id_offset < 0) {
        return -1;
    }
    
    const char* osthread = SafeAccess::load((const char**) at(_thread_osthread_offset), static_cast<const char*>(nullptr));
    if (osthread != nullptr && goodPtr(osthread)) {
        // Java thread may be in the middle of termination, and its osthread structure just released
        return SafeAccess::load32((int32_t*)(osthread + _osthread_id_offset), -1);
    }
    return -1;
}

JNIEnv* VMThread::jni() {
    if (_env_offset < 0) {
        return VM::jni();  // fallback for non-HotSpot JVM
    }
    return isJavaThread() ? SafeAccess::load((JNIEnv**) at(_env_offset), static_cast<JNIEnv*>(nullptr)) : nullptr;
}

jmethodID VMMethod::id() {
    // We may find a bogus NMethod during stack walking, it does not always point to a valid VMMethod
    if (_method_constmethod_offset < 0 || _constmethod_constants_offset < 0 || _constmethod_idnum_offset < 0 || _pool_holder_offset < 0) {
        return nullptr;
    }
    
    const char* const_method = static_cast<const char*>(SafeAccess::load((void**) at(_method_constmethod_offset), nullptr));
    if (!goodPtr(const_method)) {
        return nullptr;
    }

    const char* cpool = SafeAccess::load((const char**) (const_method + _constmethod_constants_offset), static_cast<const char*>(nullptr));
    unsigned short num = SafeAccess::load((unsigned short*) (const_method + _constmethod_idnum_offset), static_cast<unsigned short>(0));
    
    if (goodPtr(cpool)) {
        VMKlass* holder = SafeAccess::load((VMKlass**)(cpool + _pool_holder_offset), static_cast<VMKlass*>(nullptr));
        if (goodPtr(holder)) {
            jmethodID* ids = holder->jmethodIDs();
            if (ids != nullptr && num < static_cast<size_t>(ids[0])) {
                return SafeAccess::load(ids + num + 1, static_cast<jmethodID>(nullptr));
            }
        }
    }
    return nullptr;
}

jmethodID VMMethod::validatedId() {
    jmethodID method_id = id();
    if (method_id == nullptr) {
        return nullptr;
    }
    
    if (!_can_dereference_jmethod_id || (goodPtr(method_id) && SafeAccess::load((VMMethod**)method_id, static_cast<VMMethod*>(nullptr)) == this)) {
        return method_id;
    }
    return nullptr;
}

NMethod* CodeHeap::findNMethod(char* heap, const void* pc) {
    if (heap == nullptr || pc == nullptr ||
        _code_heap_memory_offset < 0 || _vs_low_offset < 0 ||
        _code_heap_segmap_offset < 0 || _heap_block_used_offset < 0 ||
        _code_heap_segment_shift < 0 || _code_heap_segment_shift > 31) {
        return nullptr;
    }
    
    unsigned char* heap_start = SafeAccess::load((unsigned char**)(heap + _code_heap_memory_offset + _vs_low_offset), static_cast<unsigned char*>(nullptr));
    unsigned char* segmap = SafeAccess::load((unsigned char**)(heap + _code_heap_segmap_offset + _vs_low_offset), static_cast<unsigned char*>(nullptr));
    
    if (heap_start == nullptr || segmap == nullptr) {
        return nullptr;
    }
    
    // Check for pointer arithmetic overflow
    if (static_cast<const unsigned char*>(pc) < heap_start) {
        return nullptr;
    }
    
    size_t offset = static_cast<const unsigned char*>(pc) - heap_start;
    size_t idx = offset >> _code_heap_segment_shift;

    // Check for array bounds
    const size_t max_idx = static_cast<size_t>(1) << (32 - _code_heap_segment_shift);
    if (idx >= max_idx) {
        return nullptr;
    }
    
    if (segmap[idx] == 0xff) {
        return nullptr;
    }
    
    while (segmap[idx] > 0) {
        size_t prev_idx = idx;
        idx -= segmap[idx];
        // Check for underflow
        if (idx >= prev_idx) {
            return nullptr;
        }
    }

    unsigned char* block = heap_start + (idx << _code_heap_segment_shift) + _heap_block_used_offset;
    unsigned char block_flag = SafeAccess::load(block, static_cast<unsigned char>(0));
    return block_flag ? align<NMethod*>(block + sizeof(uintptr_t)) : nullptr;
}

JVMFlag* JVMFlag::find(const char* name) {
    if (name == nullptr || _flags_addr == nullptr || _flag_size <= 0 || _flag_count <= 0) {
        return nullptr;
    }
    
    // Use linear search with bounds checking
    for (int i = 0; i < _flag_count; i++) {
        JVMFlag* f = reinterpret_cast<JVMFlag*>(_flags_addr + i * _flag_size);
        if (f == nullptr) continue;
        
        const char* flag_name = f->name();
        if (flag_name != nullptr && strcmp(flag_name, name) == 0 && f->addr() != nullptr) {
            return f;
        }
    }
    return nullptr;
}

int NMethod::findScopeOffset(const void* pc) {
    intptr_t pc_offset = (const char*)pc - code();
    if (pc_offset < 0 || pc_offset > 0x7fffffff) {
        return -1;
    }

    const int* scopes_pcs = (const int*) at(_scopes_pcs_offset);
    PcDesc* pcd = (PcDesc*) immutableDataAt(scopes_pcs[0]);
    PcDesc* pcd_end = (PcDesc*) immutableDataAt(scopes_pcs[1]);
    int low = 0;
    int high = (pcd_end - pcd) - 1;

    while (low <= high) {
        int mid = (unsigned int)(low + high) >> 1;
        if (pcd[mid]._pc < pc_offset) {
            low = mid + 1;
        } else if (pcd[mid]._pc > pc_offset) {
            high = mid - 1;
        } else {
            return pcd[mid]._scope_offset;
        }
    }

    return pcd + low < pcd_end ? pcd[low]._scope_offset : -1;
}

int ScopeDesc::readInt() {
    unsigned char c = *_stream++;
    unsigned int n = c - _unsigned5_base;
    if (c >= 192) {
        for (int shift = 6; ; shift += 6) {
            c = *_stream++;
            n += (c - _unsigned5_base) << shift;
            if (c < 192 || shift >= 24) break;
        }
    }
    return n;
}
