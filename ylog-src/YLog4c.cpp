#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>

#ifdef WIN32
	#include <io.h>
	#include <process.h>
#else
	#include <pthread.h>

	#include <sys/types.h>
	#include <unistd.h>
	
	#define _snprintf   snprintf
	#define _vsnprintf  vsnprintf
#endif
#include "YLog4c.h"


#ifdef WIN32
//#include <winbase.h>
  #ifndef _WINSOCKAPI_
    #include <winsock2.h>
  #endif
#endif

static char g_strLogAppname[80]={0}; //应用程序名称
static char g_strLogDir[256]={0};   //目录
static int  g_nLogFileMaxcnt=4;  
static bool g_bLog2Console=true;
static bool g_bLog2File=true;
static LOG4C_FUNC g_pLog2Func=NULL;
static bool g_bLogDiary=true;
static int  g_nLogLevel=YLog4C::LOG_DEBUG;
static int  g_nLogMaxsize=100*1048576; 
static bool g_bLogThreadID=false;
static bool g_bLogFlushRT=true;

const static char *g_szLogLevelName[]={"FATAL","ERR","WRN","INF","DBG","BUF"};

#ifndef WIN32
	#define _isatty  isatty
	#define _fileno  fileno
#endif


#ifndef WIN32
static void GetProcName_Linux(char *pszPath,int nSize)
{
	char strTmp[128];
	sprintf(strTmp, "/proc/%d/exe" ,getpid() );
	int ret = readlink(strTmp, pszPath, nSize );
	if ( ret < 0)//读取链接内容失败
	{
		pszPath[0]='\0';
	}
}

static const char *GetModuleFileName()
{
	static char tmpPath[1024]={0};

	if(tmpPath[0]!='\0')
		return tmpPath;

	memset(tmpPath,0,sizeof(tmpPath));
	
//	#ifdef __LINUX
		GetProcName_Linux(tmpPath,sizeof(tmpPath));
//	#else
//		#error: __LINUX or __SunOS not define.
//	#endif
	return tmpPath;
}

#endif //__LINUX


// -------------------------------------------------------------------------------------
static void __lfc_havarest()
{
#ifdef WIN32
  Sleep(1);
#else
  struct timespec tv;
  tv.tv_sec=0;
  tv.tv_nsec=1000000;
  nanosleep(&tv,NULL);
#endif
}

static unsigned long __lfc_getthread_id()
{
#ifdef WIN32
	return GetCurrentThreadId();
#else
	return pthread_self();
#endif
}

static bool volatile g_bLockLog4C=false;

static void __lfc_unlock_log4c()
{
	g_bLockLog4C=false;
}

static bool __lfc_testandlock_log4c()
{
	bool   bRes=false;
	if(g_bLockLog4C)
	{
		int i;
		for(i=0;i<5000;i++)
		{
			if(!g_bLockLog4C)	break;
			__lfc_havarest();
		}
//		if(i>=500)
//			__lfc_unlock_log4c();
	}
	else
	{
		static unsigned long volatile dwThrID=0;

		g_bLockLog4C=true;
		dwThrID=__lfc_getthread_id();
		int i;
		for(i=0;i<5;i++)
			__lfc_havarest();
		if(dwThrID!=__lfc_getthread_id())
		{
			for(i=0;i<500;i++)
			{
				if(!g_bLockLog4C)	break;
				__lfc_havarest();
			}
		}
		else
			bRes=true;
	}
	return bRes;
}
static void Log4CCloseLog(FILE* &pFile)
{
	if(pFile==NULL)	return;
	fclose(pFile);
	pFile=NULL;
}

