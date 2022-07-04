#include "FlowRecord.h"
#include "boost/json.hpp"
#include <iostream>
#include <memory>

using namespace std;
using namespace FlowAggregation;
namespace json = boost::json;

namespace 
{

	bool ParseStringValue(const json::object& root, const string& key, json::string& value)
	{
		if (!root.contains(key))
		{
			cerr << "json parsing failed, missing key: " << key << endl;
			return false;
		}

		if (root.at(key).kind() != json::kind::string)
		{
			cerr << "json parsing failed, key contains invalid value: " << key << endl;
			return false;
		}

		value = root.at(key).as_string();
		return true;
	}

	bool ParseIntValue(const json::object& root, const string& key, int& value)
	{
		if (!root.contains(key))
		{
			cerr << "json parsing failed, missing key: " << key << endl;
			return false;
		}

		if (root.at(key).kind() != json::kind::int64)
		{
			cerr << "json parsing failed, key contains invalid value: " << key << endl;
			return false;
		}

		value = static_cast<int>(root.at(key).as_int64());
		return true;
	}
}

bool FlowRecord::Parse(const json::value& record)
{
	try
	{
		if (record.kind() != json::kind::object)
		{
			cerr << "json parsing failed: type isn't object" << endl;
			return false;
		}

		const auto& fields = record.get_object();
		if (!ParseStringValue(fields, "src_app", m_srcApp))
		{
			return false;
		}

		if (!ParseStringValue(fields, "dest_app", m_destApp))
		{
			return false;
		}

		if (!ParseStringValue(fields, "vpc_id", m_vpcId))
		{
			return false;
		}

		if (!ParseIntValue(fields, "bytes_tx", m_bytesSent))
		{
			return false;
		}

		if (!ParseIntValue(fields, "bytes_rx", m_bytesReceived))
		{
			return false;
		}

		if (!ParseIntValue(fields, "hour", m_hour))
		{
			return false;
		}
	}
	catch (const invalid_argument& e)
	{
		cerr << "json parsing failed: " << e.what() << endl;
		return false;
	}

	return true;
}

bool FlowRecord::Parse(const std::string& s)
{
	try
	{
		json::error_code ec;
		json::value record = json::parse(s, ec);
		if (ec)
		{
			cerr << "json parsing failed: " << ec.message() << endl;
			return false;
		}

		if (!Parse(record))
		{
			return false;
		}

		return true;
	}
	catch (const bad_alloc& e)
	{
		cerr << "json parsing failed: " << e.what() << endl;
		return false;
	}


	return true;
}

string FlowRecord::ToString() const
{
	json::value record = {
		{ "src_app", m_srcApp },
		{ "dest_app", m_destApp },
		{ "vpc_id", m_vpcId },
		{ "bytes_tx", m_bytesSent },
		{ "bytes_rx", m_bytesReceived },
		{ "hour", m_hour }
	};

	return json::serialize(record);
}

bool FlowRecordCollection::Parse(const std::string& s)
{
	try
	{
		json::error_code ec;
		json::value recordCollection = json::parse(s, ec);
		if (ec)
		{
			cerr << "json parsing failed: " << ec.message() << endl;
			return false;
		}

		if (recordCollection.kind() != json::kind::array)
		{
			cerr << "json parsing failed: " << ec.message() << endl;
			return false;
		}

		const auto& records = recordCollection.as_array();
		for (const auto& record : records)
		{
			shared_ptr<FlowRecord> r = make_shared<FlowRecord>();
			if (!r->Parse(record))
			{
				return false;
			}

			m_records.push_back(r);
		}

		return true;
	}
	catch (const bad_alloc& e)
	{
		cerr << "json parsing failed: " << e.what() << endl;
		return false;
	}
	catch (const invalid_argument& e)
	{
		cerr << "json parsing failed: " << e.what() << endl;
		return false;
	}

	return true;
}

string FlowRecordCollection::ToString() const
{
	string result = "[";
	for (const auto& record : m_records)
	{
		result += record->ToString();
	}

	result += "]\n";
	return result;
}

FlowRecord::FlowRecord(
	int hour, 
	const boost::json::string& vpcId, 
	const boost::json::string& srcApp, 
	const boost::json::string& destApp, 
	int bytesSent, 
	int bytesReceived)
	:
	m_hour(hour),
	m_vpcId(vpcId.c_str()),
	m_srcApp(srcApp.c_str()),
	m_destApp(destApp.c_str()),
	m_bytesSent(bytesSent),
	m_bytesReceived(bytesReceived)
{
}