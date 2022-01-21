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


bool CIocpModel::Start(int port)
{
	b_nPorts = port;
	//��ʼ���̻߳�����
	InitializeCriticalSection(&b_csContextList);
	//����ϵͳ�˳���ʱ��֪ͨ
	b_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	//��ʼ��IOCP

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