#ifndef __HTTPCONTROLSOCKET_H__
#define __HTTPCONTROLSOCKET_H__

#include <wx/uri.h>

class CHttpOpData;
class CTlsSocket;
class CHttpFileTransferOpData;
class CHttpControlSocket final : public CRealControlSocket
{
public:
	CHttpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CHttpControlSocket();

protected:
	virtual int ContinueConnect();
	virtual bool Connected() { return m_pCurrentServer != 0; }

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification);
	virtual int SendNextCommand();
	virtual int ParseSubcommandResult(int prevResult);

	virtual int FileTransfer(const wxString localFile, const CServerPath &remotePath,
							 const wxString &remoteFile, bool download,
							 const CFileTransferCommand::t_transferSettings& transferSettings);
	virtual int FileTransferSend();
	virtual int FileTransferParseResponse(char* p, unsigned int len);
	virtual int FileTransferSubcommandResult(int prevResult);

	int InternalConnect(wxString host, unsigned short port, bool tls);
	int DoInternalConnect();

	virtual void OnConnect();
	virtual void OnClose(int error);
	virtual void OnReceive();
	int DoReceive();

	virtual int Disconnect();

	virtual int ResetOperation(int nErrorCode);

	virtual void ResetHttpData(CHttpOpData* pData);

	int OpenFile( CHttpFileTransferOpData* pData);

	int ParseHeader(CHttpOpData* pData);
	int OnChunkedData(CHttpOpData* pData);

	int ProcessData(char* p, int len);

	char* m_pRecvBuffer;
	unsigned int m_recvBufferPos;
	static const unsigned int m_recvBufferLen = 4096;

	CHttpOpData* m_pHttpOpData;

	CTlsSocket* m_pTlsSocket;

	wxURI m_current_uri;
};

#endif //__HTTPCONTROLSOCKET_H__
