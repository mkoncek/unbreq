#!/usr/bin/env python3

import sys
import argparse
import ctypes as ct
from bcc import BPF

# BPF program written in C
bpf_text = """
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>
#include <linux/fs.h>

// Maps to track processes
BPF_HASH(tracked, u32, u8);        // PID -> 1 (tracked processes)
BPF_HASH(rpmbuild_pids, u32, u8);  // PID -> 1 (rpmbuild processes)
BPF_ARRAY(target_pid, u32, 1);     // Store target PID
BPF_ARRAY(target_checked, u8, 1);  // Has target been checked?

// Structure for passing data to userspace
struct data_t {
    u32 pid;
    u32 ppid;
    char comm[TASK_COMM_LEN];
    char filename[256];
    int type;  // 0=openat, 1=execve, 2=fork, 3=rpmbuild_found, 4=process_exit
};

BPF_PERF_OUTPUT(events);

// Check if target PID is rpmbuild and start tracking
int trace_openat_entry(struct pt_regs *ctx)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u32 zero = 0;
    u32 *target = target_pid.lookup(&zero);
    u8 *checked = target_checked.lookup(&zero);
    
    if (!target || !checked) return 0;
    
    // Check if this is the target PID and we haven't checked it yet
    if (pid == *target && *checked == 0) {
        u8 one = 1;
        target_checked.update(&zero, &one);
        
        char comm[TASK_COMM_LEN];
        bpf_get_current_comm(&comm, sizeof(comm));
        
        if (bpf_probe_read_kernel_str(comm, sizeof(comm), comm) > 0) {
            // Check if it's rpmbuild
            if (comm[0] == 'r' && comm[1] == 'p' && comm[2] == 'm' && 
                comm[3] == 'b' && comm[4] == 'u' && comm[5] == 'i' && 
                comm[6] == 'l' && comm[7] == 'd') {
                tracked.update(&pid, &one);
                
                struct data_t data = {};
                data.pid = pid;
                data.type = 3; // rpmbuild_found
                bpf_get_current_comm(&data.comm, sizeof(data.comm));
                events.perf_submit(ctx, &data, sizeof(data));
            }
        }
    }
    
    // Mark rpmbuild processes for tracking
    char comm[TASK_COMM_LEN];
    bpf_get_current_comm(&comm, sizeof(comm));
    
    if (comm[0] == 'r' && comm[1] == 'p' && comm[2] == 'm' && 
        comm[3] == 'b' && comm[4] == 'u' && comm[5] == 'i' && 
        comm[6] == 'l' && comm[7] == 'd') {
        u8 *found = rpmbuild_pids.lookup(&pid);
        if (!found) {
            u8 one = 1;
            rpmbuild_pids.update(&pid, &one);
            
            struct data_t data = {};
            data.pid = pid;
            data.type = 3; // rpmbuild_found
            bpf_get_current_comm(&data.comm, sizeof(data.comm));
            events.perf_submit(ctx, &data, sizeof(data));
        }
    }
    
    // Monitor openat for tracked processes
    u8 *is_tracked = tracked.lookup(&pid);
    if (is_tracked) {
        struct data_t data = {};
        data.pid = pid;
        data.type = 0; // openat
        bpf_get_current_comm(&data.comm, sizeof(data.comm));
        
        // Get filename - try different parameter access methods
        struct pt_regs *regs = (struct pt_regs *)PT_REGS_PARM1(ctx);
        char *filename_ptr = NULL;
        if (regs) {
            filename_ptr = (char *)regs->si;  // RSI register for openat's pathname
        } else {
            filename_ptr = (char *)PT_REGS_PARM2(ctx);  // Fallback
        }
        
        if (filename_ptr != NULL) {
            int ret = bpf_probe_read_user_str(&data.filename, sizeof(data.filename), filename_ptr);
            if (ret < 0) {
                __builtin_memcpy(&data.filename, "<read_failed>", 14);
            }
        } else {
            __builtin_memcpy(&data.filename, "<null>", 7);
        }
        
        events.perf_submit(ctx, &data, sizeof(data));
    }
    
    return 0;
}

// Monitor execve calls
int trace_execve_entry(struct pt_regs *ctx)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // Mark rpmbuild processes for tracking
    char comm[TASK_COMM_LEN];
    bpf_get_current_comm(&comm, sizeof(comm));
    
    if (comm[0] == 'r' && comm[1] == 'p' && comm[2] == 'm' && 
        comm[3] == 'b' && comm[4] == 'u' && comm[5] == 'i' && 
        comm[6] == 'l' && comm[7] == 'd') {
        u8 *found = rpmbuild_pids.lookup(&pid);
        if (!found) {
            u8 one = 1;
            rpmbuild_pids.update(&pid, &one);
            
            struct data_t data = {};
            data.pid = pid;
            data.type = 3; // rpmbuild_found
            bpf_get_current_comm(&data.comm, sizeof(data.comm));
            events.perf_submit(ctx, &data, sizeof(data));
        }
    }
    
    // Monitor execve for tracked processes
    u8 *is_tracked = tracked.lookup(&pid);
    if (is_tracked) {
        struct data_t data = {};
        data.pid = pid;
        data.type = 1; // execve
        bpf_get_current_comm(&data.comm, sizeof(data.comm));
        
        // Get filename - try different parameter access methods
        struct pt_regs *regs = (struct pt_regs *)PT_REGS_PARM1(ctx);
        char *filename_ptr = NULL;
        if (regs) {
            filename_ptr = (char *)regs->di;  // RDI register for execve's filename
        } else {
            filename_ptr = (char *)PT_REGS_PARM1(ctx);  // Fallback
        }
        
        if (filename_ptr != NULL) {
            int ret = bpf_probe_read_user_str(&data.filename, sizeof(data.filename), filename_ptr);
            if (ret < 0) {
                __builtin_memcpy(&data.filename, "<read_failed>", 14);
            }
        } else {
            __builtin_memcpy(&data.filename, "<null>", 7);
        }
        
        events.perf_submit(ctx, &data, sizeof(data));
    }
    
    return 0;
}

// Monitor process forks using kprobe
int trace_do_fork(struct pt_regs *ctx, unsigned long clone_flags, 
                  unsigned long stack_start, unsigned long stack_size,
                  int __user *parent_tidptr, int __user *child_tidptr,
                  unsigned long tls)
{
    u32 parent_pid = bpf_get_current_pid_tgid() >> 32;
    
    // Check if parent is rpmbuild or already tracked
    u8 *parent_is_rpmbuild = rpmbuild_pids.lookup(&parent_pid);
    u8 *parent_is_tracked = tracked.lookup(&parent_pid);
    
    if (parent_is_rpmbuild != NULL) {
        // If parent is rpmbuild and not already tracked, start tracking parent too
        if (parent_is_tracked == NULL) {
            u8 one = 1;
            tracked.update(&parent_pid, &one);
        }
    }
    
    return 0;
}

// Monitor process forks return to get child PID
int trace_do_fork_return(struct pt_regs *ctx)
{
    u32 parent_pid = bpf_get_current_pid_tgid() >> 32;
    long child_pid = PT_REGS_RC(ctx);
    
    // Only proceed if fork was successful (child_pid > 0)
    if (child_pid <= 0) return 0;
    
    u32 child_pid_u32 = (u32)child_pid;
    
    // Check if parent is rpmbuild or already tracked
    u8 *parent_is_rpmbuild = rpmbuild_pids.lookup(&parent_pid);
    u8 *parent_is_tracked = tracked.lookup(&parent_pid);
    
    int should_track = 0;
    if (parent_is_rpmbuild != NULL) {
        should_track = 1;
    }
    if (parent_is_tracked != NULL) {
        should_track = 1;
    }
    
    if (should_track) {
        u8 one = 1;
        tracked.update(&child_pid_u32, &one);
        
        struct data_t data = {};
        data.pid = child_pid_u32;
        data.ppid = parent_pid;
        data.type = 2; // fork
        bpf_get_current_comm(&data.comm, sizeof(data.comm));
        events.perf_submit(ctx, &data, sizeof(data));
    }
    
    return 0;
}

// Clean up when processes exit - use kprobe on do_exit
int trace_do_exit(struct pt_regs *ctx, long code)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // Check if this was an rpmbuild process and notify userspace
    u8 *was_rpmbuild = rpmbuild_pids.lookup(&pid);
    if (was_rpmbuild) {
        struct data_t data = {};
        data.pid = pid;
        data.type = 4; // process_exit
        bpf_get_current_comm(&data.comm, sizeof(data.comm));
        events.perf_submit(ctx, &data, sizeof(data));
    }
    
    tracked.delete(&pid);
    rpmbuild_pids.delete(&pid);
    return 0;
}
"""

