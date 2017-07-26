# filelogger
记录日志文件，可以通过配置文件对日志进行配置。

	threadctl_use_windows_threads();
	bool load_success = LOG_LOAD_CONFIG("logconfig");
	LOG_DBG("logger %d %s" , 123 , "logger");
