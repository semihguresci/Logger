//
// Created by semih on 8/11/18.
//

#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

/*
Test this with something like:
g++ -std=c++11 -x c++ -pthread -DLOGGING_LEVEL_ALL -DTEST_LOGGING logging.hpp -o logging_test
./logging_test
*/

// to get the user name 
#include <windows.h>
#include <Lmcons.h>

#include <string>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <ctime>
#include <cstdlib>

namespace Logger {

	//TODO: use macros (again) so __FILE__ __LINE__ could be automatically added to certain error levels?

	//The list of severities defined by Syslog standard
	enum class severity_level : uint8_t {
		TRACE = 0,
		DEBUG = 1,
		INFO = 2,
		WARN = 3,
		ERR = 4,
		ALERT = 5,
		CRIT = 6,
		EMERG = 7,
		NOTICE = 8,
	};

	// Enum string tupple maker struct 
	struct enum_hasher
	{
		template <typename T> std::size_t operator()(T t) const
		{
			return static_cast<std::size_t>(t);
		}
	};

	//returns formated to: 'year/mo/dy hr:mn:sc.xxxxxx'
	inline std::string timestamp() {
		//get the time
		std::chrono::system_clock::time_point tp = std::chrono::system_clock::now();
		std::time_t tt = std::chrono::system_clock::to_time_t(tp);
		std::tm gmt{};
		gmtime_s(&gmt, &tt);
		std::chrono::duration<double> fractional_seconds = (tp - std::chrono::system_clock::from_time_t(tt)) + std::chrono::seconds(gmt.tm_sec);
		//format the string
		std::string buffer("year/mo/dy hr:mn:sc.xxxxxx");
		/*printf(&buffer.front(), "%04d/%02d/%02d %02d:%02d:%09.6f", gmt.tm_year + 1900, gmt.tm_mon + 1,
		gmt.tm_mday, gmt.tm_hour, gmt.tm_min, fractional_seconds.count());*/
		buffer = std::to_string(gmt.tm_year + 1900) + "/" +
			std::to_string(gmt.tm_mon + 1) + "/" +
			std::to_string(gmt.tm_mday) + " " +
			std::to_string(gmt.tm_hour) + ":" +
			std::to_string(gmt.tm_min) + ":"
			+ std::to_string(fractional_seconds.count()
			);
		//printf(buffer.c_str());
		return buffer;
	}

	std::string getusername() {
		TCHAR  username[UNLEN + 1];
		DWORD username_len = UNLEN + 1;
		GetUserName((TCHAR*)username, &username_len);
		std::wstring usernamewstr(&username[0]); //convert to wstring
		std::string usernamestr(usernamewstr.begin(), usernamewstr.end());
		return  usernamestr ;
	}


	//logger base class, not pure virtual so you can use as a null logger if you want
	using logging_config_t = std::unordered_map<std::string, std::string>;

	class logger
	{
	protected:

		std::mutex lock;
		const std::unordered_map<severity_level, std::string, enum_hasher> severtylevels
		{
			{ severity_level::NOTICE, " [NOTICE] " },
			{ severity_level::EMERG, " [EMERG] " },
			{ severity_level::CRIT, " [CRIT] " },
			{ severity_level::ALERT, " [ALERT] " },
			{ severity_level::ERR, " [ERROR] " },
			{ severity_level::WARN, " [WARN] " },
			{ severity_level::INFO, " [INFO] " },
			{ severity_level::DEBUG, " [DEBUG] " },
			{ severity_level::TRACE, " [TRACE] " }
			
		};

	public:
		logger() = delete;
		logger(const logging_config_t& config) {};
		virtual ~logger() {};

		virtual void log(const std::string& message, const severity_level level) {
			auto exist = severtylevels.find(level);
			if (exist == severtylevels.end())
				return;

			std::string output;
			output.reserve(message.length() + 64);
			output.append(timestamp());
			output.append(" "+ getusername());
			output.append(severtylevels.find(level)->second);
			output.append(message);
			output.push_back('\n');
			log(output);
		}

		virtual void log(const std::string& message, const std::string& customseverity) {

			std::string output;
			output.reserve(message.length() + 64);
			output.append(timestamp());
			output.append(" "+getusername());
			output.append(customseverity);
			output.append(message);
			output.push_back('\n');
			log(output);
		}
		virtual void log(const std::string&) {}

	};


	

	//logger that writes to standard out
	class std_out_logger : public logger {
	public:
		std_out_logger() = delete;
		std_out_logger(const logging_config_t& config) : logger(config) 
		{
		}
		virtual void log(const std::string& message) {
			//cout is thread safe, to avoid multiple threads interleaving on one line
			//though, we make sure to only call the << operator once on std::cout
			//otherwise the << operators from different threads could interleave
			//obviously we dont care if flushes interleave
			std::cout << message;
			std::cout.flush();
		}
	};

	//TODO: add log rolling
	//logger that writes to file
	class file_logger : public logger {

	private:
		~file_logger()
		{
			if (file.is_open())
			{
				lock.lock();
				file.flush();
				file.close();
				lock.unlock();
			}
		}

