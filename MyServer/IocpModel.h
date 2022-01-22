#pragma once
#include "PerSocketContext.h"

#define WORKER_THREADS_PER_PROCESS 2             //ÿ�����������ж��ٸ��߳�
#define MAX_LISTEN_SOCKET          SOMACONN      //ͬʱ������SOCKET����
#define MAX_POST_ACCEPT            10			 //ͬʱͶ�ݵ�AcceptEx���������
#define EXIT_CODE                  NULL			 //���ݸ�Worker�̵߳��Ƴ��ź�
#define DEFAULT_IP                 "127.0.0.1"   //Ĭ��IP��ַ
#define DEFAULT_PORT               10240         //Ĭ�϶˿ں�

#define RELEASE_ARRAY(x) {if(x != nullptr){delete []x; x = nullptr;}}
#define RELEASE_POINTER(x) {if(x != nullptr){delete x; x = nullptr;}}
#define RELEASE_HANDLE(x) {if(x != nullptr && x != INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x=nullptr}}				 //�ͷž����
#define RELEASE_SOCKET(x) {if(x != NULL && x != INVALID_SOCKET)\
	{ closesocket(x);x=INVALID_SOCKET}}          //�ͷ�Socket��

//=============================================================================
//						CIocpModel�ඨ��
//=============================================================================
typedef void (*LOG_FUNC)(const string& strInfo);

class CIocpModel;
//�������̵߳��̲߳���
struct WorkerThreadParam
{
	CIocpModel* pIocpModel;                      //��ָ��
	int nThreadNo;                               //�̱߳��
	int nThreadId;								 //�߳�ID
};

class CIocpModel
{
private:
	HANDLE                 b_hShutdownEvent;     //����֪ͨ�̣߳�Ϊ�˸��õ��˳�
	HANDLE                 b_hIOCompletionPort;  //��ɶ˿ڵľ��
	HANDLE                 b_phWorkerThreads;    //�������̵߳ľ��ָ��
	int                    b_nThreads;           //���ɵ��߳�����
	string                 b_strIP;              //�������˵�IP��ַ
	int                    b_nPorts;             //�������˵ļ�����ַ
	CRITICAL_SECTION       b_csContextList;      //����Worker�߳�ͬ���Ļ�����
	vector<SocketContext*> b_arratyClientContext;//�ͻ���Socket��Context��Ϣ
	SocketContext*         b_pListenContext;     //���ڼ���Socket��Context��Ϣ
	LONG                   acceptPostCount;      //��ǰͶ�ݵ�Accept����
	LONG                   connectCount;		 //��ǰ����������
	LONG                   errorCount;			 //��ǰ�Ĵ�������

	//GetAcceptExSockAddrs����ָ��
	LPFN_GETACCEPTEXSOCKADDRS b_lpfnGetAcceptExSockAddrs;
	//Accept����ָ��
	LPFN_ACCEPTEX b_lpfnAcceptEx;

public:
	CIocpModel();
	~CIocpModel();

	//����Socket��
	bool LoadSocketLib();
	//ж��Socket��
	void UnloadSocketLib() noexcept
	{
		WSACleanup();
	}
	//����������
	bool Start(int port);
	//ֹͣ������
	void Stop();
	//��ñ�����IP��ַ
	string GetLocalIP();

protected:
	//��ʼ��IOCP
	bool _InitializeIOCP();
	//��ʼ��Socket
	bool _InitializeListenSocket();
	//�̺߳�����ΪIOCP�������Ĺ������߳�
	DWORD WINAPI _WorkerThread(LPVOID lpParam);

	//����ҳ�������Ϣ
	virtual void _ShowMessage(const char* szFormat, ...);
};

