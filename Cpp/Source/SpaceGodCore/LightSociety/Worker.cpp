// US-T973: Light Society's threads.

#include "LightSociety/Worker.h"

#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		/// <summary>True on a thread running a chunk, so a nested pass runs inline instead of waiting on itself.</summary>
		thread_local bool inChunk = false;
	}

	JobRunner::JobRunner()
	{
		thread = std::thread([this] { Loop(); });
	}

	JobRunner::~JobRunner()
	{
		{
			std::lock_guard<std::mutex> hold(lock);
			quit = true;
			queued = false;
			pending = nullptr;
		}
		cancel.store(true);
		wake.notify_all();
		if (thread.joinable()) thread.join();
	}

	void JobRunner::Submit(Job job)
	{
		Wait();
		{
			std::lock_guard<std::mutex> hold(lock);
			pending = std::move(job);
			queued = true;
		}
		wake.notify_one();
	}

	void JobRunner::Wait()
	{
		std::exception_ptr thrown;
		{
			std::unique_lock<std::mutex> hold(lock);
			done.wait(hold, [this] { return !queued && !running; });
			thrown = std::exchange(error, nullptr);
		}
		if (thrown) std::rethrow_exception(thrown);
	}

	bool JobRunner::Busy() const
	{
		std::lock_guard<std::mutex> hold(lock);
		return queued || running;
	}

	void JobRunner::Loop()
	{
		for (;;)
		{
			Job job;
			{
				std::unique_lock<std::mutex> hold(lock);
				wake.wait(hold, [this] { return quit || queued; });
				if (quit) return;
				job = std::move(pending);
				pending = nullptr;
				queued = false;
				running = true;
				cancel.store(false);
			}
			std::exception_ptr thrown;
			try
			{
				if (job) job(cancel);
			}
			catch (...)
			{
				thrown = std::current_exception();
			}
			{
				std::lock_guard<std::mutex> hold(lock);
				running = false;
				if (thrown && !error) error = thrown;
			}
			done.notify_all();
		}
	}

	ChunkPool::ChunkPool(uint32_t threads)
	{
		Start(threads);
	}

	ChunkPool::~ChunkPool()
	{
		Stop();
	}

	uint32_t ChunkPool::DefaultThreads()
	{
		const uint32_t cores = std::thread::hardware_concurrency();
		return cores > 3 ? cores - 2 : 1;
	}

	void ChunkPool::SetThreads(uint32_t threads)
	{
		std::lock_guard<std::mutex> call(callLock);
		Stop();
		Start(threads);
	}

	void ChunkPool::Start(uint32_t threads)
	{
		const uint32_t total = threads == 0 ? DefaultThreads() : threads;
		uint64_t now = 0;
		{
			std::lock_guard<std::mutex> hold(lock);
			quit = false;
			now = generation;
		}
		helpers.reserve(total - 1);
		for (uint32_t t = 1; t < total; t++) helpers.emplace_back([this, now] { Loop(now); });
	}

	void ChunkPool::Stop()
	{
		{
			std::lock_guard<std::mutex> hold(lock);
			quit = true;
		}
		wake.notify_all();
		for (std::thread& t : helpers)
		{
			if (t.joinable()) t.join();
		}
		helpers.clear();
	}

	void ChunkPool::Loop(uint64_t seen)
	{
		for (;;)
		{
			{
				std::unique_lock<std::mutex> hold(lock);
				wake.wait(hold, [&] { return quit || generation != seen; });
				if (quit) return;
				seen = generation;
			}
			inChunk = true;
			Drain();
			inChunk = false;
			bool last = false;
			{
				std::lock_guard<std::mutex> hold(lock);
				last = --busy == 0;
			}
			if (last) idle.notify_all();
		}
	}

	void ChunkPool::Drain()
	{
		for (;;)
		{
			const uint32_t i = next.fetch_add(1);
			if (i >= jobChunks) return;
			if (failed.load()) continue;
			const uint64_t first = static_cast<uint64_t>(i) * jobChunk;
			const uint64_t last = std::min<uint64_t>(jobCount, first + jobChunk);
			try
			{
				(*jobFn)(i, static_cast<uint32_t>(first), static_cast<uint32_t>(last));
			}
			catch (...)
			{
				std::lock_guard<std::mutex> hold(errorLock);
				if (!error) error = std::current_exception();
				failed.store(true);
			}
		}
	}

	void ChunkPool::ParallelFor(uint32_t count, uint32_t chunk, const ChunkFn& fn)
	{
		if (count == 0 || !fn) return;
		const uint32_t size = std::max<uint32_t>(chunk, 1);
		const uint32_t n = Chunks(count, size);
		if (inChunk || helpers.empty() || n == 1)
		{
			for (uint32_t i = 0; i < n; i++)
			{
				const uint64_t first = static_cast<uint64_t>(i) * size;
				fn(i, static_cast<uint32_t>(first), static_cast<uint32_t>(std::min<uint64_t>(count, first + size)));
			}
			return;
		}

		std::lock_guard<std::mutex> call(callLock);
		{
			std::lock_guard<std::mutex> hold(lock);
			jobFn = &fn;
			jobCount = count;
			jobChunk = size;
			jobChunks = n;
			next.store(0);
			failed.store(false);
			error = nullptr;
			busy = static_cast<uint32_t>(helpers.size());
			generation++;
		}
		wake.notify_all();
		inChunk = true;
		Drain();
		inChunk = false;
		std::exception_ptr thrown;
		{
			std::unique_lock<std::mutex> hold(lock);
			idle.wait(hold, [this] { return busy == 0; });
			jobFn = nullptr;
			thrown = std::exchange(error, nullptr);
		}
		if (thrown) std::rethrow_exception(thrown);
	}
}
