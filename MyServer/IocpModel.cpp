#include "IocpModel.h"
#include <mstcpip.h> 

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
	WorkerThreadParam* pParam = (WorkerThreadParam*)lpParam;
	CIocpModel* pIocpModel = (CIocpModel*)pParam->pIocpModel;
	const int nThreadNo = pParam->nThreadNo;
	const int nThreadId = pParam->nThreadId;

	pIocpModel->_ShowMessage("工作者线程，No:%d, ID:%d", nThreadNo, nThreadId);
	//循环处理请求，知道接收到shutdown信息为止
	while (WAIT_OBJECT_0 != WaitForSingleObject(pIocpModel->b_hShutdownEvent, 0))
	{
		DWORD dwBytesTransfered = 0;
		OVERLAPPED* pOverlapped = nullptr;
		SocketContext* pSoContext = nullptr;
		const bool bRet = GetQueuedCompletionStatus(pIocpModel->b_hIOCompletionPort,
			&dwBytesTransfered, (PULONG_PTR)&pSoContext, &pOverlapped, INFINITE);
		IoContext* pIoContext = CONTAINING_RECORD(pOverlapped, IoContext, b_Overlapped);
		if (EXIT_CODE == (DWORD)pSoContext)
		{
			break;
		}
		if (!bRet)
		{
			const DWORD dwErr = GetLastError();
			if (!pIocpModel->HandleError(pSoContext, dwErr))
			{
				break;
			}
			continue;
		}
		else
		{
			if ((dwBytesTransfered == 0) && (PostType::RECV == pIoContext->b_PostType
				|| PostType::SEND == pIoContext->b_PostType))
			{
				pIoContext->OnConnectionClosed(pSoContext);
				pIoContext->_DoClose(pSoContext);
				continue;
			}
			else
			{
				switch (pIoContext->b_PostType)
				{
				case PostType::ACCEPT :
				{
					pIoContext->b_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoAccept(pSoContext, pIoContext);
				}
				break;

				case PostType::RECV :
				{
					pIoContext->b_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoRecv(pSoContext, pIoContext);
				}
				break;

				case PostType::SEND :
				{
					pIoContext->b_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoSend(pSoContext, pIoContext);
				}
				break;

				default:
					pIocpModel->_ShowMessage("_WorkThread中的m_OpType 参数异常");
					break;
				}
			}
		}
	}
	pIocpModel->_ShowMessage("工作者线程 %d 号退出", nThreadNo);
	RELEASE_POINTER(lpParam);
	return 0;
}

//=============================================================================
//					          系统的初始化和终止
//=============================================================================

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
		b_phWorkerThreads[i] = ::CreateThread(0, 0, _WorkerThread,
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
*函数说明：关闭所有工作的线程，关闭所有句柄，释放所有指针
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



//=============================================================================
//				 投递完成端口请求
//=============================================================================

/**************************************
*函数名称：_PostAccept()
*函数功能：投递Accept请求
*函数参数：pIoContext:数据结构体，保存了每次重叠io所需的数据结构
*函数返回：BOOL:false表示失败，true表示成功
*函数说明：提前为新客户端准备套接字，降低在客户端连接后耗费系统资源创建套接字；
*			投递acceptex
**************************************/
bool CIocpModel::_PostAccept(IoContext* pIoContext)
{
	if (b_pListenContext == NULL || b_pListenContext->b_Socket == INVALID_SOCKET)
	{
		throw "_PostAccept, b_pListenContext or b_Socket INVALID!";
	}
	pIoContext->ResetBuffer();
	pIoContext->b_PostType = PostType::ACCEPT;
	//为以后新连接的客户端准备好socket（与传统的socket最大的区别）
	pIoContext->b_acceptSocket = WSASocket(PF_INET, SOCK_STREAM, IPPROTO_TCP,
		NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == pIoContext->b_acceptSocket)
	{
		//投递多少次ACCEPT，就创建多少socket；
		_ShowMessage("创建用于Accept的Socket失败！err=%d", WSAGetLastError());
		return false;
	}
	
	//投递AcceptEx
	//将接受缓冲设置为0，令AcceptEx直接返回
	DWORD dwBytes = 0, dwAddrLen = (sizeof(sockaddr_in) + 16);
	WSABUF* pWSAbuf = &pIoContext->b_wsaBuf;
	if (!b_lpfnAcceptEx(b_pListenContext->b_Socket, pIoContext->b_acceptSocket,
		pWSAbuf->buf, 0, dwAddrLen, dwAddrLen, &dwBytes, &pIoContext->b_Overlapped));
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			_ShowMessage("投递 AcceptEx 失败，err=%d", nErr);
			return false;
		}
	}
	//原子操作，类似于加锁后再+1，防止其他线程在操作过程中修改
	InterlockedIncrement(&acceptPostCount);
	return true;
}

