#pragma once
// US-T973: Light Society's threads. A JobRunner keeps the society's step off the caller's thread (submit, wait, join on
// destruction); a ChunkPool splits one pass over n items into chunks whose bounds depend only on the chunk size, never on
// the thread count, so with results merged in chunk order one thread and many give the same answer, bit for bit.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>
	/// One background thread that runs one job at a time. A job gets a cancel flag it should poll on long work. An
	/// exception a job throws is kept and rethrown by the next <see cref="Wait"/>. Destruction cancels the running job,
	/// drops one not yet started and joins the thread.
	/// </summary>
	class JobRunner
	{
	public:
		using Job = std::function<void(const std::atomic<bool>& cancelled)>;

		JobRunner();
		~JobRunner();
		JobRunner(const JobRunner&) = delete;
		JobRunner& operator=(const JobRunner&) = delete;

		/// <summary>Waits for the previous job (rethrowing its exception), then starts this one.</summary>
		void Submit(Job job);
		/// <summary>Waits until no job is queued or running; rethrows the exception of a job that threw.</summary>
		void Wait();
		/// <summary>True while a job is queued or running.</summary>
		bool Busy() const;
		/// <summary>Raises the running job's cancel flag; it is lowered again when the next job starts.</summary>
		void Cancel() { cancel.store(true); }

	private:
		void Loop();

		mutable std::mutex lock;
		std::condition_variable wake;
		std::condition_variable done;
		Job pending;
		bool queued = false;
		bool running = false;
		bool quit = false;
		std::exception_ptr error;
		std::atomic<bool> cancel{false};
		std::thread thread;
	};

	/// <summary>
	/// A fixed set of threads (the caller's plus Threads() - 1 helpers) for chunked passes. Not for use from several
	/// threads at once: concurrent calls run one after the other, and a call from inside a chunk runs inline, serially.
	/// </summary>
	class ChunkPool
	{
	public:
		/// <summary>fn(chunkIndex, first, last): one chunk [first, last) of a pass.</summary>
		using ChunkFn = std::function<void(uint32_t chunkIndex, uint32_t first, uint32_t last)>;

		/// <summary>A pool of <paramref name="threads"/> threads, the caller's included; 0 means <see cref="DefaultThreads"/>.</summary>
		explicit ChunkPool(uint32_t threads = 1);
		~ChunkPool();
		ChunkPool(const ChunkPool&) = delete;
		ChunkPool& operator=(const ChunkPool&) = delete;

		/// <summary>The cores less two, at least 1 (ProceduralSky's rule).</summary>
		static uint32_t DefaultThreads();
		/// <summary>Chunks of <paramref name="chunk"/> items (at least 1) that cover <paramref name="count"/> items.</summary>
		static uint32_t Chunks(uint32_t count, uint32_t chunk)
		{
			const uint64_t c = std::max<uint32_t>(chunk, 1);
			return static_cast<uint32_t>((static_cast<uint64_t>(count) + c - 1) / c);
		}

		/// <summary>Sets the thread count (the caller's included; 0 means <see cref="DefaultThreads"/>); 1 runs every pass inline.</summary>
		void SetThreads(uint32_t threads);
		uint32_t Threads() const { return static_cast<uint32_t>(helpers.size()) + 1; }

		/// <summary>
		/// Calls fn for every chunk of [0, count): chunk i is [i * chunk, min(count, (i + 1) * chunk)). Chunks may run at
		/// the same time and in any order, so fn should write only to what its chunk owns. Returns once every chunk is
		/// done; rethrows the first exception a chunk threw (the chunks not yet started are then skipped).
		/// </summary>
		void ParallelFor(uint32_t count, uint32_t chunk, const ChunkFn& fn);

		/// <summary>
		/// Runs work(outs[i], first, last) for every chunk i into its own slot of <paramref name="outs"/> (resized to the
		/// chunk count), then merge(outs[i]) for each in chunk order on the caller's thread.
		/// </summary>
		template <class Out, class Work, class Merge>
		void MapMerge(uint32_t count, uint32_t chunk, std::vector<Out>& outs, Work&& work, Merge&& merge)
		{
			const uint32_t n = Chunks(count, chunk);
			if (outs.size() < n) outs.resize(n);
			ParallelFor(count, chunk, [&](uint32_t i, uint32_t first, uint32_t last) { work(outs[i], first, last); });
			for (uint32_t i = 0; i < n; i++) merge(outs[i]);
		}

	private:
		void Start(uint32_t threads);
		void Stop();
		void Loop(uint64_t seen);
		void Drain();

		std::mutex callLock;
		std::mutex lock;
		std::condition_variable wake;
		std::condition_variable idle;
		std::vector<std::thread> helpers;
		uint64_t generation = 0;
		uint32_t busy = 0;
		bool quit = false;

		// The pass in flight: written under lock before generation moves, read by helpers after they see it move.
		const ChunkFn* jobFn = nullptr;
		uint32_t jobCount = 0;
		uint32_t jobChunk = 1;
		uint32_t jobChunks = 0;
		std::atomic<uint32_t> next{0};
		std::atomic<bool> failed{false};
		std::mutex errorLock;
		std::exception_ptr error;
	};
}
