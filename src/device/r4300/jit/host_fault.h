/* Synchronous JIT fault recovery. SPDX-License-Identifier: GPL-2.0-or-later
 * Only an exact published instruction PC accessing our guard page is owned.
 * Recovery changes PC to the existing precise exit; no guest work, allocation,
 * locking or logging takes place inside the exception/signal handler. */
#ifndef N64EMU_JIT_HOST_FAULT_H
#define N64EMU_JIT_HOST_FAULT_H
#include <mutex>
#include <atomic>
#include <signal.h>
#if defined(__linux__)
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

struct host_fault_site { uintptr_t pc, resume; unsigned hits; };
struct host_fault_segment { host_fault_site* sites; size_t count, capacity; };
struct host_fault_domain {
    uintptr_t code, guard;
    size_t code_size, segment_size, page_size, segment_count;
    host_fault_segment* segments;
    volatile sig_atomic_t faults;
    volatile sig_atomic_t pending_segment, pending_site;
#if defined(__linux__)
    unsigned registration;
#endif
};
#if defined(__linux__) && defined(__GNUC__)
/* Accessed only in ordinary execution. Requiring initial-exec here would
 * force this entire DSO's large renderer TLS image into dlopen's small
 * static surplus, even when fault recovery is disabled. */
static __thread host_fault_domain* active_fault_domain __attribute__((tls_model("global-dynamic")));
#else
static thread_local host_fault_domain* active_fault_domain;
#endif
static std::mutex fault_handler_mutex;
static std::atomic<unsigned> fault_handler_users{0};
#ifdef _WIN32
static void* fault_handler_token;
#elif defined(__linux__)
static struct sigaction previous_fault_action;
static std::atomic_flag previous_fault_consumed = ATOMIC_FLAG_INIT;
/* Published outside signal context. A slot belongs to a live domain; if all
 * slots are occupied, creation fails and the normal explicit guards remain.
 * The handler never accesses TLS, including on uninitialized foreign threads. */
struct host_fault_registration {
    std::atomic<host_fault_domain*> domain{nullptr};
    std::atomic<unsigned> owner{0};
};
static host_fault_registration fault_registrations[64];
static_assert(std::atomic<host_fault_domain*>::is_always_lock_free &&
              std::atomic<unsigned>::is_always_lock_free,
              "Signal fault registry must use lock-free atomics");
static unsigned host_fault_thread_id(void)
{
    long id = syscall(SYS_gettid);
    return id > 0 ? (unsigned)id : 0;
}
#endif

static void host_fault_activate(host_fault_domain* next)
{
#if defined(__linux__)
    host_fault_domain* old = active_fault_domain;
    if (old == next) return;
    if (old) fault_registrations[old->registration].owner.store(0, std::memory_order_release);
    if (next) fault_registrations[next->registration].owner.store(host_fault_thread_id(), std::memory_order_release);
#endif
    active_fault_domain = next;
}

static uintptr_t host_fault_resume(uintptr_t pc, uintptr_t address)
{
#if defined(__linux__)
    host_fault_domain* d = nullptr;
    unsigned owner = host_fault_thread_id();
    if (!owner) return 0;
    for (auto& slot : fault_registrations) {
        if (slot.owner.load(std::memory_order_acquire) != owner) continue;
        host_fault_domain* candidate = slot.domain.load(std::memory_order_acquire);
        if (candidate && address - candidate->guard < candidate->page_size &&
            pc - candidate->code < candidate->code_size) { d = candidate; break; }
    }
#else
    host_fault_domain* d = active_fault_domain;
#endif
    if (!d || address - d->guard >= d->page_size || pc - d->code >= d->code_size) return 0;
    const host_fault_segment* s = &d->segments[(pc - d->code) / d->segment_size];
    size_t low = 0, high = s->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (s->sites[mid].pc < pc) low = mid + 1; else high = mid;
    }
    if (low == s->count || s->sites[low].pc != pc) return 0;
    if (d->faults < SIG_ATOMIC_MAX) ++d->faults;
    d->pending_segment = (sig_atomic_t)((pc - d->code) / d->segment_size);
    d->pending_site = (sig_atomic_t)(low + 1);
    return s->sites[low].resume;
}
#ifdef _WIN32
static LONG CALLBACK host_fault_handler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        e->ExceptionRecord->NumberParameters < 2 || e->ExceptionRecord->ExceptionInformation[0] != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t resume = host_fault_resume(e->ContextRecord->Rip, e->ExceptionRecord->ExceptionInformation[1]);
    if (!resume) return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Rip = resume;
    return EXCEPTION_CONTINUE_EXECUTION;
}
#elif defined(__linux__)
static void host_fault_handler(int sig, siginfo_t* info, void* context)
{
    ucontext_t* uc = (ucontext_t*)context;
#if defined(__i386__)
    const int pc_register = REG_EIP;
#else
    const int pc_register = REG_RIP;
#endif
    uintptr_t resume = info->si_code > 0 ? host_fault_resume(
        (uintptr_t)uc->uc_mcontext.gregs[pc_register], (uintptr_t)info->si_addr) : 0;
    if (resume) { uc->uc_mcontext.gregs[pc_register] = (greg_t)resume; return; }
    struct sigaction prior = previous_fault_action;
    if ((prior.sa_flags & SA_RESETHAND) && previous_fault_consumed.test_and_set(std::memory_order_relaxed))
        prior.sa_handler = SIG_DFL;
    if (prior.sa_handler == SIG_IGN) return;
    if (prior.sa_handler == SIG_DFL) {
        sigaction(sig, &prior, NULL);
        raise(sig);
    } else if (prior.sa_flags & SA_SIGINFO)
        prior.sa_sigaction(sig, info, context);
    else prior.sa_handler(sig);
}
#endif

