




// $_FILEHEADER_BEGIN *********************************************************
// �ļ����ƣ�ThreadExit.h
// �������ڣ�2014-12-02
// �����ˣ��޿���
// �ļ�˵�����߳��˳���
// $_FILEHEADER_END ***********************************************************

#ifndef THREAD_EXIT_H
#define THREAD_EXIT_H

#include <mutex>
#include <condition_variable>
#include <thread>

class CThreadExit
{
public:
	CThreadExit();
	~CThreadExit();

public:
	// $_FUNCTION_BEGIN *******************************************************
	// �������ƣ�IsExit
	// ����������aiCheckTime		[����]		���ʱ��(��λ����)
	// �� �� ֵ���Ƿ��˳��߳�
	// ����˵�����Ƿ��˳��߳�
	// $_FUNCTION_END *********************************************************
    bool IsExit(uint32_t aiCheckTime);

	// $_FUNCTION_BEGIN *******************************************************
	// �������ƣ�ExitThread
	// ����������
	// �� �� ֵ��
	// ����˵�����˳��߳�
	// $_FUNCTION_END *********************************************************
	void ExitThread();

	// $_FUNCTION_BEGIN *******************************************************
	// �������ƣ�ResetFlag
	// ����������
	// �� �� ֵ��
	// ����˵�����������ñ��
	// $_FUNCTION_END *********************************************************
	void ResetFlag();

private:
	// �Ƿ��˳��߳�
	bool				m_bThreadExit;

	// ��
	std::mutex				m_Lock;

	// ��������
	std::condition_variable	m_ConditionVariable;
};

#endif
