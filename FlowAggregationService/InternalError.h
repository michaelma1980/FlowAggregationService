#pragma once

#include <exception>

struct InternalError : public std::exception
{
public:
	InternalError(const std::string& error) : m_error(error)
	{
	}

	const char* what() const noexcept override
	{
		return m_error.c_str();
	}

private:
	std::string m_error;
};