# Track active rpmbuild processes
active_rpmbuild_pids = set()
should_exit = False

def print_event(cpu, data, size):
    global active_rpmbuild_pids, should_exit
    event = b["events"].event(data)
    
    if event.type == 0:  # openat
        print(f"OPENAT: PID {event.pid} ({event.comm.decode('utf-8', 'replace')}) -> {event.filename.decode('utf-8', 'replace')}")
    elif event.type == 1:  # execve
        print(f"EXEC: PID {event.pid} ({event.comm.decode('utf-8', 'replace')}) -> {event.filename.decode('utf-8', 'replace')}")
    elif event.type == 2:  # fork
        print(f"FORK: {event.ppid} -> {event.pid}")
    elif event.type == 3:  # rpmbuild_found
        print(f"Found rpmbuild process: PID {event.pid} - marking for tracking")
        active_rpmbuild_pids.add(event.pid)
    elif event.type == 4:  # process_exit
        pid = event.pid
        comm = event.comm.decode('utf-8', 'replace')
        if pid in active_rpmbuild_pids:
            active_rpmbuild_pids.remove(pid)
            print(f"rpmbuild process PID {pid} ({comm}) exited - {len(active_rpmbuild_pids)} rpmbuild processes remaining")
            if len(active_rpmbuild_pids) == 0:
                print("\n=== All rpmbuild processes have finished - stopping monitoring ===")
                should_exit = True