static bool Log4COpenLog(char* pszPrvFile,FILE* &pFile)
{
	char strFile[256],strHead[256],strDate[80];
	time_t tNow=time(NULL);
	struct tm td;

#ifdef WIN32  
  td=*localtime(&tNow);
#else
  localtime_r(&tNow,&td);
#endif

	if(g_strLogDir[0]=='\0')
	{
		char strExe[256];		
		memset(strExe,0,sizeof(strExe));
	#ifdef WIN32
//		GetModuleFileName(AfxGetApp()->m_hInstance,strExe,sizeof(strExe)-1);
		GetModuleFileName(NULL,strExe,sizeof(strExe)-1);
	#else	
//		strcpy(strExe,getenv("_"));
		strcpy(strExe,GetModuleFileName());
	#endif
	
		for(int i=strlen(strExe)-1;i>0;i--)
		{
			if(strExe[i]=='\\' || strExe[i]=='/')
			{
				strExe[i]='\0';
				break;
			}
		}
		strcpy(g_strLogDir,strExe);
	}
	if(g_strLogAppname[0]=='\0')
	{
		char strExe[256];		
		memset(strExe,0,sizeof(strExe));
	#ifdef WIN32
//		GetModuleFileName(AfxGetApp()->m_hInstance,strExe,sizeof(strExe)-1);
		GetModuleFileName(NULL,strExe,sizeof(strExe)-1);
	#else	
//		strcpy(strExe,getenv("_"));
		strcpy(strExe,GetModuleFileName());
	#endif

		for(int i=strlen(strExe)-1;i>0;i--)
		{
			if(strExe[i]=='\\' || strExe[i]=='/')
			{
				strcpy(g_strLogAppname,strExe+i+1);
				break;
			}
		}
		char *ptr;
	#ifdef WIN32
		if((ptr=strrchr(g_strLogAppname,'.'))!=NULL)	*ptr='\0';
	#else
		if((ptr=strchr(g_strLogAppname,'-'))!=NULL)	*ptr='\0';
	#endif
	}

	sprintf(strDate,"%04d%02d%02d",td.tm_year>70?td.tm_year+1900:td.tm_year+2000,td.tm_mon+1,td.tm_mday);

	if(g_bLogDiary)
		sprintf(strHead,"%s/%s_%s",g_strLogDir,g_strLogAppname,strDate);
	else
		sprintf(strHead,"%s/%s",g_strLogDir,g_strLogAppname);
	
	sprintf(strFile,"%s.log",strHead);

	if(pszPrvFile[0]=='\0')
		strcpy(pszPrvFile,strFile);
	else if(strcmp(strFile,pszPrvFile)!=0)
	{ //日期已变
		Log4CCloseLog(pFile);
		strcpy(pszPrvFile,strFile);
	}

	bool bLock=false;
	try
	{
		struct stat fst;
		memset(&fst,0,sizeof(fst));					
		if (stat(strFile,&fst)==0)
		{
			if (fst.st_size >= g_nLogMaxsize)
			{ //超过文件最大长度
				Log4CCloseLog(pFile);

				bLock=__lfc_testandlock_log4c();
				if(bLock)
				{
					char strTemp[256],strLast[256];
					//将所有历史文件名按顺序后移,如果文件数量超过g_nLogFileMaxcnt,则最后的那个文件会被删除
					sprintf(strTemp,"%s_%d.log",strHead,g_nLogFileMaxcnt);
					if (stat(strTemp,&fst) == 0)
          {
						remove(strTemp);
          }
					for (int i=g_nLogFileMaxcnt-1;i>0;i--)
					{
						sprintf(strTemp,"%s_%d.log",strHead,i);
						if (stat(strTemp,&fst) == 0)
						{
							sprintf(strLast,"%s_%d.log",strHead,i+1);
							rename(strTemp,strLast);
						}
					}
					//将当前日志文件重命名为第一个文件
					sprintf(strLast,"%s_1.log",strHead);	
					rename(strFile,strLast);
					__lfc_unlock_log4c();  
				}
			}
		}
		if(pFile!=NULL)	return true;

		if((pFile=fopen(strFile,"a+b"))==NULL)
		{
			fprintf(stderr,"open LogFile false [%s]!\n",strFile);
			return false;
		}
	}
	catch(...)
	{
		if(bLock)
			__lfc_unlock_log4c();
		fprintf(stderr,"open or rename LogFile false [%s]!\n",strFile);
		return false;
	}
	return true;
}

