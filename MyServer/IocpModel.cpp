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

}

//=============================================================================
//					          ϵͳ�ĳ�ʼ������ֹ
//=============================================================================
// ����1��LoadSocketLib()��ʼ��WINSOCK
// ����2��Start()����������
// ����3��Stop()ֹͣ������
// 
// 

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
*����˵����
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
		b_phWorkerThreads[i] = CreateThread(0, 0, _WorkerThread,
			(void*)pThreadParams, 0, &nThreadID);
		pThreadParams->nThreadId = nThreadID;
	}
	this->_ShowMessage("����WorkerThread %d ��",b_nThreads);
	return true;
}

/**************************************
*�������ƣ�_InitializeListenSocket()
*�������ܣ�
*������������
*�������أ�BOOL:false��ʾ����ʧ�ܣ�true��ʾ�����ɹ�
*����˵����
**************************************/
bool CIocpModel::_InitializeListenSocket()
{
	this->_ShowMessage("��ʼ��Socket-InitializeListenSocket()");
	b_pListenContext = new SocketContext;
	b_pListenContext->b_Socket = WSASocket(PF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if()
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