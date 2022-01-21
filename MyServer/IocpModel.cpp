#include "IocpModel.h"

#pragma comment(lib,"ws2_32.lib")

CIocpModel::CIocpModel() :
	b_nThreads(0),
	b_hShutdownEvent(nullptr),
	b_hIOCompletionPort(nullptr),
	b_phWorkerThreads(nullptr),
	b_strIP(DEFAULT_IP),
	b_nPorts(DEFAULT_PORT),
	b_lpfnAcceptEx(nullptr),
	b_lpfnGetAcceptExSockAddrs(nullptr),
	b_pListenContext(nullptr),
	acceptPostCount(0),
	connectCount(0)
{
	errorCount = 0;
	b_LogFunc = nullptr;
	this->LoadSocketLib();
}

CIocpModel::~CIocpModel()
{
	this->Stop();
	this->UnloadSocketLib();
}

///////////////////////////////////////////////////////////////////////////////
//   工作者线程：为IOCP请求服务的工作者线程
//每当在完成端口上出现了完成的数据包，就将其取出进行处理
///////////////////////////////////////////////////////////////////////////////
/******************************************************************************
*函数功能：线程函数，根据GetQueuedCompletionStatus返回的PostType情况进行处理；
*函数参数：lpParam是THREADPARAMS_WORKER类型指针；
*函数说明：GetQueuedCompletionStatus正确返回时说明操作已完成，
*		   第二个参数lpNumberOfBytes表示这一次套接字传输的字节数
******************************************************************************/
DWORD WINAPI CIocpModel::_WorkerThread(LPVOID lpParam)
{

}

bool CIocpModel::LoadSocketLib()
{
	WSADATA wsaData = { 0 };
	const int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (NO_ERROR != nRet)
	{
		this->_ShowMessage("初始化Winsock 2.2失败");
		return false;
	}
	return true;
}


bool CIocpModel::Start(int port)
{
	b_nPorts = port;
	//初始化线程互斥量
	InitializeCriticalSection(&b_csContextList);
	//建立系统退出的时间通知
	b_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	//初始化IOCP

}
//在主页面输出信息
void CIocpModel::_ShowMessage(const char* szFormat, ...)
{
	if (b_LogFunc)
	{
		char buf[265] = { 0 };
		va_list arglist;
		va_start(arglist, szFormat);
		vsnprintf(buf, sizeof(buf), szFormat, arglist);
		va_end(arglist);
		b_LogFunc(string(buf));
	}
}