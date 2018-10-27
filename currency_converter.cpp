// Osman Zakir 
// 3 / 16 / 2018 
// Google Maps GUI + Currency Converter Web Application 
// This application uses a Google Map as its UI.  The user's geolocation is taken if the browser has permission, and an info window 
// is opened at that location which displays a HTML form.  The form has an input element to type in the amount of money in the base 
// currency to convert, two dropdown menus populated with a list of currencies requested from the currency API, and a button to submit 
// the form. By default, the base currency is USD and the resulting currency is the currency used at the place that the info window is 
// opened on.

// Google's Geocoder and Reverse Geocoding Service returns status "ZERO_RESULTS" for Western Sahara, Wake Island, and Kosovo. The second
// dropdown menu switches to AED in that situation. The status means that there are no results to show even though reverse geocodng did work.

// This C++ application is the web server for the application. It acts as both a server and a client, as it also has to query the currency API,
// currencylayer.com, on its currency conversion endpoint and get the conversion result to return to the front-end code.  It also holds two
// environment variables, one to hold the Google Maps API Key and the other to hold the currencylayer.com API access key.  

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <cstdlib>
#include <map>
#include <cctype>
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include <jinja2cpp/template.h>
#include <jinja2cpp/value.h>
#include <jinja2cpp/template_env.h>

using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using nlohmann::json;					// from <nlohmann/json.hpp>
namespace http = boost::beast::http;    // from <boost/beast/http.hpp>

										//------------------------------------------------------------------------------

										// Function to return a reasonable mime type based on the extension of a file.
boost::beast::string_view mime_type(boost::beast::string_view path);

// This class represents a cache for storing results from the
// currency exchange API used by currencylayer.com
class cache_storage
{
public:
	cache_storage(const std::chrono::seconds &duration)
		: m_cache{}, m_duration{ duration }
	{
	}

	// This function queries the currency API after making sure
	// that the stored result(s) is/are old enough
	// It also makes a new query to the API if needed
	const json &query(const std::map<std::string, std::string> &query_data, const char *accesskey, const json &sentry);

private:
	std::map<const std::map<std::string, std::string>, std::pair<std::chrono::time_point<std::chrono::steady_clock>, json>> m_cache;
	std::chrono::seconds m_duration;
};

// Parse POST body
std::map<std::string, std::string> parse(const std::string &data);

// Perform currency conversion
double convert(const std::string &from_currency, std::string &to_currency, const double money_amount, const char *accesskey);

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string path_cat(boost::beast::string_view base, boost::beast::string_view path);

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<class Body, class Allocator, class Send>
void handle_request(boost::beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>> &&req,
	Send &&send, const char *accesskey, const char *apikey);

//------------------------------------------------------------------------------

// Report a failure
void fail(boost::system::error_code ec, const char *what);

// This is the C++11 equivalent of a generic lambda.
// The function object is used to send an HTTP message.
template<class Stream>
struct send_lambda
{
	Stream &stream_;
	bool &close_;
	boost::system::error_code &ec_;

	explicit send_lambda(Stream &stream, bool &close, boost::system::error_code &ec)
		: stream_{ stream }, close_{ close }, ec_{ ec }
	{
	}

	template<bool isRequest, class Body, class Fields>
	void operator()(http::message<isRequest, Body, Fields> &&msg) const;
};

// Handles an HTTP server connection
void do_session(tcp::socket socket, const std::string &doc_root, const char *accesskey, const char *apikey);

//------------------------------------------------------------------------------

// alternate implementation of `std::map::insert_or_assign` for C++14
template<typename M, typename K, typename V>
auto insert_or_assign(M&& map, K&& key, V&& value);

