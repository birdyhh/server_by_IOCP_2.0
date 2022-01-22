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
*函数说明：完成以下事务：
*			1.初始化完成端口
*			2.建立对应的线程数
*			3.为工作者线程初始化句柄
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
*函数功能：初始化Socket
*函数参数：无
*函数返回：BOOL:false表示初始化失败，true表示初始化成功
*函数说明：完成以下事务：
*			1.生成socket后，绑定到完成端口中，绑定到ip地址和端口上，然后开始监听
*			2.获取并保存AcceptEx函数和GetAcceptExSockAddrs函数的指针
*			3.创建10个套接字，投递AcceptEx请求
**************************************/
bool CIocpModel::_InitializeListenSocket()
{
	this->_ShowMessage("初始化Socket-InitializeListenSocket()");
	b_pListenContext = new SocketContext;
	//生成重叠IO的socket
	b_pListenContext->b_Socket = WSASocket(PF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == b_pListenContext->b_Socket)
	{
		this->_ShowMessage("WSASocket() 失败，err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("创建WSASocket() 完成");
	}
	//将listen Socket绑定到完成端口
	if (NULL == CreateIoCompletionPort((HANDLE)b_pListenContext->b_Socket,
		b_hIOCompletionPort, (DWORD)b_pListenContext, 0));
	{
		this->_ShowMessage("绑定失败！ err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	//填充地址信息
	//服务器地址信息
	sockaddr_in serverAddress;
	ZeroMemory(&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(b_nPorts);
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

	//绑定端口和地址
	if (SOCKET_ERROR == bind(b_pListenContext->b_Socket,
		(sockaddr*)&serverAddress, sizeof(serverAddress)))
	{
		this->_ShowMessage("bind()函数执行失败");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("bind() 完成");
	}

	//监听端口和地址
	if (SOCKET_ERROR == listen(b_pListenContext->b_Socket,MAX_LISTEN_SOCKET))
	{
		this->_ShowMessage("listen()函数执行失败");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("listen() 完成");
	}

	//提前获取AcceptEx函数指针，不必在后续多次调用AcceptEx函数过程中使用WSAIoctl函数
	//避免多次使用WSAIoctl函数这个很影响性能的操作
	DWORD dwBytes = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(b_pListenContext->b_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &b_lpfnAcceptEx,
		sizeof(b_lpfnAcceptEx), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoct1 未能获取AcceptEx函数指针。错误代码: %d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	//同理，获取GetAcceptExSockAddrs()函数指针
	if (SOCKET_ERROR == WSAIoctl(b_pListenContext->b_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs), &b_lpfnGetAcceptExSockAddrs,
		sizeof(b_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoct1 未能获取GetAcceptExSockAddrs函数指针。错误代码: %d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	//为AcceptEx 准备参数，然后投递AcceptEx I/O请求
	//创建10个套接字，投递AcceptEx请求，即共有10个套接字进行accept操作；
	//可修改宏MAX_POST_ACCEPT，改变同时投递的AcceptEx请求的数量，默认为10
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		//新建一个IO_CONTEXT
		IoContext* pIoContext = b_pListenContext->GetNewIoContext();
		if (pIoContext && !this->_PostAccept(pIoContext))
		{
			b_pListenContext->RemoveContext(pIoContext);
			return false;
		}
	}
	this->_ShowMessage("投递 %d 个AcceptEx请求完毕", MAX_POST_ACCEPT);
	return true;
}

/**************************************
*函数名称：_DeInitialize()
*函数功能：释放掉所有资源
*函数参数：无
*函数返回：无
*函数说明：完成以下事务：
*			1.
*			2.
*			3.
**************************************/
void CIocpModel::_DeInitialize()
{
	//删除客户端互斥量
	DeleteCriticalSection(&b_csContextList);
	//关闭系统退出时间句柄
	RELEASE_HANDLE(b_hShutdownEvent);
	//释放工作者线程句柄指针
	for (int i = 0; i < b_nThreads; i++)
	{
		RELEASE_HANDLE(b_phWorkerThreads[i]);
	}
	RELEASE_ARRAY(b_phWorkerThreads);
	//关闭IOCP句柄
	RELEASE_HANDLE(b_hIOCompletionPort);
	//关闭监听socket
	RELEASE_POINTER(b_pListenContext);
	this->_ShowMessage("释放资源完毕");
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