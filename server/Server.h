// Server.h: Schnittstelle für die Klasse CServer.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SERVER_H__4896D8C6_EDB5_438E_98E6_08957DBCD1BC__INCLUDED_)
#define AFX_SERVER_H__4896D8C6_EDB5_438E_98E6_08957DBCD1BC__INCLUDED_

class CListenSocket;
class CServerThread;
class COptions;
class CAdminListenSocket;
class CAdminInterface;
class CAdminSocket;
class CFileLogger;
class CAutoBanManager;

class CServer final
{
public:
	void ShowStatus(LPCTSTR msg, int nType);
	void ShowStatus(DWORD eventDateHigh, DWORD eventDateLow, LPCTSTR msg, int nType);
	BOOL ProcessCommand(CAdminSocket *pAdminSocket, int nID, unsigned char *pData, int nDataLength);
	void OnClose();
	bool Create();
	CServer();
	~CServer();
	HWND GetHwnd();
	COptions *m_pOptions;
	CAutoBanManager* m_pAutoBanManager;
protected:
	bool CreateListenSocket();
	bool CreateListenSocket(CStdString ports, bool ssl);
	BOOL CreateAdminListenSocket();
	int DoCreateAdminListenSocket(UINT port, LPCTSTR addr, int family);
	void OnTimer(UINT nIDEvent);
	BOOL ToggleActive(int nServerState);
	unsigned int GetNextThreadNotificationID();
	void FreeThreadNotificationID(CServerThread *pThread);

	// Send state to interface
	void SendState();

	BOOL m_bQuit;
	int m_nServerState;
	CAdminInterface *m_pAdminInterface;
	CFileLogger *m_pFileLogger{};

	std::list<CServerThread*> m_ThreadArray;
	std::list<CServerThread*> m_ClosedThreads;

	std::vector<CServerThread*> m_ThreadNotificationIDs;
	std::list<std::unique_ptr<CAdminListenSocket>> m_AdminListenSocketList;
	std::list<CListenSocket*> m_ListenSocketList;

	std::map<int, t_connectiondata> m_UsersList;

	UINT m_nTimerID;

	_int64 m_nRecvCount;
	_int64 m_nSendCount;

	UINT m_nBanTimerID;

	LRESULT OnServerMessage(CServerThread *pThread, WPARAM wParam, LPARAM lParam);
private:
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	HWND m_hWnd;
};

#endif // !defined(AFX_SERVER_H__4896D8C6_EDB5_438E_98E6_08957DBCD1BC__INCLUDED_)
