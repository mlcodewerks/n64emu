/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (retro_eventcount.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Eventcount parking from libretro/parallel-n64, commit
 * 3ce14f1be1826b61d9a908f7dc92b8b1532cefd6.
 * Restricted to n64emu's Windows 7+ and Linux hosts, without the upstream
 * condition-variable fallback, TLS events or unused timed-wait API.
 * Failure to resolve a native Windows sleep primitive leaves the pool serial.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "worker_eventcount.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#include <sched.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#endif
#if defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h>
#endif

#if defined(RETRO_ATOMIC_LOCK_FREE) && (!defined(_WIN32) || defined(RETRO_ATOMIC_HAS_PTR))

static void retro_cpu_relax(void)
{
#if defined(_WIN32)
   YieldProcessor();
#elif defined(__i386__) || defined(__x86_64__)
   _mm_pause();
#elif defined(__arm__) || defined(__aarch64__)
   __asm__ __volatile__("yield");
#endif
}

#define EC_SPIN_US 3
#define EC_SPIN_MIN 32
#define EC_SPIN_MAX 8192
static unsigned ec_spin_override(const char *text, unsigned fallback)
{
   char *end;
   unsigned long value;
   while (isspace((unsigned char)*text))
      text++;
   if (*text == '-')
      return fallback;
   errno = 0;
   value = strtoul(text, &end, 0);
   if (end == text)
      return fallback;
   while (isspace((unsigned char)*end))
      end++;
   if (*end)
      return fallback;
   if (errno == ERANGE || value > EC_SPIN_MAX)
      return EC_SPIN_MAX;
   return (unsigned)value;
}

#ifdef _WIN32
#define EC_W_WOKEN 1
#define EC_W_ASLEEP 2
#define EC_HEAD_LOCK ((uintptr_t)1)
#define EC_HEAD_MASK (~EC_HEAD_LOCK)
struct ec_waiter {
   struct ec_waiter *next;
   retro_atomic_int_t flags;
   DWORD tid;
};
typedef LONG (WINAPI *ec_nt_wait_alert_t)(volatile void*, LARGE_INTEGER*);
typedef LONG (WINAPI *ec_nt_alert_tid_t)(HANDLE);
typedef LONG (WINAPI *ec_nt_keyed_t)(HANDLE, void*, BOOLEAN, LARGE_INTEGER*);
typedef LONG (WINAPI *ec_nt_create_keyed_t)(HANDLE*, ULONG, void*, ULONG);
typedef BOOL (WINAPI *ec_wait_address_t)(volatile void*, void*, SIZE_T, DWORD);
typedef void (WINAPI *ec_wake_address_t)(void*);
enum { EC_SLEEP_ALERT = 1, EC_SLEEP_KEYED, EC_SLEEP_ADDRESS };
static struct {
   ec_nt_wait_alert_t wait_alert;
   ec_nt_alert_tid_t alert_tid;
   ec_nt_keyed_t wait_keyed, release_keyed;
   ec_wait_address_t wait_address;
   ec_wake_address_t wake_address;
   HANDLE keyed;
   unsigned spin;
   int sleep, resolved;
} ec_g;
static unsigned ec_spin_for_budget(unsigned budget_us)
{
   const unsigned  samples = 3;
   const unsigned  iters   = 2048;
   LARGE_INTEGER   freq;
   double          best = 0.0;
   unsigned        s, i;

   if (!budget_us)
      return 0;

   if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0)
      return EC_SPIN_MIN * 8;   /* no clock: a middling fixed count */

   for (s = 0; s < samples; s++)
   {
      LARGE_INTEGER t0, t1;
      double        us;

      QueryPerformanceCounter(&t0);
      for (i = 0; i < iters; i++)
         retro_cpu_relax();
      QueryPerformanceCounter(&t1);

      us = (double)(t1.QuadPart - t0.QuadPart) * 1000000.0
         / (double)freq.QuadPart;

      if (us > 0.0 && (best == 0.0 || us < best))
         best = us;
   }

   if (best <= 0.0)
      return EC_SPIN_MIN * 8;

   {
      double n = (double)budget_us * (double)iters / best;

      if (n < (double)EC_SPIN_MIN)
         return EC_SPIN_MIN;
      if (n > (double)EC_SPIN_MAX)
         return EC_SPIN_MAX;
      return (unsigned)n;
   }
}

