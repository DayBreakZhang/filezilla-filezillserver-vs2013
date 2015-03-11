#include <filezilla.h>
#include "dndobjects.h"

#if FZ3_USESHELLEXT

#include <initguid.h>
#include "../fzshellext/shellext.h"
#include <shlobj.h>
#include <wx/stdpaths.h>

CShellExtensionInterface::CShellExtensionInterface()
{
	m_shellExtension = 0;

	CoCreateInstance(CLSID_ShellExtension, NULL,
	  CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IUnknown,
	  &m_shellExtension);

	if (m_shellExtension)
		m_hMutex = CreateMutex(0, false, _T("FileZilla3DragDropExtLogMutex"));
	else
		m_hMutex = 0;

	m_hMapping = 0;
}

CShellExtensionInterface::~CShellExtensionInterface()
{
	if (m_shellExtension)
	{
		((IUnknown*)m_shellExtension)->Release();
		CoFreeUnusedLibraries();
	}

	if (m_hMapping)
		CloseHandle(m_hMapping);

	if (m_hMutex)
		CloseHandle(m_hMutex);

	if (!m_dragDirectory.empty())
		RemoveDirectory(m_dragDirectory);
}

wxString CShellExtensionInterface::InitDrag()
{
	if (!m_shellExtension)
		return wxString();

	if (!m_hMutex)
		return wxString();

	if (!CreateDragDirectory())
		return wxString();

	m_hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, DRAG_EXT_MAPPING_LENGTH, DRAG_EXT_MAPPING);
	if (!m_hMapping)
		return wxString();

	char* data = (char*)MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DRAG_EXT_MAPPING_LENGTH);
	if (!data)
	{
		CloseHandle(m_hMapping);
		m_hMapping = 0;
		return wxString();
	}

	DWORD result = WaitForSingleObject(m_hMutex, 250);
	if (result != WAIT_OBJECT_0)
	{
		UnmapViewOfFile(data);
		return wxString();
	}

	*data = DRAG_EXT_VERSION;
	data[1] = 1;
	wcscpy((wchar_t*)(data + 2), m_dragDirectory.wc_str(wxConvLocal));

	ReleaseMutex(m_hMutex);

	UnmapViewOfFile(data);

	return m_dragDirectory;
}

wxString CShellExtensionInterface::GetTarget()
{
	if (!m_shellExtension)
		return wxString();

	if (!m_hMutex)
		return wxString();

	if (!m_hMapping)
		return wxString();

	char* data = (char*)MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DRAG_EXT_MAPPING_LENGTH);
	if (!data)
	{
		CloseHandle(m_hMapping);
		m_hMapping = 0;
		return wxString();
	}

	DWORD result = WaitForSingleObject(m_hMutex, 250);
	if (result != WAIT_OBJECT_0)
	{
		UnmapViewOfFile(data);
		return wxString();
	}

	wxString target;
	const char reply = data[1];
	if (reply == 2)
	{
		data[DRAG_EXT_MAPPING_LENGTH - 1] = 0;
		target = (wchar_t*)(data + 2);
	}

	ReleaseMutex(m_hMutex);

	UnmapViewOfFile(data);

	if (target.empty())
		return target;

	if (target.Last() == '\\')
		target.RemoveLast();
	int pos = target.Find('\\', true);
	if (pos < 1)
		return wxString();
	target = target.Left(pos + 1);

	return target;
}

bool CShellExtensionInterface::CreateDragDirectory()
{
	for (int i = 0; i < 10; i++)
	{
		wxDateTime now = wxDateTime::UNow();
		wxLongLong value = now.GetTicks();
		value *= 1000;
		value += now.GetMillisecond();
		value *= 10;
		value += i;

		wxFileName dirname(wxStandardPaths::Get().GetTempDir(), DRAG_EXT_DUMMY_DIR_PREFIX + value.ToString());
		dirname.Normalize();
		wxString dir = dirname.GetFullPath();

		if (dir.Len() > MAX_PATH)
			return false;

		if (CreateDirectory(dir, 0))
		{
			m_dragDirectory = dir;
			return true;
		}
	}

	return true;
}

CShellExtensionInterface* CShellExtensionInterface::CreateInitialized()
{
	CShellExtensionInterface* ext = new CShellExtensionInterface;
	if (!ext->IsLoaded())
	{
		delete ext;
		return 0;
	}

	if (ext->InitDrag().empty())
	{
		delete ext;
		return 0;
	}

	return ext;
}

