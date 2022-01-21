#pragma once
#include "PerIoContext.h"

//=============================================================================
//							socket�ṹ��
// ----------------------------------------------------------------------------
// ���ڰ�ÿһ����ɶ˿ڣ�
// ������ÿ���ͻ���socket�йصĲ�����
//=============================================================================
struct SocketContext
{
	SOCKET          b_Socket;		    //ĳ������������ӵĿͻ���SOCKET
	SOCKADDR_IN		b_ClientAddr;       //�ÿͻ��˵�ַ
	vector<IoContext*> b_arrayIoContext;//�ÿͻ�������IO�����vector����

	//���캯��
	SocketContext()                    
	{
		b_Socket = INVALID_SOCKET;
		memset(&b_ClientAddr, 0, sizeof(b_ClientAddr));
	}
	
	//��������
	~SocketContext()                   
	{
		if (b_Socket != INVALID_SOCKET)
		{
			closesocket(b_Socket);
			b_Socket = INVALID_SOCKET;
		}
		for (size_t i = 0; i < b_arrayIoContext.size(); i++)
		{
			delete b_arrayIoContext.at(i);
		}
		b_arrayIoContext.clear();
	}

	//�����׽��ֲ���ʱ�����ô˺�������IOContext�ṹ
	IoContext* GetNewIoContext()       
	{
		IoContext* p = new IoContext;
		b_arrayIoContext.emplace_back(p);
		return p;
	}

	//ɾ��ָ����IoContext
	void RemoveContext(IoContext* pContext)
	{
		if (pContext == nullptr)
		{
			return;
		}
		vector<IoContext*>::iterator it;
		it = b_arrayIoContext.begin();
		while (it != b_arrayIoContext.end())
		{
			IoContext* pOjb = *it;
			if (pContext == pOjb)
			{
				delete pContext;
				pContext = nullptr;
				it = b_arrayIoContext.erase(it);
				break;
			}
			it++;
		}
	}
};