def main():
    parser = argparse.ArgumentParser(description='Track file accesses starting from rpmbuild processes')
    parser.add_argument('pid', type=int, help='Target PID to monitor')
    args = parser.parse_args()
    
    global b
    b = BPF(text=bpf_text)
    
    # Set target PID
    target_array = b.get_table("target_pid")
    target_array[ct.c_uint32(0)] = ct.c_uint32(args.pid)
    
    # Attach syscall kprobes for parameter access
    # Try multiple openat function names
    openat_attached = False
    for openat_func in ["__x64_sys_openat", "do_sys_openat2", "do_sys_open"]:
        try:
            b.attach_kprobe(event=openat_func, fn_name="trace_openat_entry")
            print(f"Attached to {openat_func}")
            openat_attached = True
            break
        except:
            continue
    if not openat_attached:
        print("Warning: Could not attach to any openat function")

    # Try multiple execve function names
    execve_attached = False
    for execve_func in ["__x64_sys_execve", "do_execveat", "__se_sys_execve"]:
        try:
            b.attach_kprobe(event=execve_func, fn_name="trace_execve_entry")
            print(f"Attached to {execve_func}")
            execve_attached = True
            break
        except:
            continue
    if not execve_attached:
        print("Warning: Could not attach to any execve function")
    
    try:
        b.attach_kprobe(event="__x64_sys_clone", fn_name="trace_do_fork")
        b.attach_kretprobe(event="__x64_sys_clone", fn_name="trace_do_fork_return")
        print("Attached to __x64_sys_clone")
    except:
        try:
            b.attach_kprobe(event="kernel_clone", fn_name="trace_do_fork")
            b.attach_kretprobe(event="kernel_clone", fn_name="trace_do_fork_return")
            print("Attached to kernel_clone")
        except:
            try:
                b.attach_kprobe(event="_do_fork", fn_name="trace_do_fork")
                b.attach_kretprobe(event="_do_fork", fn_name="trace_do_fork_return")
                print("Attached to _do_fork")
            except:
                print("Warning: Could not attach to fork functions")
    
    try:
        b.attach_kprobe(event="do_exit", fn_name="trace_do_exit")
        print("Attached to do_exit")
    except:
        print("Warning: Could not attach to exit function")
    
    print(f"=== Tracking file accesses starting from rpmbuild processes (monitoring PID {args.pid} tree) ===")
    print("Hit Ctrl-C to end, or wait for all rpmbuild processes to finish...")
    
    # Process events
    b["events"].open_perf_buffer(print_event)
    
    try:
        while not should_exit:
            b.perf_buffer_poll(timeout=1000)  # Poll with 1 second timeout to check should_exit
    except KeyboardInterrupt:
        print("\n=== Tracking ended ===")

if __name__ == "__main__":
    main()