int main(int argc, char* argv[])
{
	try
	{
		// Check command line arguments.
		if (argc != 4)
		{
			std::cerr <<
				"Usage: currency_converter <address> <port> <doc_root>\n" <<
				"Example:\n" <<
				"    ./currency_converter 0.0.0.0 8080 .";
			return EXIT_FAILURE;
		}
		const auto address = boost::asio::ip::make_address(argv[1]);
		const auto port = static_cast<unsigned short>(std::atoi(argv[2]));
		const std::string doc_root = argv[3];

		// The io_context is required for all I/O
		boost::asio::io_context ioc{ 1 };

		// Google API Key
		char *apikey = std::getenv("apikey");

		// Access key for currencylayer.com's currency exchange API
		char *accesskey = std::getenv("accesskey");

		// The acceptor receives incoming connections
		tcp::acceptor acceptor{ ioc, { address, port } };
		std::cout << "Starting server at " << address << ':' << port << "...\n";
		for (;;)
		{
			// This will receive the new connection
			tcp::socket socket{ ioc };

			// Block until we get a connection
			acceptor.accept(socket);

			// Launch the session, transferring ownership of the socket
			std::thread([=, socket = std::move(socket)]() mutable {
				do_session(std::move(socket), doc_root, accesskey, apikey);
			}).detach();
		}
	}
	catch (const std::runtime_error &e)
	{
		std::cerr << "Line 159: Error: " << e.what() << '\n';
		return EXIT_FAILURE;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Line 164: Error: " << e.what() << '\n';
		return EXIT_FAILURE + 1;
	}
}

