// Minimal stand-in for the Microsoft Parallel Patterns Library (<ppl.h>).
//
// Only the handful of facilities Crinkler actually uses are provided:
//   concurrency::parallel_for
//   concurrency::critical_section (+ ::scoped_lock)
//   concurrency::combinable<T>
//
// This header is only on the include path for the MinGW cross build; the MSVC
// build keeps using the real PPL.
//
// Like PPL, parallel_for runs on a persistent pool of worker threads, since
// Crinkler dispatches a great many very short parallel regions.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#define CRINKLER_PPL_PAUSE() _mm_pause()
#define CRINKLER_PPL_HAS_RDTSC 1
#else
#define CRINKLER_PPL_PAUSE() ((void)0)
#endif

namespace concurrency {
namespace detail {

inline unsigned HardwareThreads()
{
	// CRINKLER_THREADS overrides the pool size.
	if (const char* env = std::getenv("CRINKLER_THREADS")) {
		int n = std::atoi(env);
		if (n > 0) return (unsigned)n;
	}
	unsigned n = std::thread::hardware_concurrency();
	return n ? n : 1;
}

// A cheap monotonic tick count. Only ratios of these matter, never their unit,
// so the time stamp counter is preferable to a real clock: it is read inline,
// where QueryPerformanceCounter is a call costly enough to swamp the regions
// being measured.
inline unsigned long long Ticks()
{
#ifdef CRINKLER_PPL_HAS_RDTSC
	return __rdtsc();
#else
	return (unsigned long long)std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

// True while this thread is inside a parallel region. Nested parallel_for
// calls run serially rather than re-entering the pool, which keeps the outer
// loop's workers from waiting on themselves.
inline thread_local bool t_in_parallel = false;

// A parallel region, type-erased without the allocation std::function might do.
struct Task {
	virtual void Run(long long index) const = 0;
protected:
	~Task() = default;
};

// Per-call-site record of what a region costs run inline versus handed to the
// pool, so the choice can be measured rather than assumed. Which one wins
// depends on how expensive the hand-off is, and that varies by orders of
// magnitude between running natively and running under emulation.
class RegionStats {
	// Re-check the mode not in use this often, at a fixed rate: left unmeasured
	// it goes stale, and a stale estimate wins comparisons it should lose,
	// switching the call site into the slower mode until the next probe.
	static constexpr unsigned PROBE_INTERVAL = 256;
	static constexpr long long UNMEASURED = -1;

	// Ticks per 1024 items; integral to keep the atomics lock-free everywhere.
	// [0] is inline, [1] is pooled.
	std::atomic<long long> m_cost[2];
	std::atomic<unsigned> m_calls{0};

public:
	RegionStats()
	{
		m_cost[0].store(UNMEASURED, std::memory_order_relaxed);
		m_cost[1].store(UNMEASURED, std::memory_order_relaxed);
	}

	bool ShouldDispatch()
	{
		long long inline_cost = m_cost[0].load(std::memory_order_relaxed);
		long long pool_cost = m_cost[1].load(std::memory_order_relaxed);

		if (pool_cost == UNMEASURED) return true;
		if (inline_cost == UNMEASURED) return false;

		bool pool_is_better = pool_cost < inline_cost;
		unsigned n = m_calls.fetch_add(1, std::memory_order_relaxed);
		if (n % PROBE_INTERVAL == 0) return !pool_is_better;  // Probe the other one.
		return pool_is_better;
	}

	void Record(bool dispatched, long long count, unsigned long long elapsed)
	{
		long long cost = (long long)(elapsed * 1024 / (unsigned long long)count);
		std::atomic<long long>& slot = m_cost[dispatched ? 1 : 0];
		long long previous = slot.load(std::memory_order_relaxed);
		// Exponential moving average, so one descheduled region cannot pin the
		// decision for good.
		slot.store(previous == UNMEASURED ? cost : (previous * 7 + cost) / 8,
		           std::memory_order_relaxed);
	}
};

class ThreadPool {
	// Idle workers spin on the generation counter before going to sleep, so
	// that dispatching a region does not cost a mutex/condvar handshake.
	static constexpr unsigned SPINS_BEFORE_SLEEPING = 4000;
	// Work is claimed in blocks, so that short regions do not spend their whole
	// run time on the shared atomic, with enough blocks per worker left to even
	// out uneven items.
	static constexpr long long BLOCKS_PER_WORKER = 4;

	std::mutex m_mutex;
	std::condition_variable m_work_available;
	std::vector<std::thread> m_workers;

	std::atomic<unsigned long long> m_generation{0};
	std::atomic<long long> m_next{0};
	std::atomic<unsigned> m_busy_workers{0};
	std::atomic<unsigned> m_sleepers{0};
	std::atomic<bool> m_shutdown{false};
	const Task* m_task = nullptr;
	long long m_count = 0;
	long long m_block = 1;

	void Worker()
	{
		t_in_parallel = true;
		unsigned long long seen_generation = 0;
		for (;;) {
			if (!AwaitGeneration(seen_generation)) return;
			seen_generation = m_generation.load(std::memory_order_acquire);
			RunChunks(*m_task, m_count, m_block);
			m_busy_workers.fetch_sub(1, std::memory_order_release);
		}
	}

	// Waits for a generation newer than `seen`. False means shut down.
	bool AwaitGeneration(unsigned long long seen)
	{
		for (unsigned spins = 0; ; spins++) {
			if (m_shutdown.load(std::memory_order_acquire)) return false;
			if (m_generation.load(std::memory_order_acquire) != seen) return true;

			if (spins < SPINS_BEFORE_SLEEPING) {
				CRINKLER_PPL_PAUSE();
				continue;
			}
			// Long idle: sleep, but wake periodically so a missed
			// notification can never wedge the pool.
			std::unique_lock<std::mutex> lock(m_mutex);
			m_sleepers.fetch_add(1, std::memory_order_relaxed);
			m_work_available.wait_for(lock, std::chrono::milliseconds(2), [&] {
				return m_shutdown.load(std::memory_order_acquire)
					|| m_generation.load(std::memory_order_acquire) != seen;
			});
			m_sleepers.fetch_sub(1, std::memory_order_relaxed);
			spins = 0;
		}
	}

	void RunChunks(const Task& task, long long count, long long block)
	{
		for (;;) {
			long long begin = m_next.fetch_add(block, std::memory_order_relaxed);
			if (begin >= count) break;
			long long end = std::min(begin + block, count);
			for (long long i = begin; i < end; i++) task.Run(i);
		}
	}

public:
	ThreadPool()
	{
		unsigned workers = HardwareThreads();
		m_workers.reserve(workers > 0 ? workers - 1 : 0);
		for (unsigned i = 1; i < workers; i++) {
			m_workers.emplace_back([this] { Worker(); });
		}
	}

	~ThreadPool()
	{
		m_shutdown.store(true, std::memory_order_release);
		{
			std::lock_guard<std::mutex> guard(m_mutex);
		}
		m_work_available.notify_all();
		for (std::thread& worker : m_workers) worker.join();
	}

	void Run(long long count, const Task& task, RegionStats& stats)
	{
		if (count <= 0) return;

		// A nested region has no pool to go to: the workers are already busy
		// with the enclosing one.
		if (t_in_parallel || m_workers.empty()) {
			RunSerial(count, task);
			return;
		}

		const bool dispatch = stats.ShouldDispatch();
		const unsigned long long start = Ticks();
		if (dispatch) RunDispatched(count, task); else RunSerial(count, task);
		stats.Record(dispatch, count, Ticks() - start);
	}

private:
	void RunSerial(long long count, const Task& task)
	{
		for (long long i = 0; i < count; i++) task.Run(i);
	}

	void RunDispatched(long long count, const Task& task)
	{
		m_task = &task;
		m_count = count;
		m_block = std::max<long long>(
			1, count / ((long long)(m_workers.size() + 1) * BLOCKS_PER_WORKER));
		m_next.store(0, std::memory_order_relaxed);
		m_busy_workers.store((unsigned)m_workers.size(), std::memory_order_relaxed);
		m_generation.fetch_add(1, std::memory_order_release);

		if (m_sleepers.load(std::memory_order_relaxed) != 0) {
			std::lock_guard<std::mutex> guard(m_mutex);
			m_work_available.notify_all();
		}

		// The calling thread works too, then waits for the stragglers. They
		// are running, not idle, so spinning is the right way to wait.
		t_in_parallel = true;
		RunChunks(task, count, m_block);
		t_in_parallel = false;

		while (m_busy_workers.load(std::memory_order_acquire) != 0) {
			CRINKLER_PPL_PAUSE();
		}
		m_task = nullptr;
	}

public:
};

inline ThreadPool& Pool()
{
	static ThreadPool pool;
	return pool;
}

}  // namespace detail

// parallel_for(first, last, body) - body is called once for every index in
// [first, last), possibly on several threads at once. Indices are handed out
// dynamically, matching PPL's behaviour for unbalanced work loads.
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& body)
{
	if (last <= first) return;

	struct IndexedTask : detail::Task {
		const Function& body;
		Index first;
		IndexedTask(const Function& b, Index f) : body(b), first(f) {}
		void Run(long long i) const override { body((Index)((long long)first + i)); }
	} task(body, first);

	// One instantiation per lambda type, so this is per call site.
	static detail::RegionStats stats;

	detail::Pool().Run((long long)last - (long long)first, task, stats);
}

class critical_section {
	std::recursive_mutex m_mutex;

public:
	critical_section() = default;
	critical_section(const critical_section&) = delete;
	critical_section& operator=(const critical_section&) = delete;

