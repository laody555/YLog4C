#ifndef  __LAODY_LOG4C__
#define  __LAODY_LOG4C__

#include <stdarg.h>
#include <stdio.h>

// Author   : laody  -- Email -> laody@163.com
// CopyRight: 这是laody完全自主开发的代码，请原样保留这个信息

// 通用日志类 
// 使用示例:
//     YLog4C log;
//     log.Fatal("示例%d,这是致命错误日志.",1);
//     log.Error("示例%d,这是一般错误日志.",2);
//     log.Warn ("示例%d,这是告警信息日志.",3);
//     log.Info ("示例%d,这是正常信息日志.",4);
//     log.Buff ("示例5,这是码流信息日志.",18);
//     log.Debug("示例%d,这是调试信息日志.",6);

typedef void (*LOG4C_FUNC)(const char*);

class YLog4C
{
public:
	YLog4C();
	~YLog4C();

	// 日志文件名：目录+应用程序名称+“_"+日期+".log"
	static void SetLogAppname(const char *lpszAppname); //设置应用程序名称
	static void SetLogDir(const char *lpszDir); //设置目录
	static void SetLogFileMaxcnt(int n);//设置日志文件的最多个数,如每天一个文件,则设置每天最多个数
	static void SetLog2Console(bool b); //设置是否输出到控制台
	static void SetLog2File(bool b);    //设置是否输出到文件
	static void SetLog2Func(LOG4C_FUNC pFunc); //设置是否输出到函数
	static void SetLogLevel(int n);    //设置日志级别
	static void SetLogDiary(bool b);   //设置是否每天一个文件
	static void SetLogMaxsize(int n);  //设置日志文件最大长度 100K<n<2G,默认100M
	static bool SetLog2MultiThread(bool b); //设置日志为多线程模式，集中到一起写文件，减少文件IO
  static bool SetLogThreadID(bool b); //设置是否打印线程ID,默认不打印
  static bool SetFlushRT(bool b);     //设置是否实时刷新

	void Info(const char *lpszFmt,...);
	void LogBuff(const char *lpszFile,int nLine,const char *lpBuf,int nSize);

	#define Buff(lpBuf,nSize) LogBuff(__FILE__,__LINE__,lpBuf,nSize);

public:
	enum
	{ //日志级别
		LOG_FATAL   = 0,    //严重错误
		LOG_ERROR   = 1,    //错误
		LOG_WARN    = 2,    //警告
		LOG_INFO    = 3,    //信息
		LOG_DEBUG   = 4,    //调试
		LOG_BUFF    = 5,    //码流
	}LOG_LEVEL;

public: //这里虽为public函数，但不为公开调用而设计，请勿外部调用
#ifdef WIN32
	YLog4C& Log4C(const char *lpsz,int n) { m_lpszFile=lpsz;m_nLine=n;return *this;}

	#define Fatal Log4C(__FILE__,__LINE__).LogFatal
	#define Error Log4C(__FILE__,__LINE__).LogError
	#define Warn  Log4C(__FILE__,__LINE__).LogWarn
	#define Debug Log4C(__FILE__,__LINE__).LogDebug

	void LogFatal(const char *lpszFmt,...);
	void LogError(const char *lpszFmt,...);
	void LogWarn (const char *lpszFmt,...);
	void LogBuff (const char *lpBuf,int nSize);
	void LogDebug(const char *lpszFmt,...);
#else
	#define Fatal(fmt,arg...) Log(YLog4C::LOG_FATAL,__FILE__,__LINE__,fmt,##arg)
	#define Error(fmt,arg...) Log(YLog4C::LOG_ERROR,__FILE__,__LINE__,fmt,##arg)
	#define Warn(fmt,arg...)  Log(YLog4C::LOG_WARN ,__FILE__,__LINE__,fmt,##arg)
	#define Debug(fmt,arg...) Log(YLog4C::LOG_DEBUG,__FILE__,__LINE__,fmt,##arg)
#endif

public:
	void Log(int nLevel,const char *lpszFile,int nLine,const char *lpszFmt,...);
protected:	
	void LogV(int nLevel,const char *lpszFile,int nLine,const char *lpszFmt,va_list argList);
	
	bool OpenLog();
	void CloseLog();
	
	char m_strPrvFile[256];
	FILE *m_pFile;

private:
	const char *m_lpszFile;
	int   m_nLine;
};

#endif // __LAODY_LOG4C__