// -------------------------------------------------------------------------------------
#define LOG4C_MULTITHR_BUFSIZE  (65536)
class YLog4CThr
{
public:
	YLog4CThr()
	{
		m_pBufPtr=new char[LOG4C_MULTITHR_BUFSIZE];
		m_nBufLen=0;
		m_strPrvFile[0]='\0';
		m_pFile=NULL;
		m_bRunning=true;
		m_tTerminated=0;

#ifdef WIN32
		m_hMutex = CreateMutex( NULL, false,NULL);
#else
	pthread_mutex_init(&m_hMutex,NULL);
#endif
	}
	~YLog4CThr()
	{
		delete m_pBufPtr;
#ifdef WIN32
#else
	pthread_mutex_destroy(&m_hMutex);
#endif
	}
	
	bool Lock();
	void Unlock();
	
	void WriteLog(const char *lpszLog,int nLen);
	void FlushLog();
	
	char *m_pBufPtr;
	int   m_nBufLen;
	char  m_strPrvFile[256];
	FILE *m_pFile;
	bool  m_bRunning;
	time_t m_tTerminated;
	
#ifdef WIN32
	HANDLE m_hMutex;  
#else
	pthread_mutex_t m_hMutex;
#endif	
};

static YLog4CThr *g_ptheLog4CThrPtr=NULL;

bool YLog4CThr::Lock()
{
#ifdef WIN32
	WaitForSingleObject(m_hMutex,INFINITE);  
#else
	pthread_mutex_lock(&m_hMutex);
#endif	
	return true;
}

void YLog4CThr::Unlock()
{
#ifdef WIN32
	ReleaseMutex(m_hMutex);  
#else
	pthread_mutex_unlock(&m_hMutex);
#endif
}

void YLog4CThr::FlushLog()
{
	if(m_pFile==NULL)	return;
	if(m_nBufLen<=0)	return;
	try
	{
		fwrite(m_pBufPtr,1,m_nBufLen,m_pFile);
		m_nBufLen=0;
	}
	catch(...)
	{
	}
}

void YLog4CThr::WriteLog(const char *lpszLog,int nLen)
{
	Lock();
	try
	{
		if(Log4COpenLog(m_strPrvFile,m_pFile))
		{
			if(nLen + m_nBufLen >= LOG4C_MULTITHR_BUFSIZE)
				FlushLog();
			if(nLen >= LOG4C_MULTITHR_BUFSIZE)
			{
				try
				{
					fwrite(lpszLog,1,nLen,m_pFile);
				}
				catch(...)
				{
				}
			}
			else
			{
				memcpy(m_pBufPtr+m_nBufLen,lpszLog,nLen);
				m_nBufLen += nLen;
			}		
		}
	}
	catch(...)
	{
	}	
	Unlock();
}


static void Log4CStopMultiThr()
{
	if(g_ptheLog4CThrPtr==NULL)	return;
	g_ptheLog4CThrPtr->m_bRunning=false;
	for(int i=0;i<10000;i++)
	{
		if(g_ptheLog4CThrPtr->m_tTerminated != 0)	break;
		__lfc_havarest();
	}
	delete g_ptheLog4CThrPtr;
	g_ptheLog4CThrPtr=NULL;
}

struct _log4c_loc_auto
{
	_log4c_loc_auto(){}
	~_log4c_loc_auto()
	{
		Log4CStopMultiThr();
	}
};
static struct _log4c_loc_auto g_theLog4CLocalAutoObj;


#ifdef WIN32
unsigned int __stdcall Log4CThreadFunc(void *pArg)
#else
void*                  Log4CThreadFunc(void *pArg)
#endif
{
	YLog4CThr *pThr=(YLog4CThr*)pArg;
	pThr->m_tTerminated=0;
	time_t tPrv=time(NULL);
	time_t tNow;
	try
	{
		while(pThr->m_bRunning)
		{
			tNow=time(NULL);
			if(tNow>tPrv)
			{
				tPrv=tNow;
				if(pThr->m_pFile!=NULL && pThr->m_nBufLen>0)
				{
					pThr->Lock();
					pThr->FlushLog();
					pThr->Unlock();
				}
			}
			__lfc_havarest();
		}
		pThr->m_tTerminated=time(NULL);
	}
	catch(...)
	{
	}	
	return 0;
}

