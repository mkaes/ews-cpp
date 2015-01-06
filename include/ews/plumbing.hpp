#pragma once

#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <functional>
#include <utility>
#include <memory>
#ifndef _MSC_VER
#include <type_traits>
#endif
#include <cstddef>

#include "rapidxml/rapidxml.hpp"

// Macro for verifying expressions at run-time. Calls assert() with 'expr'.
// Allows turning assertions off, even if -DNDEBUG wasn't given at
// compile-time.  This macro does nothing unless EWS_ENABLE_ASSERTS was defined
// during compilation.
#define EWS_ASSERT(expr) ((void)0)
#ifndef NDEBUG
# ifdef EWS_ENABLE_ASSERTS
#  include <cassert>
#  undef EWS_ASSERT
#  define EWS_ASSERT(expr) assert(expr)
# endif
#endif // !NDEBUG

// Visual Studio 12 does not support noexcept specifier.
#ifndef _MSC_VER
# define EWS_NOEXCEPT noexcept
#else
# define EWS_NOEXCEPT 
#endif

namespace ews
{
    // Wrapper around curl.h header file. Wraps everything in that header file
    // with C++ namespace ews::curl; also defines exception for cURL related
    // runtime errors and some RAII classes.
    namespace curl
    {

#include <curl/curl.h>

        class curl_error final : public std::runtime_error
        {
        public:
            explicit curl_error(const std::string& what)
                : std::runtime_error(what)
            {
            }
            explicit curl_error(const char* what) : std::runtime_error(what) {}
        };

        // Helper function; constructs an exception with a meaningful error
        // message from the given result code for the most recent cURL API call.
        //
        // msg: A string that prepends the actual cURL error message.
        // rescode: The result code of a failed cURL operation.
        inline curl_error make_error(const std::string& msg, CURLcode rescode)
        {
            std::string reason{curl_easy_strerror(rescode)};
            return curl_error(msg + ": \'" + reason + "\'");
        }

        // RAII helper class for CURL* handles.
        class curl_ptr final
        {
        public:
            curl_ptr() : handle_{curl_easy_init()}
            {
                if (!handle_)
                {
                    throw curl_error{"Could not start libcurl session"};
                }
            }

            ~curl_ptr() { curl_easy_cleanup(handle_); }

            // Could use curl_easy_duphandle for copying
            curl_ptr(const curl_ptr&) = delete;
            curl_ptr& operator=(const curl_ptr&) = delete;

            CURL* get() const EWS_NOEXCEPT { return handle_; }

        private:
            CURL* handle_;
        };

        // RAII wrapper class around cURLs slist construct.
        class curl_string_list final
        {
        public:
            curl_string_list() EWS_NOEXCEPT : slist_{nullptr} {}

            ~curl_string_list() { curl_slist_free_all(slist_); }

            void append(const char* str) EWS_NOEXCEPT
            {
                slist_ = curl_slist_append(slist_, str);
            }

            curl_slist* get() const EWS_NOEXCEPT { return slist_; }

        private:
            curl_slist* slist_;
        };

    } // curl

    namespace plumbing
    {
        // Obligatory scope guard helper.
        class on_scope_exit final
        {
        public:
            template <typename Function>
            on_scope_exit(Function destructor_function) try
                : func_(std::move(destructor_function)) // This could throw...
            {}
            catch (std::exception&)
            {
                // ...in which case we immediately call the d'tor function
                destructor_function();
            }

            on_scope_exit() = delete;
            on_scope_exit(const on_scope_exit&) = delete;
            on_scope_exit& operator=(const on_scope_exit&) = delete;

            ~on_scope_exit()
            {
                if (func_)
                {
                    try
                    {
                        func_();
                    }
                    catch (std::exception&)
                    {
                        // Swallow, abort(3) if suitable
                        EWS_ASSERT(false);
                    }
                }
            }

            inline void release()
            {
                func_ = nullptr;
            }

        private:
            std::function<void(void)> func_;
        };

#ifndef _MSC_VER
        // <type_traits> is broken in Visual Studio 12, disregard
        static_assert(!std::is_copy_constructible<on_scope_exit>::value, "");
        static_assert(!std::is_copy_assignable<on_scope_exit>::value, "");
        static_assert(!std::is_default_constructible<on_scope_exit>::value, "");
#endif

        // Raised when a response from a server could not be parsed.
        //
        // TODO: maybe remove?
        //
        // No need for type erasure because rapidxml::xml_document is exposed
        // by us anyway
        class parse_error final : public std::runtime_error
        {
        public:
            explicit parse_error(const std::string& what)
                : std::runtime_error(what)
            {
            }
            explicit parse_error(const char* what) : std::runtime_error(what) {}
        };