	public:
		file_logger() = delete;
		file_logger(const logging_config_t& config) :logger(config) {
			//grab the file name
			auto name = config.find("file_name");
			if (name == config.end())
				throw std::runtime_error("No output file provided to file logger");
			file_name = name->second;

			//if we specify an interval
			reopen_interval = std::chrono::seconds(300);
			auto interval = config.find("reopen_interval");
			if (interval != config.end())
			{
				try {
					reopen_interval = std::chrono::seconds(std::stoul(interval->second));
				}
				catch (...) {
					throw std::runtime_error(interval->second + " is not a valid reopen interval");
				}
			}

			//crack the file open
			reopen();
		}

		virtual void log(const std::string& message) {
			lock.lock();
			file << message;
			file.flush();
			lock.unlock();
			reopen();
		}

	protected:
		void reopen() 
		{
			//TODO: use CLOCK_MONOTONIC_COARSE
			//check if it should be closed and reopened
			auto now = std::chrono::system_clock::now();
			lock.lock();
			if (now - last_reopen > reopen_interval) {
				last_reopen = now;
				try { file.close(); 
				}
				catch (...) {}
				try
				{
					if (!file.is_open())
					{

						file.open(file_name, std::ofstream::out | std::ofstream::app);
						last_reopen = std::chrono::system_clock::now();
					}
				}
				catch (std::exception& e) {
					try { file.close(); 
					}
					catch (...) {}
					throw e;
				}
			}
			lock.unlock();
		}
		std::string file_name;
		std::ofstream file;
		std::chrono::seconds reopen_interval;
		std::chrono::system_clock::time_point last_reopen;
	};

	class logmaster : public logger
	{
	private:
		std_out_logger * stdlogger = nullptr;
		file_logger * filelogger = nullptr;
		~logmaster(){
			if (stdlogger != nullptr) {
				free(stdlogger);
			}

			if (filelogger!= nullptr) {
				free(filelogger);
			}
		}
	public:
		logmaster() = delete;
		logmaster(const logging_config_t& config) : logger(config)
		{
			stdlogger = new std_out_logger(config);
			filelogger = new file_logger(config);
		}


		virtual void log(const std::string& message) 
		{
			if (stdlogger!= nullptr)
			{
				stdlogger->log(message);
			}
			if (filelogger!= nullptr)
			{
				filelogger->log(message);
			}
		}
	};

	//a factory that can create loggers (that derive from 'logger') via function pointers
	//this way you could make your own logger that sends log messages to who knows where
	using logger_creator = logger * (*)(const logging_config_t&);

	class logger_factory
	{
	public:
		logger_factory()
		{
			creators.emplace("", [](const logging_config_t& config)->logger*{return new logger(config);});
			creators.emplace("std_out", [](const logging_config_t& config)->logger* {return new std_out_logger(config); });
			creators.emplace("file", [](const logging_config_t& config)->logger* {return new file_logger(config); });
			creators.emplace("master", [](const logging_config_t& config)->logger* {return new logmaster(config); });
		}

		logger* produce(const logging_config_t& config) const 
		{
			//grab the type
			auto type = config.find("type");
			if (type == config.end())
				throw std::runtime_error("Logging factory configuration requires a type of logger");

			//grab the logger
			auto found = creators.find(type->second);
			if (found != creators.end())
			{
				return found->second(config);
			}

			//couldn't get a logger
			throw std::runtime_error("Couldn't produce logger for type: " + type->second);
		}
	protected:
		std::unordered_map<std::string, logger_creator> creators;
	};

	//statically get a factory
	inline logger_factory& get_factory() {
		static logger_factory factory_singleton{};
		return factory_singleton;
	}

	//get at the singleton
	inline logger& get_logger(const logging_config_t& config = { { "type", "master" }, { "file_name", "Log.log" } })
	{
		static std::unique_ptr<logger> singleton(get_factory().produce(config));
		return *singleton;
	}

	//configure the singleton (once only)
	inline void configure(const logging_config_t& config) {
		get_logger(config);
	}

	//statically log manually without the macros below
	inline void log(const std::string& message, const severity_level level) {
		get_logger().log(message, level);
	}

	//statically log manually without a level or maybe with a custom one
	inline void log(const std::string& message)
	{
		get_logger().log(message);
	}

	//these standout when reading code
	inline void LOGTRACE(const std::string& message) 
	{
		get_logger().log(message, severity_level::TRACE);
	};

	inline void LOGDEBUG(const std::string& message) 
	{
		get_logger().log(message, severity_level::DEBUG);
	};

	inline void LOGINFO(const std::string& message) 
	{
		get_logger().log(message, severity_level::INFO);
	};

	inline void LOGWARN(const std::string& message) 
	{
		get_logger().log(message, severity_level::WARN);
	};

	inline void LOGERROR(const std::string& message) 
	{
		get_logger().log(message, severity_level::ERR);
	};

	inline void LOGALERT(const std::string& message) {
		get_logger().log(message, severity_level::ALERT);
	};

	inline void LOGCRIT(const std::string& message) {
		get_logger().log(message, severity_level::CRIT);
	};

	inline void LOGEMERG(const std::string& message) {
		get_logger().log(message, severity_level::EMERG);
	};

	inline void LOGNOTICE(const std::string& message) {
		get_logger().log(message, severity_level::NOTICE);
	}

	inline void  LOG(const std::string& message, std::string customSeverity="CUSTOM") {

		std::string severity = " [" + customSeverity + "] ";
		get_logger().log(message, severity);
	}


}

#endif //__LOGGING_HPP__

