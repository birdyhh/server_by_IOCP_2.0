#pragma once
#include <winsock2.h>
#include <MSWSock.h>
#include <winnt.h>
#include <vector>
#include <string>

using namespace std;

#define MAX_BUFFER_LEN (1024*8)        //8K����ռ�
//=============================================================================
//                       PostTypeö����
// ----------------------------------------------------------------------------
//��������ɶ˿���Ͷ�ݵ�I/O��������
//=============================================================================
enum class PostType
{
	INIT,                              //��ʼ��ʱʹ��
	ACCEPT,                            //��־Ͷ�ݵ���ACCEPT����
	SEND,                              //��־Ͷ�ݵ���SEND����
	RECV                               //��־Ͷ�ݵ���RECV����
};


//=============================================================================
//			      IoContext���ݽṹ�壨��ÿ���ص�IO�����Ĳ�����
// ----------------------------------------------------------------------------
// ÿ���ص�IO������������ݽṹ��
// OVERLAPPED�ṹ���������׽��֡�����ȥ��IO��������
//=============================================================================
struct IoContext
{
	OVERLAPPED b_Overlapped;           //ʹ���ص�IOÿ�ζ���Ҫ
	PostType   b_PostType;             //��ʶÿ�����������PostType����
	SOCKET     b_acceptSocket;         //��һ�����������ʹ�õ��׽���
	WSABUF     b_wsaBuf;               //WSA���棬��Ϊ�ص������д��ݵĲ���
	char       b_szBuffer[MAX_BUFFER_LEN];//WSABUF�е��ַ�������
	DWORD      b_nTotalBytes;          //���ݵ����ֽ���
	DWORD      b_nSentBytes;           //�Ѿ����͵��ֽ�����δ����Ϊ0

	IoContext()                        //���캯������ʼ���ṹ�ڵ�ֵ
	{
		b_PostType = PostType::INIT;
		ZeroMemory(&b_Overlapped, sizeof(b_Overlapped));
		ZeroMemory(&b_szBuffer, MAX_BUFFER_LEN);
		b_acceptSocket = INVALID_SOCKET;
		b_wsaBuf.buf = b_szBuffer;
		b_wsaBuf.len = MAX_BUFFER_LEN;
		b_nSentBytes = 0;
		b_nTotalBytes = 0;
	}

	~IoContext()
	{
		if (b_acceptSocket == INVALID_SOCKET)
		{
			//����ǰ�ѹر�socket
			b_acceptSocket = INVALID_SOCKET;
		}
	}

	void ResetBuffer()                 //���û�����
	{
		ZeroMemory(b_szBuffer, MAX_BUFFER_LEN);
		b_wsaBuf.len = MAX_BUFFER_LEN;
		b_wsaBuf.buf = b_szBuffer;
	}
};