//{7BB79969-2C7E-4107-996C-36DB90890AB2}

#endif //__WXMSW__

CRemoteDataObject::CRemoteDataObject(const CServer& server, const CServerPath& path)
	: wxDataObjectSimple(wxDataFormat(_T("FileZilla3RemoteDataObject")))
	, m_server(server)
	, m_path(path)
	, m_didSendData()
	, m_processId(wxGetProcessId())
{
}

CRemoteDataObject::CRemoteDataObject()
	: wxDataObjectSimple(wxDataFormat(_T("FileZilla3RemoteDataObject")))
	, m_didSendData()
	, m_processId(wxGetProcessId())
{
}

size_t CRemoteDataObject::GetDataSize() const
{
	wxASSERT(!m_path.empty());

	wxCHECK(m_xmlFile.GetElement(), 0);

	return const_cast<CRemoteDataObject*>(this)->m_xmlFile.GetRawDataLength() + 1;
}

bool CRemoteDataObject::GetDataHere(void *buf) const
{
	wxASSERT(!m_path.empty());

	wxCHECK(m_xmlFile.GetElement(), false);

	const_cast<CRemoteDataObject*>(this)->m_xmlFile.GetRawDataHere((char*)buf);
	((char*)buf)[const_cast<CRemoteDataObject*>(this)->m_xmlFile.GetRawDataLength()] = 0;

	const_cast<CRemoteDataObject*>(this)->m_didSendData = true;
	return true;
}

void CRemoteDataObject::Finalize()
{
	// Convert data into XML
	TiXmlElement* pElement = m_xmlFile.CreateEmpty();
	pElement = pElement->LinkEndChild(new TiXmlElement("RemoteDataObject"))->ToElement();

	AddTextElement(pElement, "ProcessId", m_processId);

	TiXmlElement* pServer = pElement->LinkEndChild(new TiXmlElement("Server"))->ToElement();
	SetServer(pServer, m_server);

	AddTextElement(pElement, "Path", m_path.GetSafePath());

	TiXmlElement* pFiles = pElement->LinkEndChild(new TiXmlElement("Files"))->ToElement();
	for (std::list<t_fileInfo>::const_iterator iter = m_fileList.begin(); iter != m_fileList.end(); ++iter)
	{
		TiXmlElement* pFile = pFiles->LinkEndChild(new TiXmlElement("File"))->ToElement();
		AddTextElement(pFile, "Name", iter->name);
		AddTextElement(pFile, "Dir", iter->dir ? 1 : 0);
		AddTextElement(pFile, "Size", iter->size.ToString());
		AddTextElement(pFile, "Link", iter->link ? 1 : 0);
	}
}

bool CRemoteDataObject::SetData(size_t len, const void* buf)
{
	char* data = (char*)buf;
	if (data[len - 1] != 0)
		return false;

	if (!m_xmlFile.ParseData(data))
		return false;

	TiXmlElement* pElement = m_xmlFile.GetElement();
	if (!pElement || !(pElement = pElement->FirstChildElement("RemoteDataObject")))
		return false;

	m_processId = GetTextElementInt(pElement, "ProcessId", -1);
	if (m_processId == -1)
		return false;

	TiXmlElement* pServer = pElement->FirstChildElement("Server");
	if (!pServer || !::GetServer(pServer, m_server))
		return false;

	wxString path = GetTextElement(pElement, "Path");
	if (path.empty() || !m_path.SetSafePath(path))
		return false;

	m_fileList.clear();
	TiXmlElement* pFiles = pElement->FirstChildElement("Files");
	if (!pFiles)
		return false;

	for (TiXmlElement* pFile = pFiles->FirstChildElement("File"); pFile; pFile = pFile->NextSiblingElement("File"))
	{
		t_fileInfo info;
		info.name = GetTextElement(pFile, "Name");
		if (info.name.empty())
			return false;

		const int dir = GetTextElementInt(pFile, "Dir", -1);
		if (dir == -1)
			return false;
		info.dir = dir == 1;

		info.size = GetTextElementLongLong(pFile, "Size", -2);
		if (info.size <= -2)
			return false;

		info.link = GetTextElementBool(pFile, "Link", false);

		m_fileList.push_back(info);
	}

	return true;
}

void CRemoteDataObject::AddFile(const wxString& name, bool dir, const wxLongLong& size, bool link)
{
	t_fileInfo info;
	info.name = name;
	info.dir = dir;
	info.size = size;
	info.link = link;

	m_fileList.push_back(info);
}
