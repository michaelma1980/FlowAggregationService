// FlowAggregationService.cpp : Defines the entry point for the application.
//

#include <assert.h>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "FlowAggregationService.h"
#include "FlowRecord.h"
#include "FlowCollector.h"
#include "FlowAggregator.h"

using namespace std;
using namespace FlowAggregation;
using namespace boost;

namespace beast = boost::beast;                 // from <boost/beast.hpp>
namespace http = beast::http;                   // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;         // from <boost/beast/websocket.hpp>
namespace net = boost::asio;                    // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>



/// <summary>
/// Unit test
/// </summary>

static const string recordCollectionJson = R"([
												{"src_app":"foo","dest_app":"bar","vpc_id":"vpc-0","bytes_tx":300,"bytes_rx":900,"hour":1},
												{"src_app":"baz","dest_app":"qux","vpc_id":"vpc-0","bytes_tx":100,"bytes_rx":500,"hour":1}
									  		  ])";

void TestAddFlowBlocked(FlowCollector& collectorForPause)
{
	std::shared_ptr<FlowRecordCollection> recordsToAdd = std::make_shared<FlowRecordCollection>();
	recordsToAdd->Parse(recordCollectionJson);
	collectorForPause.AddFlow(recordsToAdd);
}

int UnitTest()
{
	// Test FlowRecord
	FlowRecord record;
	string json;

	{
		string tmp = R"({"src_app":"foo","dest_app":"bar","vpc_id":"vpc-0","bytes_tx":100,"bytes_rx":500,"hour":1})";
		json = tmp;
		assert(record.Parse(tmp) == true);
	}
	
	string s = record.ToString();
	assert(s == json);

	json = R"({"src_app": "foo", "dest_app": "bar", "vpc_id": "vpc-0", "bytes_tx": 100, "bytes_rx": 500, "hour": 1)";
	assert(record.Parse(json) == false);

	json = R"({"dest_app": "bar", "vpc_id": "vpc-0", "bytes_tx": 100, "bytes_rx": 500, "hour": 1})";
	assert(record.Parse(json) == false);

	json = R"({"src_app": "foo", "dest_app": "bar", "vpc_id": "vpc-0", "bytes_tx": "foo", "bytes_rx": 500, "hour": 1})";
	assert(record.Parse(json) == false);

	// Test FlowRecordCollection

	FlowRecordCollection records;
	assert(records.Parse(recordCollectionJson) == true);
	assert(records.GetSize() == 2);

	string recordCollectionJsonOutput = records.ToString();

	// Test FlowCollector
	FlowCollector collector([](std::shared_ptr<FlowRecordCollection> f) { cout << f->ToString() << endl; });
	std::shared_ptr<FlowRecordCollection> recordsToAdd = std::make_shared<FlowRecordCollection>();
	recordsToAdd->Parse(recordCollectionJson);
	collector.AddFlow(recordsToAdd);
	
	// Test PauseAndDrainFlow/ResumeFlow
	FlowCollector collectorForPause([](std::shared_ptr<FlowRecordCollection> f) { cout << f->ToString() << endl; });
	collectorForPause.AddFlow(recordsToAdd);
	collectorForPause.PauseAndDrainFlow();
	std::thread testAddFlowBlockedThread(TestAddFlowBlocked, std::ref(collectorForPause));
	collectorForPause.ResumeFlow();
	testAddFlowBlockedThread.join();

	// Test FlowAggregator
	FlowAggregator aggregator;
	aggregator.AddFlow(recordsToAdd);
	assert(aggregator.GetFlow(1)->GetSize() == 2);
	auto flows = aggregator.GetFlow(1);
	cout << flows->ToString() << endl;
	aggregator.AddFlow(recordsToAdd);
	assert(aggregator.GetFlow(1)->GetSize() == 2);
	flows = aggregator.GetFlow(1);
	cout << flows->ToString() << endl;

	return 0;
}

// End of unit test.


/// <summary>
/// Global variables
/// </summary>