bool YLog4C::SetLog2MultiThread(bool bMultiThr)
{ //设置日志为多线程模式，集中到一起写文件，减少文件IO
	if(bMultiThr)
	{
		
	}
	else
	{
	}
	return true;
}


// -------------------------------------------------------------------------------------
YLog4C::YLog4C()
{
	m_strPrvFile[0]='\0';
	m_pFile=NULL;
	m_lpszFile=NULL;
}

YLog4C::~YLog4C()
{
	CloseLog();
}

void YLog4C::SetLogFileMaxcnt(int n)
{//设置日志文件的最多个数,如每天一个文件,则设置每天最多个数
	assert(n>=1);
	assert(n<=100);
	g_nLogFileMaxcnt=n;
}

void YLog4C::SetLogAppname(const char *lpszAppname)
{ //设置应用程序名称
	assert(lpszAppname!=NULL);
	assert(lpszAppname[0]!='\0');
	assert(strlen(lpszAppname)<sizeof(g_strLogAppname)-1);
	strcpy(g_strLogAppname,lpszAppname);
}

void YLog4C::SetLogDir(const char *lpszDir)
{ //设置目录
	assert(lpszDir!=NULL);
	assert(lpszDir[0]!='\0');
	assert(strlen(lpszDir)<sizeof(g_strLogDir)-10);	
	strcpy(g_strLogDir,lpszDir);
	int nLen;
	nLen=strlen(g_strLogDir);
	char ch=g_strLogDir[nLen-1];
#ifdef WIN32
	if(nLen>2 && g_strLogDir[1]==':' && (ch=='\\' || ch=='/'))
		g_strLogDir[nLen]='\0';
#else
	if(nLen>0 && (ch=='\\' || ch=='/'))
		g_strLogDir[nLen]='\0';
#endif
}

void YLog4C::SetLog2Console(bool b)
{ //设置是否输出到控制台
	g_bLog2Console=b;
}

void YLog4C::SetLog2File(bool b)
{ //设置是否输出到文件
	g_bLog2File=b;
}

void YLog4C::SetLog2Func(LOG4C_FUNC pFunc)
{ //设置是否输出到函数
	g_pLog2Func=pFunc;
}

void YLog4C::SetLogLevel(int n)
{ //设置日志级别
	g_nLogLevel=n;
}

void YLog4C::SetLogDiary(bool b)
{ //设置是否每天一个文件
	g_bLogDiary=b;
}

void YLog4C::SetLogMaxsize(int n)
{ //设置日志文件最大长度 100K < n < 2G 
#ifndef _DEBUG
	if(n<100*1024 || n<0 || n>=0x7fffffff)
	{
		fprintf(stderr,"不合理的日志文件最大长度\n");
		return;
	}
#endif
	g_nLogMaxsize=n;
}

bool YLog4C::SetLogThreadID(bool b) 
{ //设置是否打印线程ID,默认不打印
  bool bPrv=g_bLogThreadID;
  g_bLogThreadID=b;
  return bPrv;
}

bool YLog4C::SetFlushRT(bool b)
{//设置是否实时刷新
  bool bPrv=g_bLogFlushRT;
  g_bLogFlushRT=b;
  return bPrv;
}

bool YLog4C::OpenLog()
{
	return Log4COpenLog(m_strPrvFile,m_pFile);
}

void YLog4C::CloseLog()
{
	Log4CCloseLog(m_pFile);	
}