static host_fault_domain* host_fault_create(void* code, size_t bytes, size_t segment_bytes, size_t page)
{
/* Keep the existing platform admission. The Linux signal handler uses the
 * registry rather than a dynamic TLS accessor; TLS is ordinary-context only. */
#if defined(_WIN32) || (defined(__linux__) && defined(__GLIBC__))
    const char* enabled = getenv("N64EMU_CPU_HOST_FAULTS");
    if (!enabled || strcmp(enabled, "1")) return NULL;
    host_fault_domain* d = (host_fault_domain*)calloc(1, sizeof(*d));
    if (!d) return NULL;
#if defined(__linux__)
    d->registration = (unsigned)-1;
    /* Resolve syscall's PLT entry before installing a handler, even when a
     * frontend used RTLD_LAZY and the first signal is on a foreign thread. */
    if (!host_fault_thread_id()) { free(d); return NULL; }
#endif
    d->code = (uintptr_t)code; d->code_size = bytes; d->segment_size = segment_bytes;
    d->segment_count = bytes / segment_bytes; d->page_size = page;
    d->segments = (host_fault_segment*)calloc(d->segment_count, sizeof(*d->segments));
#ifdef _WIN32
    d->guard = (uintptr_t)VirtualAlloc(NULL, page, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
#else
    void* guard = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    d->guard = guard == MAP_FAILED ? 0 : (uintptr_t)guard;
#endif
    if (d->segments && d->guard) {
        std::lock_guard<std::mutex> lock(fault_handler_mutex);
        bool available = true;
#if defined(__linux__)
        available = false;
        for (unsigned i = 0; i < sizeof(fault_registrations) / sizeof(fault_registrations[0]); ++i) {
            if (fault_registrations[i].domain.load(std::memory_order_relaxed)) continue;
            d->registration = i;
            fault_registrations[i].owner.store(0, std::memory_order_relaxed);
            fault_registrations[i].domain.store(d, std::memory_order_release);
            available = true;
            break;
        }
#endif
        bool installed = available && fault_handler_users != 0;
        if (available && !installed) {
#ifdef _WIN32
            fault_handler_token = AddVectoredExceptionHandler(1, host_fault_handler);
            installed = fault_handler_token != NULL;
#else
            struct sigaction action = {};
            sigemptyset(&action.sa_mask);
            action.sa_sigaction = host_fault_handler;
            installed = sigaction(SIGSEGV, NULL, &previous_fault_action) == 0;
            previous_fault_consumed.clear(std::memory_order_relaxed);
            action.sa_mask = previous_fault_action.sa_mask;
            action.sa_flags = SA_SIGINFO | (previous_fault_action.sa_flags & (SA_ONSTACK | SA_NODEFER));
            installed = installed && sigaction(SIGSEGV, &action, NULL) == 0;
#endif
        }
        if (installed) { ++fault_handler_users; return d; }
#if defined(__linux__)
        if (available) fault_registrations[d->registration].domain.store(nullptr, std::memory_order_release);
#endif
    }
#ifdef _WIN32
    if (d->guard) VirtualFree((void*)d->guard, 0, MEM_RELEASE);
#else
    if (d->guard) munmap((void*)d->guard, page);
#endif
    free(d->segments); free(d);
#endif
    return NULL;
}
static void host_fault_destroy(host_fault_domain* d)
{
    if (!d) return;
    if (active_fault_domain == d) host_fault_activate(NULL);
    {
        std::lock_guard<std::mutex> lock(fault_handler_mutex);
#if defined(__linux__)
        fault_registrations[d->registration].owner.store(0, std::memory_order_release);
        fault_registrations[d->registration].domain.store(nullptr, std::memory_order_release);
#endif
        if (--fault_handler_users == 0) {
#ifdef _WIN32
            RemoveVectoredExceptionHandler(fault_handler_token);
#elif defined(__linux__)
            struct sigaction current;
            if (sigaction(SIGSEGV, NULL, &current) == 0 &&
                (current.sa_flags & SA_SIGINFO) && current.sa_sigaction == host_fault_handler)
            {
                struct sigaction prior = previous_fault_action;
                if ((prior.sa_flags & SA_RESETHAND) && previous_fault_consumed.test_and_set(std::memory_order_relaxed))
                    prior.sa_handler = SIG_DFL;
                sigaction(SIGSEGV, &prior, NULL);
            }
#endif
        }
    }
#ifdef _WIN32
    VirtualFree((void*)d->guard, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap((void*)d->guard, d->page_size);
#endif
    for (size_t i = 0; i < d->segment_count; ++i) free(d->segments[i].sites);
    free(d->segments); free(d);
}
static bool host_fault_publish(host_fault_domain* d, void* pc, void* resume)
{
    if (!d) return false;
    host_fault_segment* s = &d->segments[((uintptr_t)pc - d->code) / d->segment_size];
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 128;
        void* sites = realloc(s->sites, capacity * sizeof(*s->sites));
        if (!sites) return false;
        s->sites = (host_fault_site*)sites; s->capacity = capacity;
    }
    assert(!s->count || s->sites[s->count-1].pc < (uintptr_t)pc);
    s->sites[s->count++] = {(uintptr_t)pc, (uintptr_t)resume, 0};
    return true;
}
#endif