FlowCollector* g_flowCollector = nullptr;
FlowAggregator* g_flowAggregator = nullptr;

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
    class Body, class Allocator,
    class Send>
    void
    handle_request(
        http::request<Body, http::basic_fields<Allocator>>&& req,
        Send&& send)
{
    // Returns a bad request response
    auto const bad_request =
        [&req](beast::string_view why)
    {
        cout << why << endl;
        http::response<http::string_body> res{ http::status::bad_request, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const server_error =
        [&req](beast::string_view what)
    {
        http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if (req.method() != http::verb::get 
        && req.method() != http::verb::head
        && req.method() != http::verb::post)
        return send(bad_request("Unknown HTTP-method"));

    // Request path must be absolute and not contain "..".
    if (req.target().empty() ||
        req.target()[0] != '/')
        return send(bad_request("Illegal request-target"));

    if (req.method() == http::verb::get || req.method() == http::verb::head)
    {
        string_view target = req.target();
        static const string c_flowPath = "/flows?";
        if (target.size() < c_flowPath.size() || target.substr(0, c_flowPath.size()) != c_flowPath)
        {
            string error = " invalid path: ";
            error += string(target.data(), target.size());
            return send(bad_request(error));
        }

        string parameterString(target.substr(c_flowPath.size()));
        vector<string> param;
        boost::split(param, parameterString, boost::is_any_of("="));
        if (param.size() != 2)
        {
            string error = " invalid parameter: " + parameterString;
            return send(bad_request(error));
        }
        else
        {
            string key = param[0];
            if (key != "hour")
            {
                string error = " invalid parameter: " + parameterString;
                return send(bad_request(error));
            }

            string hourString = param[1];
            int hour;
            try
            {
                hour = stol(hourString);
            }
            catch (const std::invalid_argument& e)
            {
                string error = " invalid parameter: "  + parameterString;
                return send(bad_request(error));
            }
            catch (const std::out_of_range& e)
            {
                string error = " invalid parameter: " + parameterString;
                return send(bad_request(error));
            }

            cout << "recieved get request for " << hour << " hour" << endl;
            // To ensure strong consistency we'll need to drain the flow queue and block all the new requests.
            // This can be removed if strong consistency isn't needed.
            g_flowCollector->PauseAndDrainFlow();
            auto flows = g_flowAggregator->GetFlow(hour);            
            string response = (flows == nullptr) ? "" : flows->ToString();
            int responseSize = response.size();

            if (req.method() == http::verb::head)
            {
                http::response<http::empty_body> res{ http::status::ok, req.version() };
                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(http::field::content_type, "text/plain");
                res.content_length(responseSize);
                res.keep_alive(req.keep_alive());
                return send(std::move(res));
            }

            // Respond to GET request
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/plain");
            res.keep_alive(req.keep_alive());
            res.body() = response;
            res.prepare_payload();
            send(std::move(res));

            // This can be removed if strong consistency isn't needed.
            g_flowCollector->ResumeFlow();
            return;
        }
    }
    else if (req.method() == http::verb::post)
    {
        std::shared_ptr<FlowRecordCollection> flows = std::make_shared<FlowRecordCollection>();
        if (!flows->Parse(req.body()))
        {
            string error = " invalid post body: ";
            error += req.body();
            return send(bad_request(error));
        }

        g_flowCollector->AddFlow(flows);

        http::response<http::string_body> res{ http::status::ok, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }
    else
    {
        stringstream error;
        error << " invalid method: " << req.method();
        return send(bad_request(error.str()));
    }
}

//------------------------------------------------------------------------------

// Report a failure
void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

//------------------------------------------------------------------------------

// Handles an HTTP server connection
class http_session : public std::enable_shared_from_this<http_session>
{
    // This queue is used for HTTP pipelining.
    class queue
    {
        enum
        {
            // Maximum number of responses we will queue
            limit = 8
        };

        // The type-erased, saved work item
        struct work
        {
            virtual ~work() = default;
            virtual void operator()() = 0;
        };

        http_session& self_;
        std::vector<std::unique_ptr<work>> items_;

    public:
        explicit
            queue(http_session& self)
            : self_(self)
        {
            static_assert(limit > 0, "queue limit must be positive");
            items_.reserve(limit);
        }

        // Returns `true` if we have reached the queue limit
        bool
            is_full() const
        {
            return items_.size() >= limit;
        }

        // Called when a message finishes sending
        // Returns `true` if the caller should initiate a read
        bool
            on_write()
        {
            BOOST_ASSERT(!items_.empty());
            auto const was_full = is_full();
            items_.erase(items_.begin());
            if (!items_.empty())
                (*items_.front())();
            return was_full;
        }

        // Called by the HTTP handler to send a response.
        template<bool isRequest, class Body, class Fields>
        void
            operator()(http::message<isRequest, Body, Fields>&& msg)
        {
            // This holds a work item
            struct work_impl : work
            {
                http_session& self_;
                http::message<isRequest, Body, Fields> msg_;

                work_impl(
                    http_session& self,
                    http::message<isRequest, Body, Fields>&& msg)
                    : self_(self)
                    , msg_(std::move(msg))
                {
                }

                void
                    operator()()
                {
                    http::async_write(
                        self_.stream_,
                        msg_,
                        beast::bind_front_handler(
                            &http_session::on_write,
                            self_.shared_from_this(),
                            msg_.need_eof()));
                }
            };

            // Allocate and store the work
            items_.push_back(
                boost::make_unique<work_impl>(self_, std::move(msg)));

            // If there was no previous work, start this one
            if (items_.size() == 1)
                (*items_.front())();
        }
    };

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    queue queue_;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::string_body>> parser_;

public:
    // Take ownership of the socket
    http_session(
        tcp::socket&& socket)
        : stream_(std::move(socket))
        , queue_(*this)
    {
    }

    // Start the session
    void
        run()
    {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(
            stream_.get_executor(),
            beast::bind_front_handler(
                &http_session::do_read,
                this->shared_from_this()));
    }


private:
    void
        do_read()
    {
        // Construct a new parser for each message
        parser_.emplace();

        // Apply a reasonable limit to the allowed size
        // of the body in bytes to prevent abuse.
        parser_->body_limit(10000);

        // Set the timeout.
        stream_.expires_after(std::chrono::seconds(30));

        // Read a request using the parser-oriented interface
        http::async_read(
            stream_,
            buffer_,
            *parser_,
            beast::bind_front_handler(
                &http_session::on_read,
                shared_from_this()));
    }

    void
        on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        // This means they closed the connection
        if (ec == http::error::end_of_stream)
            return do_close();

        if (ec)
            return fail(ec, "read");

        // See if it is a WebSocket Upgrade
        if (websocket::is_upgrade(parser_->get()))
        {
            return fail(error_code(system::errc::operation_not_supported, generic_category()), "not supported");
        }

        // Send the response
        handle_request(parser_->release(), queue_);

        // If we aren't at the queue limit, try to pipeline another request
        if (!queue_.is_full())
            do_read();
    }

    void
        on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        if (close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return do_close();
        }

        // Inform the queue that a write completed
        if (queue_.on_write())
        {
            // Read another request
            do_read();
        }
    }

    void
        do_close()
    {
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    listener(
        net::io_context& ioc,
        tcp::endpoint endpoint)
        : ioc_(ioc)
        , acceptor_(net::make_strand(ioc))
    {
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint.protocol(), ec);
        if (ec)
        {
            fail(ec, "open");
            return;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec)
        {
            fail(ec, "set_option");
            return;
        }

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if (ec)
        {
            fail(ec, "bind");
            return;
        }

        // Start listening for connections
        acceptor_.listen(
            net::socket_base::max_listen_connections, ec);
        if (ec)
        {
            fail(ec, "listen");
            return;
        }
    }

    // Start accepting incoming connections
    void
        run()
    {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(
            acceptor_.get_executor(),
            beast::bind_front_handler(
                &listener::do_accept,
                this->shared_from_this()));
    }

private:
    void
        do_accept()
    {
        // The new connection gets its own strand
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &listener::on_accept,
                shared_from_this()));
    }

    void
        on_accept(beast::error_code ec, tcp::socket socket)
    {
        if (ec)
        {
            fail(ec, "accept");
        }
        else
        {
            // Create the http session and run it
            std::make_shared<http_session>(
                std::move(socket))->run();
        }

        // Accept another connection
        do_accept();
    }
};


