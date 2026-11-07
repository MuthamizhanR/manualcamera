#include "ClHelper.h"
#include "Exceptions.h"

#ifdef GPU_CAMERA_PREVIEW


namespace motioncam {
    int CL_acquire(cl_context *ctx, cl_command_queue *q, bool create) {
        return 0;
    }

    int CL_release() {
        return 0;
    }

    cl_mem CL_createBuffer(cl_context context, cl_mem_flags flags, size_t size, void* hostPtr, cl_int* errCode) {
        typedef cl_mem (*createBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*);

        void* func = nullptr;
        return ((createBuffer) func)(context, flags, size, hostPtr, errCode);
    }

    cl_int CL_releaseMemObject(cl_mem mem) {
        typedef cl_int (*releaseMemObject)(cl_mem);

        void* func = nullptr;
        return ((releaseMemObject) func)(mem);
    }

    void* CL_enqueueMapBuffer(
            cl_command_queue command_queue,
            cl_mem buffer,
            cl_bool blocking_map,
            cl_map_flags map_flags,
            size_t offset,
            size_t size,
            cl_uint num_events_in_wait_list,
            const cl_event* event_wait_list,
            cl_event* event,
            cl_int* errcode_ret)
    {
        typedef void* (*enqueueMapBuffer)(
                cl_command_queue, cl_mem, cl_bool, cl_map_flags, size_t, size_t, cl_uint, const cl_event*, cl_event*, cl_int*);

        void* func = nullptr;
        return ((enqueueMapBuffer) func)(
                command_queue, buffer, blocking_map, map_flags, offset, size, num_events_in_wait_list, event_wait_list, event, errcode_ret);
    }

    cl_int CL_enqueueUnmapMemObject(
            cl_command_queue command_queue,
            cl_mem memobj,
            void* mapped_ptr,
            cl_uint num_events_in_wait_list,
            const cl_event* event_wait_list,
            cl_event* event)
    {
        typedef cl_int (*enqueueUnmapMemObject)(
                cl_command_queue, cl_mem, void*, cl_uint, const cl_event*, cl_event*);

        void* func = nullptr;
        return ((enqueueUnmapMemObject) func)(
                command_queue, memobj, mapped_ptr, num_events_in_wait_list, event_wait_list, event);
    }
}

#endif