static void ec_win32_init(void)
{
   HMODULE nt, kernel;
   const char *force, *env;
   unsigned budget = EC_SPIN_US;
   SYSTEM_INFO si;
   if (ec_g.resolved) return;
   ec_g.resolved = 1; /* Pool initialization is owned by the caller. */
   nt = GetModuleHandleA("ntdll.dll");
   /* The synchronization API-set is implemented by KernelBase on modern
    * Windows; Kernel32 does not export these names on every supported OS. */
   kernel = GetModuleHandleA("kernelbase.dll");
   if (!kernel) kernel = GetModuleHandleA("kernel32.dll");
   force = getenv("RETRO_EVENTCOUNT_WIN32");
   /* Windows 8+ can park directly on the epoch, like Linux's futex path.
    * Resolve at runtime so Windows 7 retains the keyed-event implementation.
    * One wake-all operation replaces the per-thread waiter-list walk. */
   if (kernel && !(force && strcmp(force, "address"))) {
      ec_g.wait_address = (ec_wait_address_t)(void (*)(void))GetProcAddress(kernel, "WaitOnAddress");
      ec_g.wake_address = (ec_wake_address_t)(void (*)(void))GetProcAddress(kernel, "WakeByAddressAll");
      if (ec_g.wait_address && ec_g.wake_address)
         ec_g.sleep = EC_SLEEP_ADDRESS;
   }
   if (nt) {
      ec_nt_create_keyed_t create_keyed;
      ec_g.wait_alert = (ec_nt_wait_alert_t)(void (*)(void))GetProcAddress(nt, "NtWaitForAlertByThreadId");
      ec_g.alert_tid = (ec_nt_alert_tid_t)(void (*)(void))GetProcAddress(nt, "NtAlertThreadByThreadId");
      ec_g.wait_keyed = (ec_nt_keyed_t)(void (*)(void))GetProcAddress(nt, "NtWaitForKeyedEvent");
      ec_g.release_keyed = (ec_nt_keyed_t)(void (*)(void))GetProcAddress(nt, "NtReleaseKeyedEvent");
      create_keyed = (ec_nt_create_keyed_t)(void (*)(void))GetProcAddress(nt, "NtCreateKeyedEvent");
      if (!ec_g.sleep && ec_g.wait_alert && ec_g.alert_tid && !(force && strcmp(force, "alert")))
         ec_g.sleep = EC_SLEEP_ALERT;
      else if (!ec_g.sleep && ec_g.wait_keyed && ec_g.release_keyed && create_keyed
            && !(force && strcmp(force, "keyed"))
            && create_keyed(&ec_g.keyed, 0x1f0003, NULL, 0) == 0)
         ec_g.sleep = EC_SLEEP_KEYED;
   }
   GetSystemInfo(&si);
   if ((env = getenv("RETRO_EVENTCOUNT_SPIN_US"))) budget = ec_spin_override(env, budget);
   else if (si.dwNumberOfProcessors <= 1) budget = 0;
   ec_g.spin = ec_spin_for_budget(budget);
   if ((env = getenv("RETRO_EVENTCOUNT_SPIN"))) ec_g.spin = ec_spin_override(env, ec_g.spin);
}

static void ec_wake_one(struct ec_waiter *w)
{
   DWORD tid = w->tid;
   int prev = retro_atomic_fetch_or_int(&w->flags, EC_W_WOKEN);
   /* The flag publishes that all reads of the stack block are complete. */
   if (!(prev & EC_W_ASLEEP)) return;
   if (ec_g.sleep == EC_SLEEP_ALERT)
      ec_g.alert_tid((HANDLE)(uintptr_t)tid);
   else
      ec_g.release_keyed(ec_g.keyed, w, FALSE, NULL);
}

static INLINE uintptr_t ec_head(retro_eventcount_t *ec)
{
   return (uintptr_t)retro_atomic_load_acquire_ptr(&ec->waitlist);
}

static void ec_list_lock(retro_eventcount_t *ec)
{
   unsigned spins = 0;
   for (;;)
   {
      /* The successful CAS acquires the list; polling only tests its bit. */
      uintptr_t old = (uintptr_t)retro_atomic_load_acquire_ptr(&ec->waitlist);
      if (!(old & EC_HEAD_LOCK)
            && retro_atomic_cas_ptr(&ec->waitlist, (void*)old,
               (void*)(old | EC_HEAD_LOCK)))
         return;
      if (++spins < 64)
         retro_cpu_relax();
      else
      {
         Sleep(0);
         spins = 0;
      }
   }
}

static void ec_list_unlock(retro_eventcount_t *ec)
{
   for (;;)
   {
      uintptr_t old = ec_head(ec);
      if (retro_atomic_cas_ptr(&ec->waitlist, (void*)old,
               (void*)(old & EC_HEAD_MASK)))
         return;
   }
}

static bool ec_list_unlink(retro_eventcount_t *ec, struct ec_waiter *w)
{
   for (;;)
   {
      uintptr_t old = ec_head(ec);
      struct ec_waiter *n = (struct ec_waiter*)(old & EC_HEAD_MASK);

      if (n == w)
      {
         if (retro_atomic_cas_ptr(&ec->waitlist, (void*)old,
                  (void*)((uintptr_t)w->next | EC_HEAD_LOCK)))
            return true;
         continue;   /* a push landed in front of it: look again */
      }
      while (n && n->next != w)
         n = n->next;
      if (!n)
         return false;
      n->next = w->next;
      return true;
   }
}

