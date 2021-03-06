#include "stdafx.h"
#include "Logger.h"
#define TEST_LOGGING 1

#ifdef TEST_LOGGING

#include <thread>
#include <future>
#include <vector>
#include <functional>
#include <string>


size_t work() {
	std::ostringstream s;
	s << "Thread ID: " << std::this_thread::get_id();

	for (size_t i = 0; i < 2; ++i) {
		//std::async is pretty uninteresting unless you make things yield
		Logger::LOGERROR(s.str());
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		Logger::LOGWARN(s.str());
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		Logger::LOGINFO(s.str());
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		Logger::LOGDEBUG(s.str());
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		Logger::LOGTRACE(s.str());
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		// custom user defined types
		Logger::LOG(s.str(), "User Defined");
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return 10;
}

int main(void) {
	//configure logging, if you dont it defaults to standard out logging 
	//Logger::configure({ {"type", "file"}, {"file_name", "test.log"}, {"reopen_interval", "1"} });

	//start up some threads
	std::vector<std::future<size_t> > results;
	for (size_t i = 0; i < 4; ++i) {
		results.emplace_back(std::async(std::launch::async, work));
	}

	//dont really care about the results but we can pretend
	bool exit_code = 0;
	for (auto& result : results) {
		try {
			size_t count = result.get();
		}
		catch (std::exception& e) {
			std::cout << e.what();
			exit_code++;
		}
	}
	int a;
	std::cin >> a;
	return exit_code;
}

#endif