// Function to return a reasonable mime type based on the extension of a file.
boost::beast::string_view mime_type(boost::beast::string_view path)
{
	using boost::beast::iequals;
	const auto ext = [&path]
	{
		const auto pos = path.rfind(".");
		if (pos == boost::beast::string_view::npos)
		{
			return boost::beast::string_view{};
		}
		return path.substr(pos);
	}();
	if (iequals(ext, ".htm"))
	{
		return "text/html";
	}
	if (iequals(ext, ".html"))
	{
		return "text/html";
	}
	if (iequals(ext, ".php"))
	{
		return "text/html";
	}
	if (iequals(ext, ".css"))
	{
		return "text/css";
	}
	if (iequals(ext, ".txt"))
	{
		return "text/plain";
	}
	if (iequals(ext, ".js"))
	{
		return "application/javascript";
	}
	if (iequals(ext, ".json"))
	{
		return "application/json";
	}
	if (iequals(ext, ".xml"))
	{
		return "application/xml";
	}
	if (iequals(ext, ".swf"))
	{
		return "application/x-shockwave-flash";
	}
	if (iequals(ext, ".flv"))
	{
		return "video/x-flv";
	}
	if (iequals(ext, ".png"))
	{
		return "image/png";
	}
	if (iequals(ext, ".jpe"))
	{
		return "image/jpeg";
	}
	if (iequals(ext, ".jpeg"))
	{
		return "image/jpeg";
	}
	if (iequals(ext, ".jpg"))
	{
		return "image/jpeg";
	}
	if (iequals(ext, ".gif"))
	{
		return "image/gif";
	}
	if (iequals(ext, ".bmp"))
	{
		return "image/bmp";
	}
	if (iequals(ext, ".ico"))
	{
		return "image/vnd.microsoft.icon";
	}
	if (iequals(ext, ".tiff"))
	{
		return "image/tiff";
	}
	if (iequals(ext, ".tif"))
	{
		return "image/tiff";
	}
	if (iequals(ext, ".svg"))
	{
		return "image/svg+xml";
	}
	if (iequals(ext, ".svgz"))
	{
		return "image/svg+xml";
	}
	return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string path_cat(boost::beast::string_view base, boost::beast::string_view path)
{
	if (base.empty())
	{
		return path.to_string();
	}
	std::string result = base.to_string();
#if BOOST_MSVC
	constexpr char path_separator = '\\';
	if (result.back() == path_separator)
	{
		result.resize(result.size() - 1);
	}
	result.append(path.data(), path.size());
	for (auto &c : result)
	{
		if (c == '/')
		{
			c = path_separator;
		}
	}
#else
	constexpr char path_separator = '/';
	if (result.back() == path_separator)
	{
		result.resize(result.size() - 1);
	}
	result.append(path.data(), path.size());
#endif
	return result;
}

// Parse POST body
// Function uses state machine to parse POST body. Newlines
// and spaces are ignored. If it sees a quote, it'll read the next 
// stuff until it encounters a space into the value string.  The values
// are added to the parsed_values vector and that vector is returned back
std::map<std::string, std::string> parse(const std::string &data)
{
	enum class States
	{
		Start,
		Name,
		Ignore,
		Value
	};

	std::map<std::string, std::string> parsed_values;
	std::string name;
	std::string value;

	States state = States::Start;
	for (const char c : data)
	{
		switch (state)
		{
		case States::Start:
			if (c == '"')
			{
				state = States::Name;
			}
			break;
		case States::Name:
			if (c != '"')
			{
				name += c;
			}
			else
			{
				state = States::Ignore;
			}
			break;
		case States::Ignore:
			if (!isspace(c))
			{
				state = States::Value;
				value += c;
			}
			break;
		case States::Value:
			if (c != '\n')
			{
				value += c;
			}
			else
			{
				parsed_values.insert(std::make_pair(name, value));
				name = "";
				value = "";
				state = States::Start;
			}
			break;
		}
	}
	return parsed_values;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<class Body, class Allocator, class Send>
void handle_request(boost::beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>> &&req,
	Send &&send, const char *accesskey, const char *apikey)
{
	// Returns a bad request response
	const auto bad_request = [&req](boost::beast::string_view why)
	{
		http::response<http::string_body> res{ http::status::bad_request, req.version() };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = why.to_string();
		res.prepare_payload();
		return res;
	};

	// Returns a not found response
	const auto not_found = [&req](boost::beast::string_view target)
	{
		http::response<http::string_body> res{ http::status::not_found, req.version() };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "The resource '" + target.to_string() + "' was not found.";
		res.prepare_payload();
		return res;
	};

	// Returns a server error response
	const auto server_error = [&req](boost::beast::string_view what)
	{
		http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "An error occurred: '" + what.to_string() + "'";
		res.prepare_payload();
		return res;
	};

	// Make sure we can handle the method
	if (req.method() != http::verb::get &&
		req.method() != http::verb::head &&
		req.method() != http::verb::post)
	{
		return send(bad_request("Unknown HTTP-method"));
	}

	// Request path must be absolute and not contain "..".
	if (req.target().empty() ||
		req.target()[0] != '/' ||
		req.target().find("..") != boost::beast::string_view::npos)
	{
		return send(bad_request("Illegal request-target"));
	}

	// Build the path to the requested file
	std::string path;
	if (req.target() != "/?q=accesskey")
	{
		path = path_cat(doc_root, req.target());
		if (req.target().back() == '/')
		{
			path.append("index.html");
		}
	}

	// Attempt to open the file
	boost::beast::error_code ec;
	http::file_body::value_type body;
	if (req.target() != "/?q=accesskey" && req.target() != "/")
	{
		body.open(path.c_str(), boost::beast::file_mode::scan, ec);
	}

	// Handle the case where the file doesn't exist
	if (ec == boost::system::errc::no_such_file_or_directory)
	{
		return send(not_found(req.target()));
	}

	// Handle an unknown error
	if (ec)
	{
		return send(server_error(ec.message()));
	}

	// Respond to HEAD request
	if (req.method() == http::verb::head)
	{
		http::response<http::empty_body> res{ http::status::ok, req.version() };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, mime_type(path));
		res.content_length(body.size());
		res.keep_alive(req.keep_alive());
		return send(std::move(res));
	}

	// Respond to GET request
	else if (req.method() == http::verb::get)
	{
		if (req.target() == "/?q=accesskey")
		{
			http::response<http::string_body> res{
				std::piecewise_construct,
				std::make_tuple(std::move(std::string{ accesskey })),
				std::make_tuple(http::status::ok, req.version()) };
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, "plain/text");
			res.content_length(res.body().size());
			res.keep_alive(req.keep_alive());
			return send(std::move(res));
		}
		else if (req.target() == "/")
		{
			jinja2::Template tpl;
			tpl.LoadFromFile(path.c_str());
			jinja2::ValuesMap params = { { "apikey", std::string(apikey) } };

			http::response<http::string_body> res{
				std::piecewise_construct,
				std::make_tuple(std::move(tpl.RenderAsString(params))),
				std::make_tuple(http::status::ok, req.version()) };
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, "text/html");
			res.set(http::field::content_length, tpl.RenderAsString(params).size());
			res.keep_alive(req.keep_alive());
			return send(std::move(res));
		}
		else
		{
			http::response<http::file_body> res{
			   std::piecewise_construct,
			   std::make_tuple(std::move(body)),
			   std::make_tuple(http::status::ok, req.version()) };
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, mime_type(path));
			res.content_length(body.size());
			res.keep_alive(req.keep_alive());
			return send(std::move(res));
		}
	}

	// Respond to POST request
	else if (req.method() == http::verb::post)
	{
		boost::beast::string_view content_type = req[http::field::content_type];
		if (content_type.find("multipart/form-data") == std::string::npos &&
			content_type.find("application/x-www-form-urlencoded") == std::string::npos)
		{
			return send(bad_request("Bad request"));
		}

		std::map<std::string, std::string> parsed_value = parse(req.body());
		double money_amount = std::stod(parsed_value["currency_amount"]);
		std::string to_currency = parsed_value["to_currency"];
		std::string from_abbr = "USD";
		std::string to_abbr = to_currency.substr(0, 3);
		double conversion_result = convert(from_abbr, to_abbr, money_amount, accesskey);

		http::response<http::string_body> res{
			std::piecewise_construct,
			std::make_tuple(std::move(std::to_string(conversion_result))),
			std::make_tuple(http::status::ok, req.version()) };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "text/plain");
		res.content_length(res.body().size());
		res.keep_alive(req.keep_alive());
		return send(std::move(res));
	}
}

