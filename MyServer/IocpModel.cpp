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
//   �������̣߳�ΪIOCP�������Ĺ������߳�
//ÿ������ɶ˿��ϳ�������ɵ����ݰ����ͽ���ȡ�����д���
///////////////////////////////////////////////////////////////////////////////
/******************************************************************************
*�������ܣ��̺߳���������GetQueuedCompletionStatus���ص�PostType������д���
*����������lpParam��THREADPARAMS_WORKER����ָ�룻
*����˵����GetQueuedCompletionStatus��ȷ����ʱ˵����������ɣ�
*		   �ڶ�������lpNumberOfBytes��ʾ��һ���׽��ִ�����ֽ���
******************************************************************************/
DWORD WINAPI CIocpModel::_WorkerThread(LPVOID lpParam)
{
	WorkerThreadParam* pParam = (WorkerThreadParam*)lpParam;
	CIocpModel* pIocpModel = (CIocpModel*)pParam->pIocpModel;
	const int nThreadNo = pParam->nThreadNo;
	const int nThreadId = pParam->nThreadId;

	pIocpModel->_ShowMessage("�������̣߳�No:%d, ID:%d", nThreadNo, nThreadId);
	//ѭ����������֪�����յ�shutdown��ϢΪֹ
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
					pIocpModel->_ShowMessage("_WorkThread�е�m_OpType �����쳣");
					break;
				}
			}
		}
	}
	pIocpModel->_ShowMessage("�������߳� %d ���˳�", nThreadNo);
	RELEASE_POINTER(lpParam);
	return 0;
}

//=============================================================================
//					          ϵͳ�ĳ�ʼ������ֹ
//=============================================================================

/**************************************
*�������ƣ�LoadSocketLib()
*�������ܣ���ʼ��WINSOCK
*������������
*�������أ�BOOL:false��ʾ��ʼ��ʧ�ܣ�true��ʾ��ʼ���ɹ�
*����˵��������2.2�汾��ʼ��WINSOCK��һ�㲻����ʧ��
**************************************/
bool CIocpModel::LoadSocketLib()
{
	WSADATA wsaData = { 0 };
	const int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (NO_ERROR != nRet)
	{
		this->_ShowMessage("��ʼ��Winsock 2.2ʧ��");
		return false;
	}
	return true;
}

/**************************************
*�������ƣ�Start()
*�������ܣ�����������
*����������int port���������˿ںţ�Ĭ��ΪDEFAULT_PORT��10240����
*�������أ�BOOL:false��ʾ����ʧ�ܣ�true��ʾ�����ɹ�
*����˵���������������
*			1.��ʼ���̻߳�����
*			2.����ϵͳ�˳���ʱ��֪ͨ
*			3.����_InitializeIOCP()��ʼ��IOCP
*			4.����_InitializeListenSocket()��ʼ��socket
**************************************/
bool CIocpModel::Start(int port)
{
	b_nPorts = port;
	//��ʼ���̻߳�����
	InitializeCriticalSection(&b_csContextList);
	//����ϵͳ�˳���ʱ��֪ͨ
	b_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	//��ʼ��IOCP
	if (!_InitializeIOCP())
	{
		this->_ShowMessage("��ʼ��IOCPʧ�ܣ�");
		return false;
	}
	else
	{
		this->_ShowMessage("��ʼ��IOCP��ϣ�");
	}
	//��ʼ��socket
	if (!_InitializeListenSocket())
	{
		this->_ShowMessage("����Socket��ʼ��ʧ�ܣ�");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("����Socket��ʼ����ϣ�");
	}
	this->_ShowMessage("ϵͳ׼���������Ⱥ�����....");
	return true;
}

/**************************************
*�������ƣ�Stop()
*�������ܣ�ֹͣ������
*������������
*�������أ���
*����˵������ʼ����ϵͳ�˳���Ϣ���˳���ɶ˿ں��߳���Դ
**************************************/
void CIocpModel::Stop()
{
	if (b_pListenContext != nullptr
		&& b_pListenContext->b_Socket != INVALID_SOCKET)
	{
		//����ر���Ϣ֪ͨ
		SetEvent(b_hShutdownEvent);
		for (int i = 0; i < b_nThreads; i++)
		{
			//֪ͨ���е���ɶ˿ڲ����˳�
			PostQueuedCompletionStatus(b_hIOCompletionPort, 0, (DWORD)EXIT_CODE, NULL);
		}
		//�ȴ����пͻ�����Դ�˳�
		WaitForMultipleObjects(b_nThreads, b_phWorkerThreads, TRUE, INFINITE);
		//����ͻ����б���Ϣ
		this->_ClearContextList();
		//�ͷ�������Դ
		this->_DeInitialize();
		this->_ShowMessage("ֹͣ����");
	}
	else
	{
		b_pListenContext = nullptr;
	}
}