void AggregateFlow(std::shared_ptr<FlowAggregation::FlowRecordCollection> flows)
{
    g_flowAggregator->AddFlow(flows);
}

int main(int argc, char* argv[])
{
    // Check command line arguments.
    if (argc < 3)
    {
        std::cerr <<
            "Usage: flow-aggregation-service <address> <port> <threads>\n" <<
            "Example:\n" <<
            "    flow-aggregation-service 0.0.0.0 8080 1\n";
        return EXIT_FAILURE;
    }

    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const threads = std::max<int>(1, std::atoi(argv[3]));

    FlowAggregator aggregator;
    g_flowAggregator = &aggregator;

    FlowCollector collector(AggregateFlow);
    g_flowCollector = &collector;

    //auto const address = net::ip::make_address("127.0.0.1");
    //auto const port = static_cast<unsigned short>(8080);
    //auto const threads = std::max<int>(1, std::atoi(argv[3]));

    // The io_context is required for all I/O
    net::io_context ioc{ threads };

    // Create and launch a listening port
    std::make_shared<listener>(
        ioc,
        tcp::endpoint{ address, port })->run();

    // Capture SIGINT and SIGTERM to perform a clean shutdown
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](beast::error_code const&, int)
        {
            // Stop the `io_context`. This will cause `run()`
            // to return immediately, eventually destroying the
            // `io_context` and all of the sockets in it.
            ioc.stop();
        });

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for (auto i = threads - 1; i > 0; --i)
        v.emplace_back(
            [&ioc]
            {
                ioc.run();
            });
    ioc.run();

    // (If we get here, it means we got a SIGINT or SIGTERM)

    // Block until all the threads exit
    for (auto& t : v)
        t.join();

    return EXIT_SUCCESS;
}