	void lock() { m_mutex.lock(); }
	void unlock() { m_mutex.unlock(); }
	bool try_lock() { return m_mutex.try_lock(); }

	class scoped_lock {
		critical_section& m_cs;

	public:
		explicit scoped_lock(critical_section& cs) : m_cs(cs) { m_cs.lock(); }
		~scoped_lock() { m_cs.unlock(); }
		scoped_lock(const scoped_lock&) = delete;
		scoped_lock& operator=(const scoped_lock&) = delete;
	};
};

// combinable<T> - one sub-computation per thread, merged on demand.
template <typename T>
class combinable {
	std::mutex m_mutex;
	std::list<std::pair<std::thread::id, T>> m_locals;
	std::function<T()> m_init;

public:
	combinable() : m_init([] { return T(); }) {}
	template <typename Init>
	explicit combinable(Init init) : m_init(init) {}
	combinable(const combinable&) = delete;
	combinable& operator=(const combinable&) = delete;

	T& local()
	{
		std::thread::id id = std::this_thread::get_id();
		std::lock_guard<std::mutex> guard(m_mutex);
		for (auto& entry : m_locals) {
			if (entry.first == id) return entry.second;
		}
		m_locals.emplace_back(id, m_init());
		return m_locals.back().second;
	}

	T& local(bool& exists)
	{
		std::thread::id id = std::this_thread::get_id();
		std::lock_guard<std::mutex> guard(m_mutex);
		for (auto& entry : m_locals) {
			if (entry.first == id) { exists = true; return entry.second; }
		}
		exists = false;
		m_locals.emplace_back(id, m_init());
		return m_locals.back().second;
	}

	template <typename Combine>
	T combine(Combine combine_fn)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		T result = m_init();
		for (auto& entry : m_locals) result = combine_fn(result, entry.second);
		return result;
	}

	template <typename Function>
	void combine_each(Function fn)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		for (auto& entry : m_locals) fn(entry.second);
	}

	void clear()
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_locals.clear();
	}
};

}  // namespace concurrency

namespace Concurrency = concurrency;
