#include "foundation/thread/ThreadExit.h"

CThreadExit::CThreadExit()
{
	m_bThreadExit = false;
}

CThreadExit::~CThreadExit()
{
	m_bThreadExit = false;
}


bool CThreadExit::IsExit( uint32_t aiCheckTime)
{
	bool bReturn = m_bThreadExit;

	std::unique_lock <std::mutex> AutoLock(m_Lock);
	if (!m_bThreadExit)
	{
		if (m_ConditionVariable.wait_for(AutoLock, std::chrono::milliseconds(aiCheckTime)) == std::cv_status::timeout)	// �����ʱ
		{
			bReturn = m_bThreadExit;
		}
		else
		{
			bReturn = m_bThreadExit;
		}
	}

	return bReturn;
}

// �˳��߳�
void CThreadExit::ExitThread()
{
	std::unique_lock<std::mutex> AutoLock(m_Lock);
	m_bThreadExit = true;
	m_ConditionVariable.notify_all();
}

// �������ñ��
void CThreadExit::ResetFlag()
{
	m_bThreadExit = false;
}
