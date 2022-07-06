#pragma once
#include <string>
#include <vector>
#include <memory>
#include "boost/json.hpp"

namespace FlowAggregation
{
	class FlowRecord
	{
	public:
		FlowRecord()
		{
		}

		FlowRecord(
			int hour, 
			const boost::json::string& vpcId, 
			const boost::json::string& srcApp, 
			const boost::json::string& destApp, 
			int bytesSent, 
			int bytesReceived);

		const boost::json::string& GetSrcApp()
		{
			return m_srcApp;
		}

		const boost::json::string& GetDestApp()
		{
			return m_destApp;
		}

		const boost::json::string& GetVpcId()
		{
			return m_vpcId;
		}

		int GetBytesSent()
		{
			return m_bytesSent;
		}

		int GetBytesReceived()
		{
			return m_bytesReceived;
		}

		int GetHour()
		{
			return m_hour;
		}

		bool Parse(const boost::json::value& record);

		bool Parse(const std::string& s);

		std::string ToString() const;

	private:
		boost::json::string m_srcApp;
		boost::json::string m_destApp;
		boost::json::string m_vpcId;
		int m_bytesSent;
		int m_bytesReceived;
		int m_hour;
	};

	class FlowRecordCollection
	{
	public:
		bool Parse(const std::string& s);
		std::string ToString() const;

		std::vector<std::shared_ptr<FlowRecord>>& GetFlowRecords()
		{
			return m_records;
		}

		std::size_t GetSize()
		{
			return m_records.size();
		}

	private:
		std::vector<std::shared_ptr<FlowRecord>> m_records;
	};
}