/**************************************
*函数名称：_DoAccept
*函数功能：函数进行客户端接入处理；
*函数参数：SocketContext* pSoContext:	本次accept操作对应的套接字，该套接字所对应的数据结构；
IoContext* pIoContext:			本次accept操作对应的数据结构；
DWORD		dwIOSize:			本次操作数据实际传输的字节数
*函数返回：BOOL:false表示失败，true表示成功
*函数说明：设置了心跳包
**************************************/
bool CIocpModel::_DoAccept(SocketContext* pSoContext, IoContext* pIoContext)
{
	InterlockedIncrement(&connectCount);
	InterlockedDecrement(&acceptPostCount);
	//获得对方地址
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	int remoteLen = 0, localLen = 0;
	this->b_lpfnGetAcceptExSockAddrs(pIoContext->b_wsaBuf.buf,
		0, dwAddrLen, dwAddrLen, (LPSOCKADDR*)&localAddr, &localLen,
		(LPSOCKADDR*)&clientAddr, &remoteLen);

	//为新地址建立一个socketcontext
	SocketContext* pNewSocketContext = new SocketContext;
	//加入到ContextList中去（统一管理，方便释放资源）
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->b_Socket = pIoContext->b_acceptSocket;
	memcpy(&(pNewSocketContext->b_ClientAddr), clientAddr, sizeof(SOCKADDR_IN));

	//将listenSocketContext中的IOContext 重置后继续投递AcceptEx
	if (!_PostAccept(pIoContext))
	{
		pSoContext->RemoveContext(pIoContext);
	}

	//将新socket和完成端口绑定
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{
		//失败的话无需release，在函数中已经release
		return false;
	}

	//设置心跳包
	tcp_keepalive alive_in = { 0 }, alive_out{ 0 };
	//60s 没有数据就开始send心跳包
	alive_in.keepalivetime = 1000 * 60;
	//10s 每隔10s send一个心跳包
	alive_in.keepaliveinterval = 1000 * 10;
	alive_in.onoff = true;
	DWORD lpcbBytesReturned = 0;
	if (SOCKET_ERROR == WSAIoctl(pNewSocketContext->b_Socket, SIO_KEEPALIVE_VALS,
		&alive_in, sizeof(alive_in), &alive_out, sizeof(alive_out), &lpcbBytesReturned,
		NULL, NULL))
	{
		_ShowMessage("WSAIoctl() failed: %d\n", WSAGetLastError());
	}
	OnConnectionAccepted(pNewSocketContext);

	//建立recv所需的ioContext，在新连接的socket投递recv请求
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	if (pNewIoContext != NULL)
	{
		pNewIoContext->b_PostType = PostType::RECV;
		pNewIoContext->b_acceptSocket = pNewSocketContext->b_Socket;
		//投递recv请求
		return _PostRecv(pNewSocketContext, pNewIoContext);
	}
	else
	{
		_DoClose(pNewSocketContext);
		return false;
	}
}