        class http_request;

        // This ought to be a DOM wrapper; usually around a web response
        //
        // This class basically wraps rapidxml::xml_document because the parsed
        // data must persist for the lifetime of the rapidxml::xml_document.
        class http_response final
        {
        public:
            http_response(long code, std::vector<char>&& data)
                : data_(std::move(data)), doc_(), code_(code), parsed_(false)
            {
                EWS_ASSERT(!data_.empty());
            }

            http_response(const http_response&) = delete;
            http_response& operator=(const http_response&) = delete;

            http_response(http_response&& other)
                : data_(std::move(other.data_)),
                  doc_(std::move(other.doc_)),
                  code_(std::move(other.code_)),
                  parsed_(std::move(other.parsed_))
            {
                other.code_ = 0U;
            }

            http_response& operator=(http_response&& rhs)
            {
                if (&rhs != this)
                {
                    data_ = std::move(rhs.data_);
                    doc_ = std::move(rhs.doc_);
                    code_ = std::move(rhs.code_);
                    parsed_ = std::move(rhs.parsed_);
                }
                return *this;
            }

            // Returns the SOAP payload in this response.
            //
            // Parses the payload (if it hasn't already) and returns it as
            // xml_document.
            //
            // Note: we are using a mutable temporary buffer internally because
            // we are using RapidXml in destructive mode (the parser modifies
            // source text during the parsing process). Hence, we need to make
            // sure that parsing is done only once!
            inline const rapidxml::xml_document<char>& payload()
            {
                if (!parsed_)
                {
                    auto set_parsed = on_scope_exit([&]()
                    {
                        parsed_ = true;
                    });
                    parse();
                }
                return doc_;
            }

            // Returns the response code of the HTTP request.
            inline long code() const EWS_NOEXCEPT
            {
                return code_;
            }

        private:
            // Here we handle the server's response. We load the SOAP payload
            // from response into the xml_document.
            inline void parse()
            {
                try
                {
                    static const int flags = 0;
                    doc_.parse<flags>(&data_[0]);
                }
                catch (rapidxml::parse_error& e)
                {
                    // Swallow and erase type
                    throw parse_error(e.what());
                }
            }

            std::vector<char> data_;
            rapidxml::xml_document<char> doc_;
            long code_;
            bool parsed_;
        };

        class credentials
        {
        public:
            virtual ~credentials() = default;
            virtual void certify(http_request*) const = 0;
        };

        class ntlm_credentials : public credentials
        {
        public:
            ntlm_credentials(std::string username, std::string password,
                             std::string domain)
                : username_(std::move(username)),
                  password_(std::move(password)),
                  domain_(std::move(domain))
            {
            }

        private:
            void certify(http_request*) const override; // implemented below

            std::string username_;
            std::string password_;
            std::string domain_;
        };

        class http_request final
        {
        public:
            enum class method
            {
                POST
            };

            // Create a new HTTP request to the given URL.
            explicit http_request(const std::string& url)
            {
                set_option(curl::CURLOPT_URL, url.c_str());
            }

            // Set the HTTP method (only POST supported).
            void set_method(method)
            {
                // Method can only be a regular POST in our use case
                set_option(curl::CURLOPT_POST, 1);
            }

            // Set this HTTP request's content type.
            void set_content_type(const std::string& content_type)
            {
                const std::string str = "Content-Type: " + content_type;
                headers_.append(str.c_str());
            }

            // Set credentials for authentication.
            void set_credentials(const credentials& creds)
            {
                creds.certify(this);
            }

            // Small wrapper around curl_easy_setopt(3).
            //
            // Converts return codes to exceptions of type curl_error and allows
            // objects other than http_web_requests to set transfer options
            // without direct access to the internal CURL handle.
            template <typename... Args>
            void set_option(curl::CURLoption option, Args... args)
            {
                auto retcode = curl::curl_easy_setopt(
                    handle_.get(), option, std::forward<Args>(args)...);
                switch (retcode)
                {
                case curl::CURLE_OK:
                    return;

                case curl::CURLE_FAILED_INIT:
                {
                    throw curl::make_error(
                        "curl_easy_setopt: unsupported option", retcode);
                }

                default:
                {
                    throw curl::make_error(
                        "curl_easy_setopt: failed setting option", retcode);
                }
                };
            }

