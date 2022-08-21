#pragma once

#include <cassert>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <readerwritercircularbuffer.h>
#include <readerwriterqueue.h>

// Thread pool. Interface to queue work and evaluate results is not thread safe,
// all work needs to be submitted from a single thread.
class IOThreadPool {
public:
	using RetF = std::function<void()>;
	using OptRetF = std::optional<RetF>;
	using WorkF = std::function<OptRetF()>;

	class Handle {
		constexpr explicit Handle(unsigned id) : threadId{id} {}
		const unsigned threadId;
		friend IOThreadPool;
	};

	// Creates a new thread pool with poolSize threads. Each thread has
	// a constant size work queue with workQueueSlots items available.
	IOThreadPool(unsigned poolSize, unsigned workQueueSlots);
	~IOThreadPool();

	// Executes functions returned from the finished submitted work.
	void pullResults();

	// Pulls all remaining results and closes threads. Automatically called
	// by destructor. All other function calls on thread pool after call to
	// finish have undefined behavior.
	void finish();

	// Submits more work to the queue. All work submitted for the same
	// handle will execute synchronously in the FIFO order.
	// The call blocks if the work queue is full.
	void submit(Handle handle, WorkF&& work) {
		assert(!threads.empty());
		sendQ[handle.threadId].wait_enqueue(work);
	}

	// Creates a new work queue handle. Provides functionality analogous to
	// strand in asio/Networking TS.
	Handle getHandle() {
		assert(!threads.empty());
		return Handle(threadDist(randomGen));
	}
private:
	struct Close {};

	void worker(unsigned id);

	std::vector<std::thread> threads;
	std::vector<moodycamel::BlockingReaderWriterCircularBuffer<std::variant<Close, WorkF>>> sendQ;
	std::vector<moodycamel::BlockingReaderWriterQueue<std::variant<Close, RetF>>> recvQ;
	std::default_random_engine randomGen;
	std::uniform_int_distribution<> threadDist;
};