/**************************************
*函数名称：_PostRecv
*函数功能：投递WSARecv请求；
*函数参数：IoContext* pIoContext:	用于进行IO的套接字上的结构，主要为WSARecv参数和WSASend参数；
*函数返回：BOOL:false表示失败，true表示成功
*函数说明：与_DoRecv配套
**************************************/
bool CIocpModel::_PostRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	pIoContext->ResetBuffer();
	pIoContext->b_PostType = PostType::RECV;
	pIoContext->b_nTotalBytes = 0;
	pIoContext->b_nSentBytes = 0;
	//初始化变量
	DWORD dwFlags = 0, dwBytes = 0;
	const int nBytesRecv = WSARecv(pIoContext->b_acceptSocket, &pIoContext->b_wsaBuf,
		1, &dwBytes, &dwFlags, &pIoContext->b_Overlapped, NULL);
	if (SOCKET_ERROR == nBytesRecv)
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			this->_ShowMessage("投递WSARecv失败！err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

/**************************************
*函数名称：_DoRecv
*函数功能：在有接收的数据到达的时候，进行处理
*函数参数：IoContext* pIoContext:	用于进行IO的套接字上的结构
*函数返回：BOOL:false表示失败，true表示成功
*函数说明：回到应用层进行处理
**************************************/
bool CIocpModel::_DoRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	//先把收到的数据显示出现，然后重置状态，发出下一个Recv请求
	//SOCKADDR_IN* clientAddr = &pSoContext->b_ClientAddr;
	this->OnRecvCompleted(pSoContext, pIoContext);
	return true;
}

/**************************************
*函数名称：_PostSend
*函数功能：投递WSASend请求
*函数参数：IoContext* pIoContext:用于进行IO的套接字上的结构
*函数返回：BOOL:false表示失败，true表示成功
*函数说明：调用PostWrite之前需要设置pIoContext中m_wsaBuf, m_nTotalBytes, m_nSendBytes；
**************************************/
bool CIocpModel::_PostSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	pIoContext->b_PostType = PostType::SEND;
	pIoContext->b_nTotalBytes = 0;
	pIoContext->b_nSentBytes = 0;
	const DWORD dwFlags = 0;
	DWORD dwSendNumBytes = 0;
	const int nRet = WSASend(pIoContext->b_acceptSocket, &pIoContext->b_wsaBuf,
		1, &dwSendNumBytes, dwFlags, &pIoContext->b_Overlapped, NULL);
	if (SOCKET_ERROR == nRet)
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			this->_ShowMessage("投递WSASend失败！err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

/**************************************
*函数名称：_DoSend
*函数功能：继续完成发送数据或者通知应用层已发送完毕
*函数参数：IoContext* pIoContext:用于进行IO的套接字上的结构
*函数返回：BOOL:false表示失败，true表示成功
*函数说明：
**************************************/
bool CIocpModel::_DoSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	if (pIoContext->b_nSentBytes < pIoContext->b_nTotalBytes)
	{
		//数据未发送完，继续发送
		pIoContext->b_wsaBuf.buf = pIoContext->b_szBuffer + pIoContext->b_nSentBytes;
		pIoContext->b_wsaBuf.len = pIoContext->b_nTotalBytes - pIoContext->b_nSentBytes;
		return this->_PostSend(pSoContext, pIoContext);
	}
	else
	{
		//通知应用层已发送完毕
		this->OnSendCompleted(pSoContext, pIoContext);
		return true;
	}
}

/**************************************
*函数名称：_DoClose
*函数功能：检查是否能关闭
*函数参数：
*函数返回：BOOL:false表示失败，true表示成功
*函数说明：
**************************************/
bool CIocpModel::_DoClose(SocketContext* pSoContext)
{
	if (pSoContext != b_pListenContext)
	{
		//这个
		InterlockedDecrement(&connectCount);
		this->_RemoveContext(pSoContext);
		return true;
	}
	InterlockedIncrement(&errorCount);
	return false;
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