#pragma once
#include "PerSocketContext.h"

#define WORKER_THREADS_PER_PROCESS 2             //ÿ�����������ж��ٸ��߳�
#define MAX_LISTEN_SOCKET          SOMAXCONN     //ͬʱ������SOCKET����
#define MAX_POST_ACCEPT            10			 //ͬʱͶ�ݵ�AcceptEx���������
#define EXIT_CODE                  NULL			 //���ݸ�Worker�̵߳��Ƴ��ź�
#define DEFAULT_IP                 "127.0.0.1"   //Ĭ��IP��ַ
#define DEFAULT_PORT               10240         //Ĭ�϶˿ں�

#define RELEASE_ARRAY(x) {if(x != nullptr){delete []x; x = nullptr;}}
#define RELEASE_POINTER(x) {if(x != nullptr){delete x; x = nullptr;}}
#define RELEASE_HANDLE(x) {if(x != nullptr && x != INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x=nullptr;}}				 //�ͷž����
#define RELEASE_SOCKET(x) {if(x != NULL && x != INVALID_SOCKET)\
	{ closesocket(x);x=INVALID_SOCKET;}}          //�ͷ�Socket��

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
	HANDLE*                b_phWorkerThreads;    //�������̵߳ľ��ָ��
	int                    b_nThreads;           //���ɵ��߳�����
	string                 b_strIP;              //�������˵�IP��ַ
	int                    b_nPorts;             //�������˵ļ�����ַ
	CRITICAL_SECTION       b_csContextList;      //����Worker�߳�ͬ���Ļ�����
	vector<SocketContext*> b_arrayClientContext;//�ͻ���Socket��Context��Ϣ
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
	bool Start(int port = DEFAULT_PORT);
	//ֹͣ������
	void Stop();
	//��ñ�����IP��ַ
	string GetLocalIP();
	// ��ָ���ͻ��˷�������
	bool SendData(SocketContext* pSoContext, char* data, int size);
	bool SendData(SocketContext* pSoContext, IoContext* pIoContext);
	// ��������ָ���ͻ��˵�����
	bool RecvData(SocketContext* pSoContext, IoContext* pIoContext);

	// ��ȡ��ǰ������
	int GetConnectCount() { return connectCount; }
	// ��ȡ��ǰ�����˿�
	unsigned int GetPort() { return b_nPorts; }

	// �¼�֪ͨ����(���������ش��庯��)
	virtual void OnConnectionAccepted(SocketContext* pSoContext) {};
	virtual void OnConnectionClosed(SocketContext* pSoContext) {};
	virtual void OnConnectionError(SocketContext* pSoContext, int error) {};
	virtual void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		SendData(pSoContext, pIoContext); // ����������ɣ�ԭ�ⲻ������ȥ
	};
	virtual void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		RecvData(pSoContext, pIoContext); // ����������ɣ�������������
	};

protected:
	//��ʼ��IOCP
	bool _InitializeIOCP();
	//��ʼ��Socket
	bool _InitializeListenSocket();
	//����ͷ���Դ
	void _DeInitialize();
	//Ͷ��AccpetEx����
	bool _PostAccept(IoContext* pIoContext);
	//�пͻ�������ʱ�����д���
	bool _DoAccept(SocketContext* pSoContext, IoContext* pIoContext);
	//Ͷ��WSARecv���ڽ�������
	bool _PostRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//���ݵ����������pIoContext������
	bool _DoRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//Ͷ��WSASend�����ڷ�������
	bool _PostSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoClose(SocketContext* pSoContext);
	//���ͻ���socket�������Ϣ�洢��������
	void _AddToContextList(SocketContext* pSoContext);
	//���ͻ���socket����Ϣ���������Ƴ�
	void _RemoveContext(SocketContext* pSoContext);
	// ��տͻ���socket��Ϣ
	void _ClearContextList();
	// ������󶨵���ɶ˿���
	bool _AssociateWithIOCP(SocketContext* pSoContext);
	// ������ɶ˿��ϵĴ���
	bool HandleError(SocketContext* pSoContext, const DWORD& dwErr);
	//��ñ����Ĵ���������
	int _GetNumOfProcessors() noexcept;
	//�жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
	bool _IsSocketAlive(SOCKET s) noexcept;

	//�̺߳�����ΪIOCP�������Ĺ������߳�
	//������Ϊ��̬ȫ�ֺ����������޷�������
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);

	//����ҳ�������Ϣ
	virtual void _ShowMessage(const char* szFormat, ...);

public:
	void SetLogFunc(LOG_FUNC fn) { b_LogFunc = fn; }
protected:
	LOG_FUNC b_LogFunc;
};

