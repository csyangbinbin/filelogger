#include "stdafx.h"
#include <cassert>
#include <boost/date_time.hpp>
#include <boost/bind.hpp>
#include "log_file.h"
#include <Winsock2.h>
#include <Dbghelp.h>
#include <boost/algorithm/string.hpp>
//#include "current_thread.h"
#include "tinystr.h"
#include "tinyxml.h"

#if defined(WIN32)
#pragma  comment(lib,"Dbghelp.lib")
#endif

using namespace fst_log_file;

fast_file::fast_file(const std::string& filename)
: fp_(::fopen(filename.data(), "ab")),
writtenBytes_(0)
{
	assert(fp_);
}

fast_file::~fast_file()
{
	::fclose(fp_);
}
void fast_file::append(const char* logline, const size_t len)
{ 
	size_t n = write(logline, len);
	size_t remain = len - n;
	while (remain > 0)
	{
		size_t x = write(logline + n, remain);
		if (x == 0)
		{
			int err = ferror(fp_);
			if (err)
			{
				const unsigned int err_buf_size = 128 ; 
				char err_buf[err_buf_size] ; 
				strerror_s(err_buf, err_buf_size , err);
				fprintf(stderr, "fast_file::append() failed %s\n", err_buf);
			}
			break;
		}
		n += x;
		remain = len - n; 
	}

	writtenBytes_ += len;

}
void fast_file::flush()
{
	::fflush(fp_);
}
size_t fast_file::write(const char* logline, size_t len) 
{
	return ::_fwrite_nolock(logline, 1, len, fp_) ; 
}


log_file::log_file(const std::string& basename,
				   size_t rollSize,
				   int checkEveryN ,
				   bool threadSafe/* = true*/ , 
				   int flushInterval /*= 3*/)
				   : basename_(basename),
				   flushInterval_(flushInterval),
				   lastRoll_(0),
				   startOfPeriod_(0),
				   rollSize_(rollSize),
				   count_(0),
				   checkEveryN_(checkEveryN),
				   lastFlush_(0)
{
	if(threadSafe)
		THREADCTL_ALLOC_LOCK(mutex_ , THREADCTL_LOCKTYPE_READWRITE);
	else
		mutex_ = NULL ; 

	rollFile();

}

log_file::~log_file()
{

}

void log_file::append(const char* logline, int len)
{ 
	if(mutex_)
	{
		lock_guard guard(mutex_);
		append_unlocked(logline, len);
	}
	else
		append_unlocked(logline, len);


}
void log_file::flush()
{
	if(mutex_)
	{
		lock_guard guard(mutex_);
		file_->flush();
	}
	else
		file_->flush();


}
void log_file::append_unlocked(const char* logline, int len)
{
	file_->append(logline, len);
	if (file_->writtenBytes() > rollSize_)
	{
		rollFile();
	}
	else
	{
		++count_;
		if (count_ >= checkEveryN_)
		{
			count_ = 0;
			time_t now = ::time(NULL);
			time_t thisPeriod_ = now / kRollPerSeconds_ * kRollPerSeconds_;
			if (thisPeriod_ != startOfPeriod_)
			{
				rollFile();
			}
			else if (now - lastFlush_ > flushInterval_)
			{
				lastFlush_ = now;
				file_->flush();
			}
		}
	}

}

bool log_file::rollFile()
{
	time_t now = 0;
	std::string filename = getLogFileName(basename_, &now);
	time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;

	if (now > lastRoll_)
	{
		lastRoll_ = now;
		lastFlush_ = now;
		startOfPeriod_ = start;
		file_.reset(new fast_file(filename));
		return true;
	}
	return false;
}

std::string log_file::getLogFileName(const std::string& basename, time_t* now)
{
	std::string filename;
	filename.reserve(basename.size() + 64);
	filename = basename;

	char timebuf[32];
	struct tm tm;
	*now = time(NULL);
	gmtime_s(&tm , now );
	strftime(timebuf, sizeof timebuf, ".%Y%m%d-%H%M%S", &tm);
	filename += timebuf;

#ifdef WIN32
	SYSTEMTIME st ;
	::GetLocalTime(&st);
	char buffer_milsec[10] ;
	sprintf_s(buffer_milsec , "-%d" , st.wMilliseconds);
	filename+=buffer_milsec ; 
#endif

	filename += ".log";

	return filename;
}