/**************************************
*�������ƣ�_InitializeIOCP()
*�������ܣ���ʼ��IOCP
*������������
*�������أ�BOOL:false��ʾ����ʧ�ܣ�true��ʾ�����ɹ�
*����˵���������������
*			1.��ʼ����ɶ˿�
*			2.������Ӧ���߳���
*			3.Ϊ�������̳߳�ʼ�����
**************************************/
bool CIocpModel::_InitializeIOCP()
{
	this->_ShowMessage("��ʼ��IOCP-InitializeIOCP()");
	b_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (b_hIOCompletionPort == nullptr)
	{
		this->_ShowMessage("������ɶ˿�ʧ�ܣ�������룺 %d!", WSAGetLastError());
		return false;
	}
	//���ݱ����еĴ�����������������Ӧ���߳���
	b_nThreads = WORKER_THREADS_PER_PROCESS * _GetNumOfProcessors();
	//Ϊ�������̳߳�ʼ�����
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
	this->_ShowMessage("����WorkerThread %d ��",b_nThreads);
	return true;
}

/**************************************
*�������ƣ�_InitializeListenSocket()
*�������ܣ���ʼ��Socket
*������������
*�������أ�BOOL:false��ʾ��ʼ��ʧ�ܣ�true��ʾ��ʼ���ɹ�
*����˵���������������
*			1.����socket�󣬰󶨵���ɶ˿��У��󶨵�ip��ַ�Ͷ˿��ϣ�Ȼ��ʼ����
*			2.��ȡ������AcceptEx������GetAcceptExSockAddrs������ָ��
*			3.����10���׽��֣�Ͷ��AcceptEx����
**************************************/
bool CIocpModel::_InitializeListenSocket()
{
	this->_ShowMessage("��ʼ��Socket-InitializeListenSocket()");
	b_pListenContext = new SocketContext;
	//�����ص�IO��socket
	b_pListenContext->b_Socket = WSASocket(PF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == b_pListenContext->b_Socket)
	{
		this->_ShowMessage("WSASocket() ʧ�ܣ�err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("����WSASocket() ���");
	}
	//��listen Socket�󶨵���ɶ˿�
	if (NULL == CreateIoCompletionPort((HANDLE)b_pListenContext->b_Socket,
		b_hIOCompletionPort, (DWORD)b_pListenContext, 0));
	{
		this->_ShowMessage("��ʧ�ܣ� err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	//����ַ��Ϣ
	//��������ַ��Ϣ
	sockaddr_in serverAddress;
	ZeroMemory(&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(b_nPorts);
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

	//�󶨶˿ں͵�ַ
	if (SOCKET_ERROR == bind(b_pListenContext->b_Socket,
		(sockaddr*)&serverAddress, sizeof(serverAddress)))
	{
		this->_ShowMessage("bind()����ִ��ʧ��");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("bind() ���");
	}

	//�����˿ں͵�ַ
	if (SOCKET_ERROR == listen(b_pListenContext->b_Socket,MAX_LISTEN_SOCKET))
	{
		this->_ShowMessage("listen()����ִ��ʧ��");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("listen() ���");
	}

	//��ǰ��ȡAcceptEx����ָ�룬�����ں�����ε���AcceptEx����������ʹ��WSAIoctl����
	//������ʹ��WSAIoctl���������Ӱ�����ܵĲ���
	DWORD dwBytes = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(b_pListenContext->b_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &b_lpfnAcceptEx,
		sizeof(b_lpfnAcceptEx), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoct1 δ�ܻ�ȡAcceptEx����ָ�롣�������: %d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	//ͬ����ȡGetAcceptExSockAddrs()����ָ��
	if (SOCKET_ERROR == WSAIoctl(b_pListenContext->b_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs), &b_lpfnGetAcceptExSockAddrs,
		sizeof(b_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoct1 δ�ܻ�ȡGetAcceptExSockAddrs����ָ�롣�������: %d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	//ΪAcceptEx ׼��������Ȼ��Ͷ��AcceptEx I/O����
	//����10���׽��֣�Ͷ��AcceptEx���󣬼�����10���׽��ֽ���accept������
	//���޸ĺ�MAX_POST_ACCEPT���ı�ͬʱͶ�ݵ�AcceptEx�����������Ĭ��Ϊ10
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		//�½�һ��IO_CONTEXT
		IoContext* pIoContext = b_pListenContext->GetNewIoContext();
		if (pIoContext && !this->_PostAccept(pIoContext))
		{
			b_pListenContext->RemoveContext(pIoContext);
			return false;
		}
	}
	this->_ShowMessage("Ͷ�� %d ��AcceptEx�������", MAX_POST_ACCEPT);
	return true;
}

/**************************************
*�������ƣ�_DeInitialize()
*�������ܣ��ͷŵ�������Դ
*������������
*�������أ���
*����˵�����ر����й������̣߳��ر����о�����ͷ�����ָ��
**************************************/
void CIocpModel::_DeInitialize()
{
	//ɾ���ͻ��˻�����
	DeleteCriticalSection(&b_csContextList);
	//�ر�ϵͳ�˳�ʱ����
	RELEASE_HANDLE(b_hShutdownEvent);
	//�ͷŹ������߳̾��ָ��
	for (int i = 0; i < b_nThreads; i++)
	{
		RELEASE_HANDLE(b_phWorkerThreads[i]);
	}
	RELEASE_ARRAY(b_phWorkerThreads);
	//�ر�IOCP���
	RELEASE_HANDLE(b_hIOCompletionPort);
	//�رռ���socket
	RELEASE_POINTER(b_pListenContext);
	this->_ShowMessage("�ͷ���Դ���");
}



//=============================================================================
//				 Ͷ����ɶ˿�����
//=============================================================================

/**************************************
*�������ƣ�_PostAccept()
*�������ܣ�Ͷ��Accept����
*����������pIoContext:���ݽṹ�壬������ÿ���ص�io��������ݽṹ
*�������أ�BOOL:false��ʾʧ�ܣ�true��ʾ�ɹ�
*����˵������ǰΪ�¿ͻ���׼���׽��֣������ڿͻ������Ӻ�ķ�ϵͳ��Դ�����׽��֣�
*			Ͷ��acceptex
**************************************/
bool CIocpModel::_PostAccept(IoContext* pIoContext)
{
	if (b_pListenContext == NULL || b_pListenContext->b_Socket == INVALID_SOCKET)
	{
		throw "_PostAccept, b_pListenContext or b_Socket INVALID!";
	}
	pIoContext->ResetBuffer();
	pIoContext->b_PostType = PostType::ACCEPT;
	//Ϊ�Ժ������ӵĿͻ���׼����socket���봫ͳ��socket��������
	pIoContext->b_acceptSocket = WSASocket(PF_INET, SOCK_STREAM, IPPROTO_TCP,
		NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == pIoContext->b_acceptSocket)
	{
		//Ͷ�ݶ��ٴ�ACCEPT���ʹ�������socket��
		_ShowMessage("��������Accept��Socketʧ�ܣ�err=%d", WSAGetLastError());
		return false;
	}
	
	//Ͷ��AcceptEx
	//�����ܻ�������Ϊ0����AcceptExֱ�ӷ���
	DWORD dwBytes = 0, dwAddrLen = (sizeof(sockaddr_in) + 16);
	WSABUF* pWSAbuf = &pIoContext->b_wsaBuf;
	if (!b_lpfnAcceptEx(b_pListenContext->b_Socket, pIoContext->b_acceptSocket,
		pWSAbuf->buf, 0, dwAddrLen, dwAddrLen, &dwBytes, &pIoContext->b_Overlapped));
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			_ShowMessage("Ͷ�� AcceptEx ʧ�ܣ�err=%d", nErr);
			return false;
		}
	}
	//ԭ�Ӳ����������ڼ�������+1����ֹ�����߳��ڲ����������޸�
	InterlockedIncrement(&acceptPostCount);
	return true;
}

/**************************************
*�������ƣ�_DoAccept
*�������ܣ��������пͻ��˽��봦��
*����������SocketContext* pSoContext:	����accept������Ӧ���׽��֣����׽�������Ӧ�����ݽṹ��
IoContext* pIoContext:			����accept������Ӧ�����ݽṹ��
DWORD		dwIOSize:			���β�������ʵ�ʴ�����ֽ���
*�������أ�BOOL:false��ʾʧ�ܣ�true��ʾ�ɹ�
*����˵����������������
**************************************/
bool CIocpModel::_DoAccept(SocketContext* pSoContext, IoContext* pIoContext)
{
	InterlockedIncrement(&connectCount);
	InterlockedDecrement(&acceptPostCount);
	//��öԷ���ַ
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	int remoteLen = 0, localLen = 0;
	this->b_lpfnGetAcceptExSockAddrs(pIoContext->b_wsaBuf.buf,
		0, dwAddrLen, dwAddrLen, (LPSOCKADDR*)&localAddr, &localLen,
		(LPSOCKADDR*)&clientAddr, &remoteLen);

	//Ϊ�µ�ַ����һ��socketcontext
	SocketContext* pNewSocketContext = new SocketContext;
	//���뵽ContextList��ȥ��ͳһ���������ͷ���Դ��
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->b_Socket = pIoContext->b_acceptSocket;
	memcpy(&(pNewSocketContext->b_ClientAddr), clientAddr, sizeof(SOCKADDR_IN));

	//��listenSocketContext�е�IOContext ���ú����Ͷ��AcceptEx
	if (!_PostAccept(pIoContext))
	{
		pSoContext->RemoveContext(pIoContext);
	}

	//����socket����ɶ˿ڰ�
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{
		//ʧ�ܵĻ�����release���ں������Ѿ�release
		return false;
	}

	//����������
	tcp_keepalive alive_in = { 0 }, alive_out{ 0 };
	//60s û�����ݾͿ�ʼsend������
	alive_in.keepalivetime = 1000 * 60;
	//10s ÿ��10s sendһ��������
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

	//����recv�����ioContext���������ӵ�socketͶ��recv����
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	if (pNewIoContext != NULL)
	{
		pNewIoContext->b_PostType = PostType::RECV;
		pNewIoContext->b_acceptSocket = pNewSocketContext->b_Socket;
		//Ͷ��recv����
		return _PostRecv(pNewSocketContext, pNewIoContext);
	}
	else
	{
		_DoClose(pNewSocketContext);
		return false;
	}
}

/**************************************
*�������ƣ�_PostRecv
*�������ܣ�Ͷ��WSARecv����
*����������IoContext* pIoContext:	���ڽ���IO���׽����ϵĽṹ����ҪΪWSARecv������WSASend������
*�������أ�BOOL:false��ʾʧ�ܣ�true��ʾ�ɹ�
*����˵������_DoRecv����
**************************************/
bool CIocpModel::_PostRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	pIoContext->ResetBuffer();
	pIoContext->b_PostType = PostType::RECV;
	pIoContext->b_nTotalBytes = 0;
	pIoContext->b_nSentBytes = 0;
	//��ʼ������
	DWORD dwFlags = 0, dwBytes = 0;
	const int nBytesRecv = WSARecv(pIoContext->b_acceptSocket, &pIoContext->b_wsaBuf,
		1, &dwBytes, &dwFlags, &pIoContext->b_Overlapped, NULL);
	if (SOCKET_ERROR == nBytesRecv)
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			this->_ShowMessage("Ͷ��WSARecvʧ�ܣ�err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

/**************************************
*�������ƣ�_DoRecv
*�������ܣ����н��յ����ݵ����ʱ�򣬽��д���
*����������IoContext* pIoContext:	���ڽ���IO���׽����ϵĽṹ
*�������أ�BOOL:false��ʾʧ�ܣ�true��ʾ�ɹ�
*����˵�����ص�Ӧ�ò���д���
**************************************/
bool CIocpModel::_DoRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	//�Ȱ��յ���������ʾ���֣�Ȼ������״̬��������һ��Recv����
	//SOCKADDR_IN* clientAddr = &pSoContext->b_ClientAddr;
	this->OnRecvCompleted(pSoContext, pIoContext);
	return true;
}

/**************************************
*�������ƣ�_PostSend
*�������ܣ�Ͷ��WSASend����
*����������IoContext* pIoContext:���ڽ���IO���׽����ϵĽṹ
*�������أ�BOOL:false��ʾʧ�ܣ�true��ʾ�ɹ�
*����˵��������PostWrite֮ǰ��Ҫ����pIoContext��m_wsaBuf, m_nTotalBytes, m_nSendBytes��
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
			this->_ShowMessage("Ͷ��WSASendʧ�ܣ�err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

/**************************************
*�������ƣ�_DoSend
*�������ܣ�������ɷ������ݻ���֪ͨӦ�ò��ѷ������
*����������IoContext* pIoContext:���ڽ���IO���׽����ϵĽṹ
*�������أ�BOOL:false��ʾʧ�ܣ�true��ʾ�ɹ�
*����˵����
**************************************/
bool CIocpModel::_DoSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	if (pIoContext->b_nSentBytes < pIoContext->b_nTotalBytes)
	{
		//����δ�����꣬��������
		pIoContext->b_wsaBuf.buf = pIoContext->b_szBuffer + pIoContext->b_nSentBytes;
		pIoContext->b_wsaBuf.len = pIoContext->b_nTotalBytes - pIoContext->b_nSentBytes;
		return this->_PostSend(pSoContext, pIoContext);
	}
	else
	{
		//֪ͨӦ�ò��ѷ������
		this->OnSendCompleted(pSoContext, pIoContext);
		return true;
	}
}

/**************************************
*�������ƣ�_DoClose
*�������ܣ�����Ƿ��ܹر�
*����������
*�������أ�BOOL:false��ʾʧ�ܣ�true��ʾ�ɹ�
*����˵����
**************************************/
bool CIocpModel::_DoClose(SocketContext* pSoContext)
{
	if (pSoContext != b_pListenContext)
	{
		//���
		InterlockedDecrement(&connectCount);
		this->_RemoveContext(pSoContext);
		return true;
	}
	InterlockedIncrement(&errorCount);
	return false;
}
//����ҳ�������Ϣ
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