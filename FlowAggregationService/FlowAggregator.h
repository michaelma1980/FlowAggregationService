#pragma once

#include <shared_mutex>
#include <tuple>
#include <unordered_map>

#include "boost/json.hpp"
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>

#include "FlowRecord.h"

namespace FlowAggregation
{
	struct FlowTuple
	{
		boost::json::string m_vpcId;
		boost::json::string m_srcApp;
		boost::json::string m_destApp;

		FlowTuple(const boost::json::string& vpcId, const boost::json::string& srcApp, const boost::json::string& destApp)
			:
			m_vpcId(vpcId),
			m_srcApp(srcApp),
			m_destApp(destApp)
		{}

		bool operator==(const FlowTuple& other) const
		{
			return boost::iequals(m_vpcId, other.m_vpcId)
				&& boost::iequals(m_srcApp, other.m_srcApp)
				&& boost::iequals(m_destApp, other.m_destApp);
		}
	};

	struct FlowTupleHash : public std::unary_function<FlowTuple, std::size_t>
	{
		std::size_t operator()(const FlowTuple& k) const;
	};

	class FlowAggregator
	{
	public:

		typedef std::unordered_map<FlowTuple, std::pair<int, int>, FlowTupleHash> TupleToFlowMap;
		typedef std::unordered_map<int, TupleToFlowMap> HourToFlowMap;

		void AddFlow(std::shared_ptr<FlowAggregation::FlowRecordCollection> flows);

		std::shared_ptr<FlowAggregation::FlowRecordCollection> GetFlow(int hour);

	private:
		void AddNewFlow(const std::shared_ptr<FlowAggregation::FlowRecord>& flow, TupleToFlowMap& map);

		HourToFlowMap m_flowTable;

		boost::shared_mutex m_lock;
	};
}