async_logging::async_logging(const std::string& basename,int flushInterval/* = 3*/)
:flushInterval_(flushInterval),
running_(false),
basename_(basename),
currentBuffer_(new Buffer()),
nextBuffer_(new Buffer()),
buffers_(),
latch_(1)
{
	THREADCTL_ALLOC_LOCK(mutex_ , THREADCTL_LOCKTYPE_READWRITE);
	THREADCTL_ALLOC_COND(cond_) ; 

	currentBuffer_->bzero();
	nextBuffer_->bzero();
	buffers_.reserve(16);
}

async_logging::~async_logging()
{
	stop();
}

void async_logging::start()
{
	running_ = true;
	log_thread = new boost::thread(boost::bind(&async_logging::threadFunc , this )) ;
	latch_.wait();
}

void async_logging::stop()
{
	if(log_thread)
	{
	running_ = false;
	THREADCTL_COND_BROADCAST(cond_);
	log_thread->join(); 
	delete log_thread ;
	log_thread = NULL ; 
	}

}


void async_logging::append(const char* logline, int len)
{
	lock_guard lock(mutex_) ; 

	if (currentBuffer_->avail() > len)
	{
		currentBuffer_->append(logline, len);
	}
	else
	{
		buffers_.push_back(currentBuffer_.release());

		if (nextBuffer_)
		{
			currentBuffer_ = boost::ptr_container::move(nextBuffer_);
		}
		else
		{
			currentBuffer_.reset(new Buffer);
		}
		currentBuffer_->append(logline, len);
		THREADCTL_COND_BROADCAST(cond_);
	}
}


void async_logging::threadFunc()
{
	assert(running_ == true);
	log_file output(basename_   ,4*FILE_SIZE_1M, 256 ,false );
	BufferPtr newBuffer1(new Buffer());
	BufferPtr newBuffer2(new Buffer());
	newBuffer1->bzero();
	newBuffer2->bzero();
	BufferVector buffersToWrite;
	buffersToWrite.reserve(16);

	latch_.countdown();

	while (running_)
	{
		assert(newBuffer1 && newBuffer1->length() == 0);
		assert(newBuffer2 && newBuffer2->length() == 0);
		assert(buffersToWrite.empty());

		{ //mutex scope 
			lock_guard lock(mutex_) ; 

			timeval wait_time;
			wait_time.tv_sec =flushInterval_;
			wait_time.tv_usec = 0 ;
			if (buffers_.empty()) {
				THREADCTL_COND_WAIT_TIMED(cond_ , mutex_ ,&wait_time);
			}

			buffers_.push_back(currentBuffer_.release());
			currentBuffer_ = boost::ptr_container::move(newBuffer1);
			buffersToWrite.swap(buffers_);
			if (!nextBuffer_)
			{
				nextBuffer_ = boost::ptr_container::move(newBuffer2);
			}
		} //end mutex scope 

		assert(!buffersToWrite.empty());

		if (buffersToWrite.size() > 25) //前端日志写入速度过快 ,将过多的buffer抛弃
		{
			char buf[256];
			sprintf_s(buf, sizeof buf, "Dropped log messages  %d larger buffers\n",
				buffersToWrite.size()-2);
			fputs(buf, stderr);

			buffersToWrite.erase(buffersToWrite.begin()+2, buffersToWrite.end());
		}

		for (size_t i = 0; i < buffersToWrite.size(); ++i)
		{
			output.append(buffersToWrite[i].data(), buffersToWrite[i].length());
		}

		if (buffersToWrite.size() > 2) //后端只保留两个基本的buffer
		{
			buffersToWrite.resize(2);
		}

		if (!newBuffer1)
		{
			assert(!buffersToWrite.empty());
			newBuffer1 = buffersToWrite.pop_back();
			newBuffer1->reset_buffer();
		}

		if (!newBuffer2)
		{
			assert(!buffersToWrite.empty());
			newBuffer2 = buffersToWrite.pop_back();
			newBuffer2->reset_buffer();
		}

		buffersToWrite.clear();
		output.flush();
	}//for
	output.flush();
}


