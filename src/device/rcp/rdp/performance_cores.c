#include "performance_cores.h"
#include <stdlib.h>
#include <string.h>
#include <features/features_cpu.h>
#include <libretro.h>
#ifdef __linux__
#include <stdio.h>
#include <unistd.h>
#endif

extern retro_log_printf_t log_cb;
static bool initialized, enabled = true;
static unsigned core_count;
#ifdef _WIN32
static GROUP_AFFINITY cores[1024];
#elif defined(__linux__)
static cpu_set_t cores[CPU_SETSIZE];
static int linux_topology_value(unsigned cpu, const char *name)
{
    char path[192];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/%s", cpu, name);
    FILE *file = fopen(path, "r");
    int value = -1;
    if (file) { if (fscanf(file, "%d", &value) != 1) value = -1; fclose(file); }
    return value;
}
#endif

void performance_cores_init(void)
{
    if (initialized) return;
    initialized = true;
#ifdef _WIN32
    DWORD bytes = 0, offset;
    unsigned highest = 0;
    GROUP_AFFINITY current;
    DWORD_PTR process_mask = 0, system_mask = 0;
    unsigned char *buffer;
    GetThreadGroupAffinity(GetCurrentThread(), &current);
    GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bytes);
    buffer = bytes ? (unsigned char *)malloc(bytes) : NULL;
    if (buffer && GetLogicalProcessorInformationEx(RelationProcessorCore,
                (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer, &bytes))
    {
        /* Higher EfficiencyClass means greater performance on Windows.
         * All-zero classes describe a homogeneous CPU: every core qualifies. */
        for (offset = 0; offset < bytes; )
        {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (void *)(buffer + offset);
            if (!info->Size || info->Size > bytes - offset) break;
            if (info->Relationship == RelationProcessorCore &&
                info->Processor.EfficiencyClass > highest)
                highest = info->Processor.EfficiencyClass;
            offset += info->Size;
        }
        for (offset = 0; offset < bytes; )
        {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (void *)(buffer + offset);
            if (!info->Size || info->Size > bytes - offset) break;
            if (info->Relationship == RelationProcessorCore &&
                info->Processor.EfficiencyClass == highest && info->Processor.GroupCount)
            {
                GROUP_AFFINITY affinity = info->Processor.GroupMask[0];
                if (affinity.Group == current.Group && process_mask)
                    affinity.Mask &= process_mask;
                if (affinity.Mask && core_count < sizeof(cores) / sizeof(cores[0]))
                    cores[core_count++] = affinity;
            }
            offset += info->Size;
        }
    }
    free(buffer);
#elif defined(__linux__)
    cpu_set_t allowed;
    int packages[CPU_SETSIZE], ids[CPU_SETSIZE];
    int highest = 0;
    if (!sched_getaffinity(0, sizeof(allowed), &allowed)) {
        for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (!CPU_ISSET(cpu, &allowed)) continue;
            int quality = linux_topology_value(cpu, "cpu_capacity");
            if (quality < 0) quality = linux_topology_value(cpu, "topology/core_type");
            if (quality < 0) quality = 0;
            if (quality > highest) highest = quality;
        }
        for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (!CPU_ISSET(cpu, &allowed)) continue;
            int package = linux_topology_value(cpu, "topology/physical_package_id");
            int id = linux_topology_value(cpu, "topology/core_id");
            int quality = linux_topology_value(cpu, "cpu_capacity");
            if (quality < 0) quality = linux_topology_value(cpu, "topology/core_type");
            if (quality < 0) quality = 0;
            if (id < 0 || package < 0 || quality != highest) continue;
            unsigned slot;
            for (slot = 0; slot < core_count; ++slot)
                if (packages[slot] == package && ids[slot] == id) break;
            if (slot == core_count) {
                packages[slot] = package; ids[slot] = id;
                CPU_ZERO(&cores[slot]); ++core_count;
            }
            CPU_SET(cpu, &cores[slot]);
        }
    }
#endif
    if (log_cb)
        log_cb(RETRO_LOG_INFO, "Renderer: %u physical performance cores detected; affinity %s.\n",
                core_count, core_count ? "available" : "unavailable (OS scheduling)");
}

unsigned performance_cores_count(void)
{
    performance_cores_init();
    /* Do not invent P-core topology when the OS cannot report it. */
    return core_count ? core_count : 1;
}

void performance_cores_set_enabled(bool value) { enabled = value; }

bool performance_emulation_enter(struct performance_affinity *state)
{
    memset(state, 0, sizeof(*state));
#ifdef _WIN32
    if (enabled && core_count)
    {
        GROUP_AFFINITY current, preferred;
        KAFFINITY eligible = 0;
        if (!GetThreadGroupAffinity(GetCurrentThread(), &current))
            return false;
        preferred = current;
        preferred.Mask = 0;
        for (unsigned i = 0; i < core_count; ++i)
            if (cores[i].Group == current.Group)
            {
                KAFFINITY available = cores[i].Mask & current.Mask;
                eligible |= available;
                if (!preferred.Mask) preferred.Mask = available;
            }
        /* Respect explicit host pinning, including masks with no P-core.
         * Homogeneous CPUs and already restricted threads need no change. */
        /* Rendering lane zero already runs on the first eligible physical
         * core. Keep CPU/RSP execution there between batches too, rather
         * than migrating its working set across P/E cores repeatedly. */
        if (eligible && eligible != current.Mask)
            state->changed = SetThreadGroupAffinity(GetCurrentThread(),
                    &preferred, &state->previous) != 0;
    }
#elif defined(__linux__)
    if (enabled && core_count && !sched_getaffinity(0, sizeof(state->previous), &state->previous)) {
        cpu_set_t preferred;
        CPU_AND(&preferred, &cores[0], &state->previous);
        if (CPU_COUNT(&preferred) && !CPU_EQUAL(&preferred, &state->previous))
            state->changed = !sched_setaffinity(0, sizeof(preferred), &preferred);
    }
#endif
    return state->changed;
}

bool performance_core_enter(unsigned worker, struct performance_affinity *state)
{
    memset(state, 0, sizeof(*state));
#ifdef _WIN32
    if (enabled && core_count) {
        const GROUP_AFFINITY *target = &cores[worker % core_count];
        GROUP_AFFINITY current;
        if (GetThreadGroupAffinity(GetCurrentThread(), &current) &&
            (current.Group != target->Group || current.Mask != target->Mask))
            state->changed = SetThreadGroupAffinity(GetCurrentThread(), target, &state->previous) != 0;
    }
#elif defined(__linux__)
    if (enabled && core_count && !sched_getaffinity(0, sizeof(state->previous), &state->previous)) {
        const cpu_set_t *target = &cores[worker % core_count];
        if (!CPU_EQUAL(target, &state->previous))
            state->changed = !sched_setaffinity(0, sizeof(*target), target);
    }
#else
    (void)worker;
#endif
    return state->changed;
}

void performance_core_leave(struct performance_affinity *state)
{
#ifdef _WIN32
    if (state->changed)
        SetThreadGroupAffinity(GetCurrentThread(), &state->previous, NULL);
#elif defined(__linux__)
    if (state->changed)
        sched_setaffinity(0, sizeof(state->previous), &state->previous);
#endif
    state->changed = false;
}