void YLog4C::LogV(int nLevel,const char *lpszFile,int nLine,const char *lpszFmt,va_list argList)
{
	assert(nLevel>=LOG_FATAL && nLevel<=LOG_BUFF);
	if(nLevel>g_nLogLevel)	return;

	const char *lpszName;
	lpszName=NULL;
	if(lpszFile!=NULL && lpszFile[0]!='\0')
	{
		lpszName=lpszFile;
		for(int e=strlen(lpszFile)-1;e>0;e--)
		{
			if(lpszFile[e]=='/' || lpszFile[e]=='\\')
			{
				lpszName=lpszFile+e+1;
				break;
			}
		}
	}

	time_t tNow=time(NULL);
	struct tm td;
#ifdef WIN32
  td=*localtime(&tNow);
#else
  localtime_r(&tNow,&td);
#endif

	if(g_bLog2File)
	{
		if(g_ptheLog4CThrPtr==NULL)
		{
		if(!OpenLog())	return;
	}
	}

	char strTag[256],strPID[80],strTID[80];
	strPID[0]='\0';
#ifndef WIN32
	sprintf(strPID,"%d",getpid());
#endif

  if(!g_bLogThreadID)
    strTID[0]='\0';
  else
  {
    unsigned long nThrID=__lfc_getthread_id();
    #ifndef WIN32
      static int nSizeofLong=sizeof(long);
      if(nSizeofLong==8)
      { //64位Linux系统
        nThrID=nThrID/10489856;
      }
    #endif
    nThrID=nThrID%1000;
    sprintf(strTID,"T%03d",(int)nThrID);
  }

	if(lpszName==NULL)
		sprintf(strTag,"[%02d:%02d:%02d %s%s<%s>] ",td.tm_hour,td.tm_min,td.tm_sec,strPID,strTID,g_szLogLevelName[nLevel]);
	else
		sprintf(strTag,"[%02d:%02d:%02d %s%s<%s>%s#%d] ",td.tm_hour,td.tm_min,td.tm_sec,strPID,strTID,g_szLogLevelName[nLevel],lpszName,nLine);

	if(nLevel==LOG_BUFF)
		strcat(strTag,"\r\n");

#ifndef va_copy
	#define va_copy(d,s) ( (d) = (s))
#endif
	va_list arg;

	if(g_bLog2Console && _fileno(stdout)>0)
	{
		printf("%s",strTag);
		if(argList==NULL)
			fwrite(lpszFmt,1,strlen(lpszFmt),stdout);
		else
		{
			va_copy(arg,argList);
			vprintf(lpszFmt,arg);
			va_end(arg);
		}
		printf("\r\n");
	}
	if(g_pLog2Func!=NULL)
	{
		char strLog[8192];
		int nLen;
		if(argList==NULL)
		{
			nLen=_snprintf(strLog,sizeof(strLog)-2,"%s%s",strTag,lpszFmt);
			if(nLen<=0)
				strLog[sizeof(strLog)-2]='\0';
		}
		else
		{
			strcpy(strLog,strTag);
			int nTmp=strlen(strLog);
			va_copy(arg,argList);
			nLen=_vsnprintf(strLog+nTmp,sizeof(strLog)-nTmp-2,lpszFmt,arg);
			va_end(arg);
			if(nLen<=0)
				strLog[sizeof(strLog)-2]='\0';
		}
		g_pLog2Func(strLog);
	}
	if(g_bLog2File)
	{
		if(g_ptheLog4CThrPtr==NULL)
		{
		try
		{
			fprintf(m_pFile,"%s",strTag);
			if(argList==NULL)
			{
//				fprintf(m_pFile,lpszFmt);
				int nTmp=strlen(lpszFmt);
				fwrite(lpszFmt,1,nTmp,m_pFile);
			}
			else
			{
				va_copy(arg,argList);
				vfprintf(m_pFile,lpszFmt,arg);
				va_end(arg);
			}
			fprintf(m_pFile,"\r\n");
			if(g_bLogFlushRT)
			  fflush(m_pFile);

#ifdef WIN32
      CloseLog();
#endif
		}
		catch(...)
		{
			fprintf(stderr,"Fatal Error::write LogFile false!!!\n");
		}
	}
		else
		{
			char strLog[8192];
			int nLen;
			if(argList==NULL)
			{
				nLen=_snprintf(strLog,sizeof(strLog)-4,"%s%s",strTag,lpszFmt);
				if(nLen<=0)
					strLog[sizeof(strLog)-2]='\0';
			}
			else
			{
				strcpy(strLog,strTag);
				int nTmp=strlen(strLog);
				va_copy(arg,argList);
				nLen=_vsnprintf(strLog+nTmp,sizeof(strLog)-nTmp-2,lpszFmt,arg);
				va_end(arg);
				if(nLen<=0)
					strLog[sizeof(strLog)-2]='\0';
			}
			strcat(strLog,"\r\n");
			g_ptheLog4CThrPtr->WriteLog(strLog,strlen(strLog));
		}
	}
}