const char* logLevelStr[NULL_LEVEL] = {
	"D",
	"I",
	"W",
	"E",
} ;

void logger::init(const std::string& log_in_dir ,const std::string&basename ,LogLevel level)
{
	basename_ =basename ;
	console_level_ = logfile_level_  =level ;
	log_dir_ = log_in_dir ;

	bool dir_ok = create_log_dir() ;
	if(!dir_ok)
		return;
	start_logging() ;

}

void logger::start_logging()
{
	std::string log_full_file_name = log_dir_ + basename_ ; 

	logfilePtr_.reset(new async_logging(log_full_file_name));
	logfilePtr_->start() ; 
}

bool logger::create_log_dir()
{
#ifdef WIN32
	std::string::size_type pos = 0;
	std::string::size_type srclen = log_dir_.size();
	while(std::string::npos != (pos = log_dir_.find('/', pos)))
	{
		log_dir_.replace(pos, 1, "\\");
		pos += 1;
	}
	if(log_dir_.at(log_dir_.size()-1)!='\\')
		log_dir_+="\\" ; 

	BOOL result = MakeSureDirectoryPathExists(log_dir_.c_str());
	if(result == FALSE)
	{
		printf("MakeSureDirectoryPathExists error:[%s]\r\n" ,log_dir_.c_str() );
	}
	return result ;
#else
#error not support
#endif
}


void logger::log(LogLevel level ,const char *logstr, ... )
{
	if( (level<console_level_)
		&&(level<logfile_level_))
		return ; 


	char buffer[MAX_LOG_BUFFER_SIZE] ; 

	SYSTEMTIME curST ;
	::GetLocalTime(&curST);

	sprintf_s(buffer ,MAX_LOG_BUFFER_SIZE ,"%04d-%02d-%02d %02d:%02d:%02d.%03d,%s," ,
		curST.wYear , curST.wMonth ,curST.wDay ,curST.wHour ,curST.wMinute ,curST.wSecond ,
		curST.wMilliseconds , logLevelStr[level]) ;
	int head_len = strlen(buffer);

	va_list args;
	int     len;
	va_start( args, logstr );

	len = _vscprintf( logstr, args ) + 1; 
	if(len>(MAX_LOG_BUFFER_SIZE - head_len-2))
		return ;
	vsprintf( &buffer[head_len], logstr, args ); 

	strcat(buffer ,"\r\n");

	if(is_console_log)
		console_output( level , buffer , head_len + len +2 ); //控制台输出

	if(is_file_log)
		logfile_output(level,buffer , head_len + len +2 );	 //文件追加
}

void logger::console_output(LogLevel level , const char* buffer , int len )
{
	if(level < console_level_)
		return ;

	printf(buffer);

}
void logger::logfile_output(LogLevel level , const char* buffer , int len)
{
	if(level < logfile_level_)
		return ;

	if(logfilePtr_)
		logfilePtr_->append(buffer , len-1  );
}

static void trim_space_and_lower(std::string& str)
{
	boost::trim(str);
	boost::to_lower(str);
}

bool logger::config_set_log_level(std::string& cfg , LogLevel& level)
{
	if(cfg.empty())
	{
		level = DEBUG_LEVEL  ;
		return true ;
	}
	else
	{
		trim_space_and_lower(cfg);
		if(cfg == "debug" || cfg =="dbg")
		{
			level =	DEBUG_LEVEL;
		}
		else if(cfg == "info" || cfg=="infomation")
		{
			level = INFO_LEVEL;
		}
		else if(cfg == "warn" || cfg=="warning")
		{
			level = WARN_LEVEL;
		}
		else if(cfg == "err" || cfg =="error")
		{
			level = ERR_LEVEL;
		}
		else
		{
			printf("error:读取 level 错误，不可识别的类型[%s]\r\n" , cfg.c_str() );
			return false ;
		}
	}

	return true ; 
}