// Report a failure
void fail(boost::system::error_code ec, const char *what)
{
	std::cerr << what << ": " << ec.message() << "\n";
}

template<class Stream>
template<bool isRequest, class Body, class Fields>
void send_lambda<Stream>::operator()(http::message<isRequest, Body, Fields> &&msg) const
{
	// Determine if we should close the connection after
	close_ = msg.need_eof();

	// We need the serializer here because the serializer requires
	// a non-const file_body, and the message oriented version of
	// http::write only works with const messages.
	http::serializer<isRequest, Body, Fields> sr{ msg };
	http::write(stream_, sr, ec_);
}

// Handles an HTTP server connection
void do_session(tcp::socket socket, const std::string &doc_root, const char *accesskey, const char *apikey)
{
	bool close = false;
	boost::system::error_code ec;

	// This buffer is required to persist across reads 
	boost::beast::flat_buffer buffer;

	// This lambda is used to send messages 
	send_lambda<tcp::socket> lambda{ socket, close, ec };

	for (;;)
	{
		// Read a request 
		http::request<http::string_body> req;
		http::read(socket, buffer, req, ec);
		if (ec == http::error::end_of_stream)
		{
			break;
		}
		if (ec)
		{
			std::cerr << "Lines 587 and 588:\n";
			return fail(ec, "read");
		}

		// Send the response 
		handle_request(doc_root, std::move(req), lambda, accesskey, apikey);
		if (ec)
		{
			std::cerr << "Lines 595 and 596:\n";
			return fail(ec, "write");
		}
		if (close)
		{
			// This means we should close the connection, usually because 
			// the response indicated the "Connection: close" semantic. 
			break;
		}
	}

	// Send a TCP shutdown 
	socket.shutdown(tcp::socket::shutdown_send, ec);

	// At this point the connection is closed gracefully
}