static void ec_list_push(retro_eventcount_t *ec, struct ec_waiter *w)
{
   uintptr_t old;
   do
   {
      old    = ec_head(ec);
      w->next = (struct ec_waiter*)(old & EC_HEAD_MASK);
   } while (!retro_atomic_cas_ptr(&ec->waitlist, (void*)old,
            (void*)((uintptr_t)w | (old & EC_HEAD_LOCK))));
}

static void ec_wake_all(retro_eventcount_t *ec)
{
   struct ec_waiter *w;
   uintptr_t         old;

   if (!(ec_head(ec) & EC_HEAD_MASK))
      return;

   ec_list_lock(ec);
   /* Take the whole list, dropping the lock in the same swap.  A push
    * does not take the list lock -- it only preserves the bit -- so
    * reading the head and then storing over it would drop any block
    * that landed in between, and that block would never be woken. */
   do
   {
      old = ec_head(ec);
   } while (!retro_atomic_cas_ptr(&ec->waitlist, (void*)old, NULL));

   w = (struct ec_waiter*)(old & EC_HEAD_MASK);
   while (w)
   {
      struct ec_waiter *next = w->next;
      ec_wake_one(w);
      w = next;
   }
}

static void ec_win32_park(retro_eventcount_t *ec, int key)
{
   struct ec_waiter w;
   unsigned i;
   w.next = NULL;
   w.tid = GetCurrentThreadId();
   retro_atomic_int_init(&w.flags, 0);
   ec_list_push(ec, &w);
   if (retro_atomic_load_acquire_int(&ec->epoch) != key) {
      bool unlinked;
      ec_list_lock(ec);
      unlinked = ec_list_unlink(ec, &w);
      ec_list_unlock(ec);
      /* A detached block belongs to its waker until WOKEN is published. */
      if (!unlinked)
         while (!(retro_atomic_load_acquire_int(&w.flags) & EC_W_WOKEN)) retro_cpu_relax();
      return;
   }
   for (i = 0; i < ec_g.spin; ++i) {
      if (retro_atomic_load_acquire_int(&w.flags) & EC_W_WOKEN) return;
      retro_cpu_relax();
   }
   if (retro_atomic_fetch_or_int(&w.flags, EC_W_ASLEEP) & EC_W_WOKEN) return;
   /* Alert waits can return spuriously. A keyed release is a rendezvous,
    * so never retire the block until its matching wake has been consumed. */
   do {
      if (ec_g.sleep == EC_SLEEP_ALERT) ec_g.wait_alert(&w.flags, NULL);
      else ec_g.wait_keyed(ec_g.keyed, &w, FALSE, NULL);
   } while (!(retro_atomic_load_acquire_int(&w.flags) & EC_W_WOKEN));
}
#else
static retro_atomic_int_t ec_linux_spin_state;
static unsigned ec_linux_spin;
static unsigned ec_linux_spin_for_budget(unsigned budget_us)
{
   const unsigned  samples = 3;
   const unsigned  iters   = 2048;
   double          best    = 0.0;
   unsigned        s, i;

   if (!budget_us)
      return 0;

   for (s = 0; s < samples; s++)
   {
      struct timespec t0, t1;
      double          us;

      if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0)
         return EC_SPIN_MIN * 8;   /* no clock: a middling fixed count */
      for (i = 0; i < iters; i++)
         retro_cpu_relax();
      clock_gettime(CLOCK_MONOTONIC, &t1);

      us = (double)(t1.tv_sec - t0.tv_sec) * 1000000.0
         + (double)(t1.tv_nsec - t0.tv_nsec) / 1000.0;

      if (us > 0.0 && (best == 0.0 || us < best))
         best = us;
   }

   if (best <= 0.0)
      return EC_SPIN_MIN * 8;

   {
      double n = (double)budget_us * (double)iters / best;

      if (n < (double)EC_SPIN_MIN)
         return EC_SPIN_MIN;
      if (n > (double)EC_SPIN_MAX)
         return EC_SPIN_MAX;
      return (unsigned)n;
   }
}

