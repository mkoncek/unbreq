#!/usr/bin/python3
#
# bcc_file_monitor.py
#
# This script monitors file access syscalls (openat, execve) for a specific
# process and all its descendants, but only if the process is an "rpmbuild" process.
# It uses stable tracepoints for reliability.
#
# Usage: sudo ./bcc_file_monitor.py <PID>

from bcc import BPF
import argparse
import ctypes as ct
import os
import subprocess

# Argument parsing
parser = argparse.ArgumentParser(
    description="Monitor file accesses for a process tree if it's rpmbuild.",
    formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("pid", type=int, help="The target PID to start monitoring from.")
args = parser.parse_args()

# BPF C program
bpf_text = """
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>
#include <linux/fs.h>

// Enum to identify the type of event sent to user-space
enum event_type {
    EVENT_OPEN,
    EVENT_EXEC,
    EVENT_FORK,
    EVENT_RPMBUILD_START,
    EVENT_PROCESS_EXIT,
};

// Data structure to pass event information from kernel to user-space
struct data_t {
    enum event_type type;
    u32 pid;
    u32 ppid; // Parent PID, used for fork
    char comm[TASK_COMM_LEN];
    char filename[256];
    u8 is_rpmbuild; // Flag for exit event
};

BPF_PERF_OUTPUT(events);

// Map to store PIDs of all processes we are actively tracking (rpmbuild + children)
BPF_HASH(tracked_pids, u32, u8);

// Map to store only the PIDs of processes confirmed to be 'rpmbuild'
BPF_HASH(rpmbuild_pids, u32, u8);

// Trace process forks to follow children
TRACEPOINT_PROBE(sched, sched_process_fork)
{
    u32 parent_pid = args->parent_pid;
    u32 child_pid = args->child_pid;

    // If the parent is tracked, track the child.
    if (tracked_pids.lookup(&parent_pid)) {
        u8 one = 1;
        tracked_pids.update(&child_pid, &one);

        struct data_t data = {};
        data.type = EVENT_FORK;
        data.pid = child_pid;
        data.ppid = parent_pid;
        // At fork time, the parent is the current process. Get its comm.
        bpf_get_current_comm(&data.comm, sizeof(data.comm));

        events.perf_submit(args, &data, sizeof(data));
    }
    return 0;
}

// Trace process exits to clean up maps
TRACEPOINT_PROBE(sched, sched_process_exit)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;

    // If it was a tracked process, send an exit event and clean up.
    if (tracked_pids.lookup(&pid)) {
        struct data_t data = {};
        data.type = EVENT_PROCESS_EXIT;
        data.pid = pid;
        bpf_get_current_comm(&data.comm, sizeof(data.comm));

        // Check if it was an rpmbuild process
        if (rpmbuild_pids.lookup(&pid)) {
            data.is_rpmbuild = 1;
            rpmbuild_pids.delete(&pid);
        } else {
            data.is_rpmbuild = 0;
        }

        events.perf_submit(args, &data, sizeof(data));
        tracked_pids.delete(&pid);
    }
    return 0;
}

// Generic handler for both openat and execve syscalls
static int trace_syscall_enter(void *ctx, const char __user *filename, enum event_type type)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u8 *is_tracked = tracked_pids.lookup(&pid);

    // If not currently tracked, check if we should start tracking now.
    if (!is_tracked) {
        // We only perform this check for the initial target PID.
        if (pid == TARGET_PID) {
            char current_comm[TASK_COMM_LEN];
            bpf_get_current_comm(&current_comm, sizeof(current_comm));

            char rpmbuild_str[] = "rpmbuild";
            int match = 1;
            #pragma unroll
            for (int i = 0; i < 8; ++i) { // "rpmbuild" is 8 chars
                if (current_comm[i] != rpmbuild_str[i] || current_comm[i] == '\\0') {
                    match = 0;
                    break;
                }
            }

            if (match) {
                u8 one = 1;
                tracked_pids.update(&pid, &one);
                rpmbuild_pids.update(&pid, &one);
                is_tracked = tracked_pids.lookup(&pid); // Set the flag so we proceed

                // Send an event to Python to let it know we've started.
                struct data_t data = {};
                data.type = EVENT_RPMBUILD_START;
                data.pid = pid;
                bpf_get_current_comm(&data.comm, sizeof(data.comm));
                events.perf_submit(ctx, &data, sizeof(data));
            }
        }
    }

    // If not tracked after all checks, exit.
    if (!is_tracked) {
        return 0;
    }

    // Now we are sure the process is tracked, so submit the actual syscall event.
    struct data_t data = {};
    data.type = type;
    data.pid = pid;
    bpf_get_current_comm(&data.comm, sizeof(data.comm));
    bpf_probe_read_user_str(&data.filename, sizeof(data.filename), filename);
    events.perf_submit(ctx, &data, sizeof(data));

    return 0;
}


// Attach to the enter tracepoints for openat
TRACEPOINT_PROBE(syscalls, sys_enter_openat)
{
    return trace_syscall_enter(args, args->filename, EVENT_OPEN);
}

// Attach to the enter tracepoints for execve
TRACEPOINT_PROBE(syscalls, sys_enter_execve)
{
    char filename[64]; // Check just a portion of the filename for performance
    bpf_probe_read_user_str(&filename, sizeof(filename), args->filename);

    // Manual check if filename contains "rpmbuild"
    char rpmbuild_str[] = "rpmbuild";
    #pragma unroll
    for (int i = 0; i < sizeof(filename) - sizeof(rpmbuild_str); i++) {
        int match = 1;
        #pragma unroll
        for (int j = 0; j < sizeof(rpmbuild_str) - 1; j++) {
            if (filename[i+j] != rpmbuild_str[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            u32 pid = bpf_get_current_pid_tgid() >> 32;
            u8 one = 1;
            // If a process execs into rpmbuild, start tracking it.
            if (!rpmbuild_pids.lookup(&pid)) {
                rpmbuild_pids.update(&pid, &one);
                tracked_pids.update(&pid, &one); // Also add to general tracked list

                struct data_t data = {};
                data.type = EVENT_RPMBUILD_START;
                data.pid = pid;
                bpf_get_current_comm(&data.comm, sizeof(data.comm));
                events.perf_submit(args, &data, sizeof(data));
            }
            break;
        }
    }

    // Now call the generic handler to log the EXEC event itself for any tracked process
    return trace_syscall_enter(args, args->filename, EVENT_EXEC);
}
"""

# --- Python User-space Code ---

# Cache for PID info: {pid: (comm, exe_path)}
pid_cache = {}

# Python representation of the data_t struct
class Data(ct.Structure):
    _fields_ = [
        ("type", ct.c_int),
        ("pid", ct.c_uint),
        ("ppid", ct.c_uint),
        ("comm", ct.c_char * 16), # TASK_COMM_LEN
        ("filename", ct.c_char * 256),
        ("is_rpmbuild", ct.c_uint8),
    ]

def get_process_info(pid):
    """
    Resolves PID to its command name and full executable path.
    Uses a cache to avoid repeated /proc lookups.
    """
    if pid in pid_cache:
        return pid_cache[pid]

    try:
        path = os.readlink(f"/proc/{pid}/exe")
        comm = open(f"/proc/{pid}/comm").read().strip()
        pid_cache[pid] = (comm, path)
        return comm, path
    except FileNotFoundError:
        # Process may have exited between event and this lookup
        return (b'<exited>', b'<unknown>')

# Callback function to process events from the kernel
def print_event(cpu, data, size):
    event = ct.cast(data, ct.POINTER(Data)).contents

    event_type = event.type
    pid = event.pid

    if event_type == 0: # EVENT_OPEN
        comm, path = get_process_info(pid)
        details = f"-> {event.filename.decode('utf-8', 'replace')}"
        print(f"{'OPENAT':<18}: {path} ({pid}) {'':<16} {details}")
    elif event_type == 1: # EVENT_EXEC
        # At exec, the old path is still valid for the PID, but the new one is in filename
        _comm, old_path = get_process_info(pid)
        new_path = event.filename.decode('utf-8', 'replace')
        print(f"{'EXEC':<18}: {old_path} ({pid}) -> {new_path}")
        # Invalidate cache, it will be repopulated on the next event
        if pid in pid_cache:
            del pid_cache[pid]
    elif event_type == 2: # EVENT_FORK
        ppid = event.ppid
        pcomm, ppath = get_process_info(ppid)
        details = f"{ppath} ({ppid}) -> {pid}"
        print(f"{'FORK':<18}: {details}")
    elif event_type == 3: # EVENT_RPMBUILD_START
        comm, path = get_process_info(pid)
        print(f"{'RPMBUILD STARTED':<18}: {path} ({pid}) - now tracking.")
    elif event_type == 4: # EVENT_PROCESS_EXIT
        comm, path = get_process_info(pid)
        exit_type = "RPMBUILD" if event.is_rpmbuild else "PROCESS"
        print(f"{exit_type + ' EXITED':<18}: {path} ({pid})")
        if pid in pid_cache:
            del pid_cache[pid]

# --- Main Program ---
print(f"=== Tracking file accesses for PID {args.pid} tree (if rpmbuild is involved) ===")
print("Hit Ctrl-C to end...")

# Load BPF program, replacing the placeholder with the actual PID
b = BPF(text=bpf_text.replace('TARGET_PID', str(args.pid)))

# Open perf buffer to receive events from the kernel
b["events"].open_perf_buffer(print_event)

# Main loop to process events
try:
    while True:
        b.perf_buffer_poll()
except KeyboardInterrupt:
    print("\n=== Tracking ended by user ===")
    exit()

