#include "FlowAggregator.h"

#include <boost/thread.hpp>
#include <mutex>

using namespace std;
namespace json = boost::json;

using namespace FlowAggregation;

void FlowAggregator::AddNewFlow(const std::shared_ptr<FlowRecord>& flow, FlowAggregator::TupleToFlowMap& map)
{
	const json::string& vpcId = flow->GetVpcId();
	const json::string& srcApp = flow->GetSrcApp();
	const json::string& destApp = flow->GetDestApp();

	FlowTuple key(vpcId, srcApp, destApp);
	pair<int, int>& flowStat = map[key];
	flowStat.first = flow->GetBytesSent();
	flowStat.second = flow->GetBytesReceived();
}

void FlowAggregator::AddFlow(std::shared_ptr<FlowRecordCollection> flows)
{
	boost::unique_lock<boost::shared_mutex> lock(m_lock);
	for (const auto& flow : flows->GetFlowRecords())
	{
		int hour = flow->GetHour();
		auto it1 = m_flowTable.find(hour);
		if (it1 != m_flowTable.end())
		{
			TupleToFlowMap& map = it1->second;
			const json::string& vpcId = flow->GetVpcId();
			const json::string& srcApp = flow->GetSrcApp();
			const json::string& destApp = flow->GetDestApp();

			FlowTuple key(vpcId, srcApp, destApp);
			const auto it2 = map.find(key);
			if (it2 != map.end())
			{
				pair<int, int>& flowStat = it2->second;
				flowStat.first += flow->GetBytesSent();
				flowStat.second += flow->GetBytesReceived();
			}
			else
			{
				AddNewFlow(flow, map);
			}
		}
		else
		{
			AddNewFlow(flow, m_flowTable[hour]);
		}
	}
}

std::shared_ptr<FlowRecordCollection> FlowAggregator::GetFlow(int hour)
{
	boost::shared_lock<boost::shared_mutex> lock(m_lock);
	const auto it1 = m_flowTable.find(hour);
	if (it1 == m_flowTable.end())
	{
		return nullptr;
	}


	TupleToFlowMap& map = it1->second;

	std::shared_ptr<FlowRecordCollection> result = make_shared<FlowRecordCollection>();

	for (const auto& tupleToFlow : map)
	{
		const FlowTuple& key = tupleToFlow.first;
		const pair<int, int>& value = tupleToFlow.second;
		std::shared_ptr<FlowRecord> flow = make_shared<FlowRecord>(hour, key.m_vpcId, key.m_srcApp, key.m_destApp, value.first, value.second);
		result->GetFlowRecords().push_back(flow);
	}

	return result;
}

std::size_t FlowTupleHash::operator()(const FlowTuple& k) const
{
	return std::hash<json::string>()(k.m_vpcId)
		^ std::hash<json::string>()(k.m_srcApp)
		^ std::hash<json::string>()(k.m_destApp);
}