// Perform currency conversion
double convert(const std::string &from_currency, std::string &to_currency, const double money_amount, const char *accesskey)
{
	using namespace std::chrono_literals;
	std::vector<std::string> currencies{
		"AED","AFN","ALL","AMD","ANG","AOA","ARS","AUD","AWG","AZN","BAM","BBD","BDT","BGN","BHD","BIF","BMD","BND","BOB","BRL","BSD","BTC","BTN","BWP",
		"BYN","BYR","BZD","CAD","CDF","CHF","CLF","CLP","CNY","COP","CRC","CUC","CUP","CVE","CZK","DJF","DKK","DOP","DZD","EGP","ERN","ETB","EUR","FJD",
		"FKP","GBP","GEL","GGP","GHS","GIP","GMD","GNF","GTQ","GYD","HKD","HNL","HRK","HTG","HUF","IDR","ILS","IMP","INR","IQD","IRR","ISK","JEP","JMD",
		"JOD","JPY","KES","KGS","KHR","KMF","KPW","KRW","KWD","KYD","KZT","LAK","LBP","LKR","LRD","LSL","LTL","LVL","LYD","MAD","MDL","MGA","MKD","MMK",
		"MNT","MOP","MRO","MUR","MVR","MWK","MXN","MYR","MZN","NAD","NGN","NIO","NOK","NPR","NZD","OMR","PAB","PEN","PGK","PHP","PKR","PLN","PYG","QAR",
		"RON","RSD","RUB","RWF","SAR","SBD","SCR","SDG","SEK","SGD","SHP","SLL","SOS","SRD","STD","SVC","SYP","SZL","THB","TJS","TMT","TND","TOP","TRY",
		"TTD","TWD","TZS","UAH","UGX","USD","UYU","UZS","VEF","VND","VUV","WST","XAF","XAG","XAU","XCD","XDR","XOF","XPF","YER","ZAR","ZMK","ZMW","ZWL"
	};

	std::map<std::string, std::string> query_data{ std::make_pair("from_currency", from_currency), std::make_pair("to_currency", to_currency) };

	cache_storage cache{ 1h };
	const json sentry{ nullptr };
	json j_res = cache.query(query_data, accesskey, sentry);

	double result = 0, rate = 0;

	try
	{
		rate = j_res["quotes"][from_currency + to_currency].get<double>();
	}
	catch (const json::exception &e)
	{
		std::cerr << "Line 640: Error: " << e.what() << '\n';
	}

	if (std::find(currencies.begin(), currencies.end(), to_currency) != currencies.end() &&
		std::find(currencies.begin(), currencies.end(), from_currency) != currencies.end())
	{
		result = money_amount * rate;
	}

	return result;
}

// This function queries the currency API after making sure
// that the stored result(s) is/are old enough
// It also makes a new query to the API if needed
const json &cache_storage::query(const std::map<std::string, std::string> &query_data, const char *accesskey, const json &sentry)
{
	auto found = m_cache.find(query_data);
	boost::beast::error_code ec;
	try
	{
		if (found == m_cache.end() || (std::chrono::steady_clock::now() - found->second.first) > m_duration)
		{
			std::string host{ "apilayer.net" }, api_endpoint{ "/api/live" },
				key{ accesskey }, source{ query_data.at(std::string("from_currency")) }, currency_param{ query_data.at(std::string("to_currency")) };
			std::string target;
			if (query_data.at(std::string("from_currency")) != "USD")
			{
				target = api_endpoint + "?access_key=" + accesskey + "&source=" + source + "&currencies=" + currency_param + "&format=1";
			}
			else
			{
				target = api_endpoint + "?access_key=" + accesskey + "&currencies=" + currency_param + "&format=1";
			}
			std::string port{ "80" };
			int version = 11;

			// The io_context is required for all IO
			boost::asio::io_context ioc;

			// These objects perform our IO
			tcp::resolver resolver{ ioc };
			tcp::socket socket{ ioc };

			// Look up the domain name
			const auto results = resolver.resolve(host, port);

			// Make the connection on the IP address we get from a lookup
			boost::asio::connect(socket, results.begin(), results.end());

			// Set up an HTTP GET request message
			http::request<http::string_body> req{ http::verb::get, target, version };
			req.set(http::field::host, host);
			req.set(http::field::content_type, "application/json");
			req.set(http::field::accept, "application/json");
			req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

			// Send the HTTP request to the remote host
			http::write(socket, req);

			// This buffer is used for reading and must be persisted
			boost::beast::flat_buffer buffer;

			// Declare a container to hold the response
			http::response<http::string_body> res;

			// Receive the HTTP response
			http::read(socket, buffer, res, ec);
			found = m_cache.insert_or_assign(found, query_data, std::make_pair(std::chrono::steady_clock::now(), json::parse(res.body())));
		}
		return found->second.second;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Line 714: Error: " << e.what() << '\n';
	}
	catch (const boost::beast::error_code &ec)
	{
		std::cerr << "Line 718: Error: " << ec.message() << '\n';
	}
	return sentry;
}