            // Perform the HTTP request and returns the response. This function
            // blocks until the complete response is received or a timeout is
            // reached. Throws curl_error if operation could not be completed.
            //
            // request: The complete request string; you must make sure that
            // the data is encoded the way you want the server to receive it.
            http_response send(const std::string& request)
            {
#ifndef NDEBUG
                // Print HTTP headers to stderr
                set_option(curl::CURLOPT_VERBOSE, 1L);
#endif

                // Some servers don't like requests that are made without a
                // user-agent field, so we provide one
                set_option(curl::CURLOPT_USERAGENT, "libcurl-agent/1.0");

                // Set complete request string for HTTP POST method; note: no
                // encoding here

                set_option(curl::CURLOPT_POSTFIELDS, request.c_str());
                set_option(curl::CURLOPT_POSTFIELDSIZE, request.length());

                set_option(curl::CURLOPT_HTTPHEADER, headers_.get());

                std::vector<char> response_data;

                auto callback =
                    [](char* ptr, std::size_t size, std::size_t nmemb,
                       void* userdata) -> std::size_t
                {
                    std::vector<char>* buf
                        = reinterpret_cast<std::vector<char>*>(userdata);
                    const auto realsize = size * nmemb;
                    try
                    {
                        buf->reserve(realsize + 1);
                    }
                    catch (std::bad_alloc&)
                    {
                        // Out of memory
                        return 0U;
                    }
                    std::copy(ptr, ptr + realsize, std::back_inserter(*buf));
                    buf->emplace_back('\0');
                    return realsize;
                };

                set_option(
                    curl::CURLOPT_WRITEFUNCTION,
                    static_cast<std::size_t (*)(char*, std::size_t, std::size_t,
                                                void*)>(callback));
                set_option(curl::CURLOPT_WRITEDATA,
                        std::addressof(response_data));

#ifndef NDEBUG
                // Turn-off verification of the server's authenticity
                set_option(curl::CURLOPT_SSL_VERIFYPEER, 0);
#endif

                auto retcode = curl::curl_easy_perform(handle_.get());
                if (retcode != 0)
                {
                    throw curl::make_error("curl_easy_perform", retcode);
                }
                long response_code = 0U;
                curl::curl_easy_getinfo(handle_.get(),
                    curl::CURLINFO_RESPONSE_CODE, &response_code);
                return http_response(response_code, std::move(response_data));
            }

        private:
            curl::curl_ptr handle_;
            curl::curl_string_list headers_;
        };

        // Makes a raw SOAP request.
        //
        // url: The URL of the server to talk to.
        // username: The username of user.
        // password: The user's secret password, plain-text.
        // domain: The user's Windows domain.
        // soap_body: The contents of the SOAP body (minus the body element);
        // this is the actual EWS request.
        // soap_headers: Any SOAP headers to add.
        //
        // Returns the response.
        inline http_response make_raw_soap_request(
            const std::string& url, const std::string& username,
            const std::string& password, const std::string& domain,
            const std::string& soap_body,
            const std::vector<std::string>& soap_headers)
        {
            http_request request{url};
            request.set_method(http_request::method::POST);
            request.set_content_type("text/xml; charset=utf-8");

            ntlm_credentials creds{username, password, domain};
            request.set_credentials(creds);

            std::stringstream request_stream;
            request_stream <<
R"(<?xml version="1.0" encoding="utf-8"?>
<soap:Envelope
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    xmlns:xsd="http://www.w3.org/2001/XMLSchema"
    xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/"
    xmlns:m="http://schemas.microsoft.com/exchange/services/2006/messages"
    xmlns:t="http://schemas.microsoft.com/exchange/services/2006/types"
    >)";

            // Add SOAP headers if present
            if (!soap_headers.empty())
            {
                request_stream << "<soap:Header>\n";
                for (const auto& header : soap_headers)
                {
                    request_stream << header;
                }
                request_stream << "</soap:Header>\n";
            }

            request_stream << "<soap:Body>\n";
            // Add the passed request
            request_stream << soap_body;
            request_stream << "</soap:Body>\n";
            request_stream << "</soap:Envelope>\n";

            return request.send(request_stream.str());
        }

        // Implementations

        void ntlm_credentials::certify(http_request* request) const
        {
            EWS_ASSERT(request != nullptr);

            // CURLOPT_USERPWD: domain\username:password
            std::string login = domain_ + "\\" + username_ + ":" + password_;
            request->set_option(curl::CURLOPT_USERPWD, login.c_str());
            request->set_option(curl::CURLOPT_HTTPAUTH, CURLAUTH_NTLM);
        }
    }
}
