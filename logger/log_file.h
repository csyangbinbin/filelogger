/********************************************************************
created:	2013/07/17
created:	17:7:2013   8:16
filename: 	log_file.h
file path:	logfile
file base:	log_file
file ext:	h
author:		

purpose:	异步日志记录（主要算法参考muduo）

License:

// Muduo - A reactor-based C++ network library for Linux
// Copyright (c) 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//   * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//   * The name of the author may not be used to endorse or promote
// products derived from this software without specific prior written
// permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*********************************************************************/
#ifndef __LOG_FILE_INCLUDE__
#define __LOG_FILE_INCLUDE__

#include <stdio.h>
#include <string>
#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/thread.hpp>
#include <windows.h>
#include "threadctrl/threadctrl.h"
#include "threadctrl/threadctrl_ext.h"
#include "singleton.h"


#define  FILE_SIZE_1K		(1024)
#define  FILE_SIZE_1M		(1024*FILE_SIZE_1K)
#define  FILE_SIZE_1G		(1024*FILE_SIZE_1M)

namespace fst_log_file
{

	class fast_file : boost::noncopyable
	{
	public:
		explicit fast_file(const std::string& filename);
		~fast_file();
		void append(const char* logline, const size_t len);
		void flush();
		size_t writtenBytes() const { return writtenBytes_; }

	private:
		size_t write(const char* logline, size_t len) ;
		FILE* fp_;
		size_t writtenBytes_;
	};


	class log_file : boost::noncopyable
	{
	public:
		log_file(const std::string& basename,
			size_t rollSize = 4*FILE_SIZE_1M,
			int checkEveryN = 20,
			bool threadSafe = true ,
			int flushInterval = 2);
		~log_file();

		void append(const char* logline, int len);
		void flush();

		static 	std::string getLogFileName(const std::string& basename, time_t* now) ;
	private:
		void append_unlocked(const char* logline, int len);
		bool rollFile() ;

		const std::string basename_;
		const int flushInterval_;
		time_t lastFlush_;
		time_t startOfPeriod_;
		time_t lastRoll_;
		void* mutex_ ; 	
		size_t rollSize_ ; 
		int count_ ; 
		int checkEveryN_ ; 
		boost::scoped_ptr<fast_file> file_;
		const static int kRollPerSeconds_ = 60*60*24;
	};


	const int kSmallBuffer = 4000;
	const int kLargeBuffer = 4000*1000;

	template<int SIZE>
	class FixedBuffer : boost::noncopyable
	{
	public:
		FixedBuffer()
			: cur_(data_)
		{}

		~FixedBuffer(){}

		void append(const char*  buf, size_t len){
			if ((size_t)avail() > len){
				memcpy(cur_, buf, len);
				cur_ += len;
			}
		}
		const char* data() const { return data_; }
		int length() const { return static_cast<int>(cur_ - data_); }

		char* current() { return cur_; }
		int avail() const { return static_cast<int>(end() - cur_); }
		void add(size_t len) { cur_ += len; }

		void reset_buffer() { cur_ = data_; }
		void bzero() { ::memset(data_,  0 , sizeof(data_) ) ; }

	private:
		const char* end() const { return data_ + sizeof data_; }
		char data_[SIZE];
		char* cur_;
	};

	//每条日志的最大字节数
	#define  MAX_LOG_BUFFER_SIZE	(512)

	class async_logging : boost::noncopyable
	{
	public:
		async_logging(const std::string& basename,int flushInterval = 2);
		~async_logging();

		void append(const char* logline, int len);
		void start();
		void stop();

	private:
		async_logging(const async_logging&);  // ptr_container
		async_logging& operator=(const async_logging&);  // ptr_container

		void threadFunc();
	
		typedef FixedBuffer<kSmallBuffer> Buffer;
		typedef boost::ptr_vector<Buffer> BufferVector;
		typedef BufferVector::auto_type BufferPtr;

		const int flushInterval_;
		volatile bool running_;
		std::string basename_;
		void* mutex_ ;
		void* cond_ ; 
		BufferPtr currentBuffer_;
		BufferPtr nextBuffer_;
		BufferVector buffers_;
		countdown_latch latch_ ; 
		boost::thread* log_thread ; 
	};

	enum LogLevel
	{
		DEBUG_LEVEL =0,
		INFO_LEVEL,
		WARN_LEVEL,
		ERR_LEVEL,
		NULL_LEVEL
	};

	class logger
	{
		DECLARE_SINGLETON_CLASS(logger);
	public:
		void init(const std::string& log_in_dir ,const std::string&basename ,LogLevel level) ; 
		bool load_config(const std::string& filename);
		void log(LogLevel level ,const char *logstr, ... );
		void stop();

	protected:
		logger()
			:is_console_log(false)
			,is_file_log(false)
		{}
	private:
		logger(const logger&);
		logger& operator=(const logger&) ;

	private:
			void console_output(LogLevel level , const char* buffer , int len );
			void logfile_output(LogLevel level , const char* buffer , int len) ; 
			bool config_set_log_level(std::string& cfg , LogLevel& level) ; 
			bool create_log_dir();
			void start_logging();
			
	private:
		std::string basename_ ; 
		LogLevel console_level_ , logfile_level_ ;
		boost::scoped_ptr<async_logging> logfilePtr_ ;
		bool is_console_log , is_file_log ; 
		HANDLE hOut; 
		std::string log_dir_;

	};

	typedef pattern::singleton<logger>	sln_logger ;
	

#define LOG_LOAD_CONFIG(file)	 fst_log_file::sln_logger::instance().load_config(file)

#if defined(USE_LOG_FILE)
	//使用logfile日志
	#define LOG_DBG(...)	fst_log_file::sln_logger::instance().log( fst_log_file::DEBUG_LEVEL,  __VA_ARGS__);
	#define LOG_DEBUG(...)	fst_log_file::sln_logger::instance().log( fst_log_file::DEBUG_LEVEL,  __VA_ARGS__);
	#define LOG_INFO(...)	fst_log_file::sln_logger::instance().log( fst_log_file::INFO_LEVEL,  __VA_ARGS__);
	#define LOG_WARN(...)	fst_log_file::sln_logger::instance().log( fst_log_file::WARN_LEVEL,  __VA_ARGS__);
	#define LOG_ERR(...)	fst_log_file::sln_logger::instance().log( fst_log_file::ERR_LEVEL,  __VA_ARGS__);
	#define LOG_ERROR(...)	fst_log_file::sln_logger::instance().log( fst_log_file::ERR_LEVEL,  __VA_ARGS__);

#else
	//不使用logfile日志，仅仅将日志重定向到标准输出

#define LOG_DBG(...)	printf(  __VA_ARGS__);
#define LOG_DEBUG(...)	printf(  __VA_ARGS__);
#define LOG_INFO(...)	printf(  __VA_ARGS__);
#define LOG_WARN(...)	printf(  __VA_ARGS__);
#define LOG_ERR(...)	printf(  __VA_ARGS__);
#define LOG_ERROR(...)	printf(  __VA_ARGS__);


#endif	

} 


#endif