static void ec_linux_spin_init(void)
{
   if (retro_atomic_load_acquire_int(&ec_linux_spin_state) == 2)
      return;
   if (retro_atomic_cas_int(&ec_linux_spin_state, 0, 1))
   {
      const char *env;
      unsigned    budget = EC_SPIN_US;
      long        cpus   = sysconf(_SC_NPROCESSORS_ONLN);

      /* An asked-for budget is honoured whatever the processor count;
       * otherwise one processor does not spin. */
      if ((env = getenv("RETRO_EVENTCOUNT_SPIN_US")))
         budget = ec_spin_override(env, budget);
      else if (cpus <= 1)
         budget = 0;

      ec_linux_spin = ec_linux_spin_for_budget(budget);

      if ((env = getenv("RETRO_EVENTCOUNT_SPIN")))
         ec_linux_spin = ec_spin_override(env, ec_linux_spin);

      retro_atomic_store_release_int(&ec_linux_spin_state, 2);
      return;
   }
   while (retro_atomic_load_acquire_int(&ec_linux_spin_state) != 2)
      retro_cpu_relax();
}

#endif

bool retro_eventcount_init(retro_eventcount_t *ec)
{
   memset(ec, 0, sizeof(*ec));
   retro_atomic_int_init(&ec->epoch, 0);
   retro_atomic_int_init(&ec->waiters, 0);
#ifdef _WIN32
   retro_atomic_ptr_init(&ec->waitlist, NULL);
   ec_win32_init();
   return ec_g.sleep != 0;
#else
   ec_linux_spin_init();
   return true;
#endif
}

void retro_eventcount_free(retro_eventcount_t *ec) { (void)ec; }

void retro_eventcount_notify(retro_eventcount_t *ec)
{
   /* These sequentially consistent pairs share a total order with prepare:
    * a notifier cannot miss a waiter that still observes the old epoch. */
   retro_atomic_fetch_add_seq_cst_int(&ec->epoch, 1);
   if (retro_atomic_load_seq_cst_int(&ec->waiters) == 0) return;
#ifdef _WIN32
   if (ec_g.sleep == EC_SLEEP_ADDRESS)
      ec_g.wake_address((void*)&ec->epoch);
   else
      ec_wake_all(ec);
#else
   syscall(SYS_futex, (void*)&ec->epoch, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
#endif
}

int retro_eventcount_prepare_wait(retro_eventcount_t *ec)
{
   retro_atomic_fetch_add_seq_cst_int(&ec->waiters, 1);
   return retro_atomic_load_seq_cst_int(&ec->epoch);
}

void retro_eventcount_cancel_wait(retro_eventcount_t *ec)
{ retro_atomic_fetch_sub_int(&ec->waiters, 1); }

void retro_eventcount_commit_wait(retro_eventcount_t *ec, int key)
{
#ifdef _WIN32
   if (ec_g.sleep == EC_SLEEP_ADDRESS)
   {
      unsigned i;
      for (i = 0; i < ec_g.spin; ++i) {
         if (retro_atomic_load_acquire_int(&ec->epoch) != key) break;
         retro_cpu_relax();
      }
      /* WaitOnAddress compares atomically with parking. A notify between the
       * last load and this call therefore cannot be lost. Spurious returns
       * are handled by the caller's existing predicate loop. */
      if (retro_atomic_load_acquire_int(&ec->epoch) == key)
         ec_g.wait_address((void*)&ec->epoch, &key, sizeof(key), INFINITE);
   }
   else if (retro_atomic_load_acquire_int(&ec->epoch) == key)
      ec_win32_park(ec, key);
#else
   unsigned i;
   for (i = 0; i < ec_linux_spin; ++i) {
      if (retro_atomic_load_acquire_int(&ec->epoch) != key) break;
      retro_cpu_relax();
   }
   if (retro_atomic_load_acquire_int(&ec->epoch) == key)
      syscall(SYS_futex, (void*)&ec->epoch, FUTEX_WAIT_PRIVATE, key, NULL, NULL, 0);
#endif
   retro_atomic_fetch_sub_int(&ec->waiters, 1);
}

const char *retro_eventcount_backend_name(void)
{
#ifdef _WIN32
   ec_win32_init();
   return ec_g.sleep == EC_SLEEP_ADDRESS ? "WaitOnAddress"
        : ec_g.sleep == EC_SLEEP_ALERT ? "ntdll alert-by-thread-id"
        : ec_g.sleep == EC_SLEEP_KEYED ? "ntdll keyed event" : "unavailable (serial)";
#else
   return "futex";
#endif
}
#else
/* No native atomic handshake: init refuses workers and the pool stays serial. */
bool retro_eventcount_init(retro_eventcount_t *ec) { (void)ec; return false; }
void retro_eventcount_free(retro_eventcount_t *ec) { (void)ec; }
void retro_eventcount_notify(retro_eventcount_t *ec) { (void)ec; }
int retro_eventcount_prepare_wait(retro_eventcount_t *ec) { (void)ec; return 0; }
void retro_eventcount_cancel_wait(retro_eventcount_t *ec) { (void)ec; }
void retro_eventcount_commit_wait(retro_eventcount_t *ec, int key) { (void)ec; (void)key; }
const char *retro_eventcount_backend_name(void) { return "unavailable (serial)"; }
#endif
