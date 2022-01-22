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

//=============================================================================
//					          系统的初始化和终止
//=============================================================================
// 函数1：LoadSocketLib()初始化WINSOCK
// 函数2：Start()启动服务器
// 函数3：Stop()停止服务器
// 
// 

/**************************************
*函数名称：LoadSocketLib()
*函数功能：初始化WINSOCK
*函数参数：无
*函数返回：BOOL:false表示初始化失败，true表示初始化成功
*函数说明：按照2.2版本初始化WINSOCK，一般不可能失败
**************************************/
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

/**************************************
*函数名称：Start()
*函数功能：启动服务器
*函数参数：int port：服务器端口号（默认为DEFAULT_PORT（10240））
*函数返回：BOOL:false表示启动失败，true表示启动成功
*函数说明：完成以下事务：
*			1.初始化线程互斥量
*			2.建立系统退出的时间通知
*			3.调用_InitializeIOCP()初始化IOCP
*			4.调用_InitializeListenSocket()初始化socket
**************************************/
bool CIocpModel::Start(int port)
{
	b_nPorts = port;
	//初始化线程互斥量
	InitializeCriticalSection(&b_csContextList);
	//建立系统退出的时间通知
	b_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	//初始化IOCP
	if (!_InitializeIOCP())
	{
		this->_ShowMessage("初始化IOCP失败！");
		return false;
	}
	else
	{
		this->_ShowMessage("初始化IOCP完毕！");
	}
	//初始化socket
	if (!_InitializeListenSocket())
	{
		this->_ShowMessage("监听Socket初始化失败！");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("监听Socket初始化完毕！");
	}
	this->_ShowMessage("系统准备就绪，等候连接....");
	return true;
}

/**************************************
*函数名称：Stop()
*函数功能：停止服务器
*函数参数：无
*函数返回：无
*函数说明：开始发送系统退出消息，退出完成端口和线程资源
**************************************/
void CIocpModel::Stop()
{
	if (b_pListenContext != nullptr
		&& b_pListenContext->b_Socket != INVALID_SOCKET)
	{
		//激活关闭消息通知
		SetEvent(b_hShutdownEvent);
		for (int i = 0; i < b_nThreads; i++)
		{
			//通知所有的完成端口操作退出
			PostQueuedCompletionStatus(b_hIOCompletionPort, 0, (DWORD)EXIT_CODE, NULL);
		}
		//等待所有客户端资源退出
		WaitForMultipleObjects(b_nThreads, b_phWorkerThreads, TRUE, INFINITE);
		//清楚客户端列表信息
		this->_ClearContextList();
		//释放其他资源
		this->_DeInitialize();
		this->_ShowMessage("停止监听");
	}
	else
	{
		b_pListenContext = nullptr;
	}
}

/**************************************
*函数名称：_InitializeIOCP()
*函数功能：初始化IOCP
*函数参数：无
*函数返回：BOOL:false表示启动失败，true表示启动成功
*函数说明：
**************************************/
bool CIocpModel::_InitializeIOCP()
{
	this->_ShowMessage("初始化IOCP-InitializeIOCP()");
	b_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (b_hIOCompletionPort == nullptr)
	{
		this->_ShowMessage("建立完成端口失败！错误代码： %d!", WSAGetLastError());
		return false;
	}
	//根据本机中的处理器数量，建立对应的线程数
	b_nThreads = WORKER_THREADS_PER_PROCESS * _GetNumOfProcessors();
	//为工作者线程初始化句柄
	b_phWorkerThreads = new HANDLE[b_nThreads];
	DWORD nThreadID = 0;
	for (int i = 0; i < b_nThreads; i++)
	{
		WorkerThreadParam* pThreadParams = new WorkerThreadParam;
		pThreadParams->pIocpModel = this;
		pThreadParams->nThreadNo = i + 1;
		b_phWorkerThreads[i] = CreateThread(0, 0, _WorkerThread,
			(void*)pThreadParams, 0, &nThreadID);
		pThreadParams->nThreadId = nThreadID;
	}
	this->_ShowMessage("建立WorkerThread %d 个",b_nThreads);
	return true;
}

/**************************************
*函数名称：_InitializeListenSocket()
*函数功能：
*函数参数：无
*函数返回：BOOL:false表示启动失败，true表示启动成功
*函数说明：
**************************************/
bool CIocpModel::_InitializeListenSocket()
{
	this->_ShowMessage("初始化Socket-InitializeListenSocket()");
	b_pListenContext = new SocketContext;
	b_pListenContext->b_Socket = WSASocket(PF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if()
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