#define  INNER_DEBUG	

bool logger::load_config(const std::string& filename)
{    
	boost::scoped_ptr<TiXmlDocument> config_document( new TiXmlDocument(filename.c_str()));
	assert(config_document && "config_document == NULL");
	if(config_document == NULL)
	{
		printf("error: load log config file failed!\r\n");
		return false ; 
	}
	config_document->LoadFile();

	TiXmlElement *RootElement = config_document->RootElement(); //log_config
	if(RootElement == NULL)
	{
		printf("error:this file is not a log  config-1!\r\n");
		return false ;
	}

	std::string root_node_name = RootElement->Value() ; 
	if(root_node_name!="log_config")
	{
		printf("error:this file is not a log  config-2!\r\n");
		return false;
	}

	//读取log_dst配置
	TiXmlElement*  log_dst_elem = RootElement->FirstChildElement("log_dst");
	if(log_dst_elem == NULL)
	{
		printf("error:load log_dst error!");
		return false ;
	}
	std::string log_dst_str = log_dst_elem->FirstChild()->Value() ;
	if(log_dst_str.empty())
	{
		//losg_dst没有设置，则默认全部不输出
		is_console_log = false;
		is_file_log = false;
	}
	else
	{
		trim_space_and_lower(log_dst_str);

#ifdef INNER_DEBUG
		printf("log_dst:[%s]\r\n" , log_dst_str.c_str() );
#endif

		if(log_dst_str == "console")
		{
			is_console_log = true;
			is_file_log = false;
		}
		else if(log_dst_str == "file")
		{
			is_console_log = false;
			is_file_log = true;
		}
		else if(log_dst_str == "both")
		{
			is_console_log = true;
			is_file_log = true;
		}
		else
		{
			is_console_log = false;
			is_file_log = false;
		}
	}

	//读取console_level配置
	TiXmlElement* console_level_elem = RootElement->FirstChildElement("console_level");
	if(console_level_elem == NULL)
	{
		printf("error:load console_level error!");
		return false ;
	}
	std::string console_level_str = console_level_elem->FirstChild()->Value() ;

	bool set_result =config_set_log_level(console_level_str , console_level_);
	if(!set_result)
		return false ; 

	//读取file_level配置
	TiXmlElement* file_level_elem = RootElement->FirstChildElement("file_level");
	if(file_level_elem == NULL)
	{
		printf("error:load file_level error!");
		return false ;
	}
	std::string file_level_str = file_level_elem->FirstChild()->Value() ;

	set_result =config_set_log_level(file_level_str , logfile_level_);
	if(!set_result)
		return false ; 

	//读取log_dir配置
	TiXmlElement* log_dir_elem = RootElement->FirstChildElement("log_dir");
	if(log_dir_elem == NULL)
	{
		printf("error:load log_dir error!");
		return false ;
	}
	std::string log_dir_str = log_dir_elem->FirstChild()->Value() ;
	boost::trim(log_dir_str);
#ifdef INNER_DEBUG
	printf("log_dir:[%s]\r\n" ,log_dir_str.c_str() );
#endif

	//读取basename配置
	TiXmlElement* basename_elem = RootElement->FirstChildElement("basename");
	if(basename_elem == NULL)
	{
		printf("error:load basename error!");
		return false ;
	}
	std::string basename_str = basename_elem->FirstChild()->Value() ;
	boost::trim(basename_str);
#ifdef INNER_DEBUG
	printf("basename:[%s]\r\n" ,basename_str.c_str() );
#endif

	basename_ =basename_str ;
	log_dir_ = log_dir_str ;

	assert(!basename_.empty());
	assert(!log_dir_.empty());
	bool dir_ok = create_log_dir() ;
	if(!dir_ok)
		return false ;

	start_logging() ;
	return true ; 
}

void logger::stop()
{
if(logfilePtr_)
logfilePtr_->stop();
}