




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
#include <set>
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
	    // ... 现有构造函数和方法

    // $_FUNCTION_BEGIN *******************************************************
    // 函数名称：RegisterThread
    // 功能说明：注册当前线程，便于统一管理
    // $_FUNCTION_END *********************************************************
    void RegisterThread(std::thread::id thread_id = std::this_thread::get_id())
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        m_registered_threads.insert(thread_id);
    }

    // $_FUNCTION_BEGIN *******************************************************
    // 函数名称：UnregisterThread
    // 功能说明：注销线程
    // $_FUNCTION_END *********************************************************
    void UnregisterThread(std::thread::id thread_id = std::this_thread::get_id())
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        m_registered_threads.erase(thread_id);
    }

    // $_FUNCTION_BEGIN *******************************************************
    // 函数名称：NotifyAllThreads
    // 功能说明：通知所有已注册线程退出
    // $_FUNCTION_END *********************************************************
    void NotifyAllThreads()
    {
        ExitThread();
        m_ConditionVariable.notify_all();
    }

private:
	// �Ƿ��˳��߳�
	bool				m_bThreadExit;

	// ��
	std::mutex				m_Lock;

	// ��������
	std::condition_variable	m_ConditionVariable;
	std::set<std::thread::id> m_registered_threads;  // 已注册的线程
};

#endif
