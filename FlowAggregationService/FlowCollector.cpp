#include "FlowCollector.h"
#include "FlowCollector.h"

#include <mutex>
#include <iostream>

#include "InternalError.h"

using namespace std;
using namespace FlowAggregation;

namespace
{
	void PostSemaphore(sem_t* semaphore)
	{
		int ret = sem_post(semaphore);
		if (ret != 0)
		{
			string error = "failed to release semaphore: ";
			error += strerror(errno);
			cerr << error << endl;
			throw InternalError(error);
		}
	}

	void WaitSemaphore(sem_t* semaphore)
	{
		int ret = sem_wait(semaphore);
		if (ret != 0)
		{
			string error = "failed to wait for semaphore: ";
			error += strerror(errno);
			cerr << error << endl;
			throw InternalError(error);
		}
	}
}

void FlowCollector::AddFlow(std::shared_ptr<FlowAggregation::FlowRecordCollection> flowRecords)
{
	WaitSemaphore(&m_countOfEmptyQueueSlots);

	{
		lock_guard<mutex> guard(m_queueTailMutex);
		m_flowQueue[m_tail] = flowRecords;
		++m_tail;
		PostSemaphore(&m_countOfFlows);
	}
}

void FlowCollector::ProcessFlows()
{
	while (true)
	{
		WaitSemaphore(&m_countOfFlows);

		if (m_isStopped)
		{
			cout << "collector stopped" << endl;
			break;
		}

		shared_ptr<FlowRecordCollection> flows;

		flows = m_flowQueue[m_head];

		{
			lock_guard<mutex> guard(m_queueHeadMutex);
			++m_head;
			if (m_isDraining)
			{
				if (m_head == m_tail)
				{
					m_isDrainCompleted.notify_one();
				}
			}
		}

		if (m_processFlowCallback != nullptr)
		{
			m_processFlowCallback(flows);
		}

		PostSemaphore(&m_countOfEmptyQueueSlots);
	}
}

void FlowCollector::ProcessFlowsThread(FlowCollector& collector)
{
	collector.ProcessFlows();	
}

FlowCollector::FlowCollector(ProcessFlowCallback callback)
	:
	m_flowQueue(c_maxQueueSize),
	m_processFlowCallback(callback),
	m_processFlowThread(),
	m_isStopped(false)
{
	sem_init(&m_countOfEmptyQueueSlots, 0, c_maxQueueSize);
	sem_init(&m_countOfFlows, 0, 0);
	m_processFlowThread.reset(new thread(ProcessFlowsThread, ref(*this)));
}

FlowCollector::~FlowCollector()
{
	m_isStopped = true;

	int ret = sem_post(&m_countOfFlows);
	if (ret != 0)
	{
		string error = "failed to release semaphore: ";
		error += strerror(errno);
		cerr << error << endl;
		assert(0);
	}

	m_processFlowThread->join();
	sem_destroy(&m_countOfEmptyQueueSlots);
	sem_destroy(&m_countOfFlows);
}

/// <summary>
/// Block the collector so that no new flow can be added. The function returns when all the flows in the queue are 
/// processed.
/// </summary>
void FlowCollector::PauseAndDrainFlow()
{
	// Lock the queue so that no new flow can enter it.
	m_queueTailMutex.lock();

	// Wait for the queue to be drained.
	unique_lock<mutex> guard(m_queueHeadMutex);
	m_isDraining = true;
	if (m_head != m_tail)
	{
		m_isDrainCompleted.wait(guard);
	}
}

void FlowCollector::ResumeFlow()
{
	{
		unique_lock<mutex> guard(m_queueHeadMutex);
		m_isDraining = false;
	}

	m_queueTailMutex.unlock();
}
