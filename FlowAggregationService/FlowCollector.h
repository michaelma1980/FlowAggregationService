#pragma once
#include <memory>
#include <functional>
#include <semaphore.h>
#include <thread>
#include <condition_variable>

#include "FlowRecord.h"

namespace FlowAggregation
{
	class FlowCollector
	{
	public:
		typedef std::function<void(std::shared_ptr<FlowAggregation::FlowRecordCollection>)> ProcessFlowCallback;
		
		FlowCollector(ProcessFlowCallback callback);

		~FlowCollector();

		void AddFlow(std::shared_ptr<FlowAggregation::FlowRecordCollection> flowRecords);

		void PauseAndDrainFlow();

		void ResumeFlow();

	private:

		static void ProcessFlowsThread(FlowCollector& collector);

		void ProcessFlows();

		static const unsigned int c_maxQueueSize = 4 * 1024 * 1024;
		ProcessFlowCallback m_processFlowCallback;
		std::vector<std::shared_ptr<FlowAggregation::FlowRecordCollection>> m_flowQueue;
		int m_head = 0;
		int m_tail = 0;
		std::mutex m_queueTailMutex;
		std::mutex m_queueHeadMutex;
		sem_t m_countOfFlows;
		sem_t m_countOfEmptyQueueSlots;
		std::unique_ptr<std::thread> m_processFlowThread;
		std::atomic<bool> m_isStopped;
		bool m_isDraining = false;
		std::condition_variable m_isDrainCompleted;
	};
}