void YLog4C::Info(const char *lpszFmt,...)
{
	if(LOG_INFO>g_nLogLevel)	return; 
	va_list arg_ptr; 
	va_start(arg_ptr,lpszFmt);
	LogV(YLog4C::LOG_INFO,NULL,0,lpszFmt,arg_ptr);
	va_end(arg_ptr); 
}

static inline void Bin2HexLine(const char *lpBuf,int nLen,char *pszLine)
{
	char strTmp[80];
	int  nTmp,i,j;
	
	strcpy(pszLine,"  ");
	for(i=0;i<nLen;i++)
	{
		sprintf(strTmp,"%02X ",(unsigned char)lpBuf[i]);
		strcat(pszLine,strTmp);
		if(i==7)	strcat(pszLine," ");
	}
	nTmp=strlen(pszLine);
	for(j=nTmp;j<55;j++)
		pszLine[j]=' ';

	char ch;
	for(i=0;i<nLen;i++,j++)
	{
		ch=lpBuf[i];
		if((unsigned char)ch<32)
			pszLine[j]='.';
		else
			pszLine[j]=ch;
	}
	pszLine[j  ]='\r';
	pszLine[j+1]='\n';
	pszLine[j+2]='\0';
}

void YLog4C::LogBuff(const char *lpszFile,int nLine,const char *lpBuf,int nSize)
{
	if(LOG_BUFF>g_nLogLevel)	return; 

	char strLog[8192],*pLog;
	char strLine[256];
	
	int nHead;
	pLog=strLog;
	for(nHead=0;nHead<nSize;nHead+=1024)
	{
		int i,nLen;
		pLog[0]='\0';
		#ifndef min
			#define  min(a,b) ((a) < (b) ? (a) : (b))		
		#endif
		for(i=nHead;i<min(nHead+1024,nSize);i+=16)
		{
			nLen=min(nSize-i,16);
			Bin2HexLine(lpBuf+i,nLen,strLine);
			strcat(pLog,strLine);
		}
		nLen=strlen(pLog);
		LogV(LOG_BUFF,lpszFile,nLine,pLog,NULL);
	}
}

#define LOG4C_IMPL(lev,fmt)	 va_list arg_ptr; \
	if(lev>g_nLogLevel)	return; \
	va_start(arg_ptr,fmt); \
	LogV(lev,m_lpszFile,m_nLine,lpszFmt,arg_ptr); \
	va_end(arg_ptr); 

void YLog4C::Log(int nLevel,const char *lpszFile,int nLine,const char *lpszFmt,...)
{
	if(nLevel>g_nLogLevel)	return; 
	m_lpszFile=lpszFile;
	m_nLine=nLine;
	LOG4C_IMPL(nLevel,lpszFmt);
}

#ifdef WIN32

void YLog4C::LogFatal(const char *lpszFmt,...)
{
	LOG4C_IMPL(YLog4C::LOG_FATAL,lpszFmt);
} 

void YLog4C::LogError(const char *lpszFmt,...)
{
	LOG4C_IMPL(YLog4C::LOG_ERROR,lpszFmt);
} 

void YLog4C::LogWarn(const char *lpszFmt,...)
{
	LOG4C_IMPL(YLog4C::LOG_WARN,lpszFmt);
} 

void YLog4C::LogDebug(const char *lpszFmt,...)
{
	LOG4C_IMPL(YLog4C::LOG_DEBUG,lpszFmt);
} 

#endif //WIN32




