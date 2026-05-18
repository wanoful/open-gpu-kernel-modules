/*******************************************************************************
    Copyright (c) 2026 POLARIS contributors

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

        The above copyright notice and this permission notice shall be
        included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

*******************************************************************************/

#include <linux/module.h>

#include "uvm_common.h"
#include "uvm_gpu.h"
#include "uvm_polaris.h"
#include "uvm_processors.h"

typedef int (*uvm_polaris_fault_handler_t)(NvU32 gpu_id, NvU64 fault_address, NvU32 access_type);

extern int polaris_uvm_handle_gpu_fault(NvU32 gpu_id, NvU64 fault_address, NvU32 access_type);

NV_STATUS uvm_polaris_filter_replayable_faults(uvm_parent_gpu_t *parent_gpu,
                                               uvm_fault_service_batch_context_t *batch_context,
                                               NvU32 *handled_faults)
{
    NvU32 i;
    NvU32 out = 0;
    NvU32 original_count = batch_context->num_coalesced_faults;
    uvm_polaris_fault_handler_t handler;

    *handled_faults = 0;

#if defined(symbol_get)
    handler = symbol_get(polaris_uvm_handle_gpu_fault);
#else
    handler = NULL;
#endif
    if (!handler)
        return NV_OK;

    for (i = 0; i < batch_context->num_coalesced_faults; ++i) {
        uvm_fault_buffer_entry_t *entry = batch_context->ordered_fault_cache[i];
        int ret;

        ret = handler(uvm_parent_id_value(parent_gpu->id), entry->fault_address, (NvU32)entry->fault_access_type);

        if (ret == UVM_POLARIS_FAULT_HANDLED) {
            entry->filtered = true;
            *handled_faults += entry->num_instances;
            continue;
        }
        else if (ret == UVM_POLARIS_FAULT_NOT_MINE) {
            batch_context->ordered_fault_cache[out++] = entry;
        }
        else {
            UVM_DBG_PRINT("POLARIS fault handler returned %d for GPU%u VA 0x%llx; cancelling batch\n",
                          ret,
                          uvm_parent_id_value(parent_gpu->id),
                          entry->fault_address);
            batch_context->ordered_fault_cache[out++] = entry;
            while (++i < original_count)
                batch_context->ordered_fault_cache[out++] = batch_context->ordered_fault_cache[i];
            batch_context->num_coalesced_faults = out;
#if defined(symbol_put)
            symbol_put(polaris_uvm_handle_gpu_fault);
#endif
            return NV_ERR_INVALID_ADDRESS;
        }
    }

    batch_context->num_coalesced_faults = out;

#if defined(symbol_put)
    symbol_put(polaris_uvm_handle_gpu_fault);
#endif

    return NV_OK;
}
