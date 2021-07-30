#include <windows.h>
#include <objbase.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <memory>
#include <set>
#include <mutex>
#include <iostream>
#include <projectedfslib.h>

using namespace std;

struct GUIDComparer {
	bool operator()(const GUID& Left, const GUID& Right) const {
		return memcmp(&Left, &Right, sizeof(Right)) < 0;
	}
};

std::set<GUID, GUIDComparer> dirEnumerations;
std::mutex dirEnumMutex;

bool FileExists(const wchar_t* file) {
	WIN32_FIND_DATA FindFileData;
	HANDLE handle = FindFirstFile(file, &FindFileData);
	if (handle != INVALID_HANDLE_VALUE) FindClose(handle);
	return handle;
}

HRESULT PrjStartDirectoryEnumerationCb(const PRJ_CALLBACK_DATA* callbackData, const GUID* enumerationId) {
	wprintf(L"PrjStartDirectoryEnumerationCb: %s\n", callbackData->FilePathName);
	wstring fileName(callbackData->FilePathName);
	if (fileName != L"") return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	lock_guard<mutex> guard(dirEnumMutex);
	dirEnumerations.emplace(*enumerationId);
	return S_OK;
}

HRESULT PrjEndDirectoryEnumerationCb(const PRJ_CALLBACK_DATA* callbackData, const GUID* enumerationId) {
	wprintf(L"PrjEndDirectoryEnumerationCb: %s\n", callbackData->FilePathName);
	lock_guard<mutex> guard(dirEnumMutex);
	dirEnumerations.erase(*enumerationId);
	return S_OK;
}

HRESULT PrjGetDirectoryEnumerationCb(const PRJ_CALLBACK_DATA* callbackData, const GUID* enumerationId, PCWSTR searchExpression, PRJ_DIR_ENTRY_BUFFER_HANDLE dirEntryBufferHandle) {
	wprintf(L"PrjGetDirectoryEnumerationCb: %s, search=%s flags=%d\n", callbackData->FilePathName, searchExpression, callbackData->Flags);
	wstring fileName(callbackData->FilePathName);
	if (fileName != L"") return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	bool containsWildcards = PrjDoesNameContainWildCards(searchExpression);
	lock_guard<mutex> guard(dirEnumMutex);
	bool firstSearch = (dirEnumerations.find(*enumerationId) != dirEnumerations.end());
	dirEnumerations.erase(*enumerationId);
	bool needsRestart = (callbackData->Flags & PRJ_CB_DATA_FLAG_ENUM_RESTART_SCAN);
	bool match = (wcslen(searchExpression) == 0) ||
		(containsWildcards && PrjFileNameMatch(L"test", searchExpression)) ||
		(!containsWildcards && 0 == PrjFileNameCompare(searchExpression, L"test"));
	if (match && (firstSearch || needsRestart)) {
		PRJ_FILE_BASIC_INFO info = {};
		info.IsDirectory = false;
		info.FileSize = 0;
		HRESULT hr = PrjFillDirEntryBuffer(L"test", &info, dirEntryBufferHandle);
		cout << "returning entry for 'test': " << hr << endl;
	}
	return S_OK;
}

HRESULT PrjGetPlaceholderInfoCb(const PRJ_CALLBACK_DATA* callbackData) {
	bool isTest = 0 == PrjFileNameCompare(callbackData->FilePathName, L"test");
	wprintf(L"PrjGetPlaceholderInfoCb: %s, exists=%d\n", callbackData->FilePathName, isTest);
	if (!isTest) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	PRJ_PLACEHOLDER_INFO info = {};
	info.FileBasicInfo.IsDirectory = false;
	info.FileBasicInfo.FileSize = 0;
	PrjWritePlaceholderInfo(callbackData->NamespaceVirtualizationContext, callbackData->FilePathName, &info, sizeof(info));
	return S_OK;
}

HRESULT PrjGetFileDataCb(const PRJ_CALLBACK_DATA* callbackData, UINT64 byteOffset, UINT32 length) {
	bool isTest = 0 == PrjFileNameCompare(callbackData->FilePathName, L"test");
	wprintf(L"PrjGetFileDataCb: %s, exists=%d\n", callbackData->FilePathName, isTest);
	if (!isTest) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	return S_OK;
}

HRESULT PrjQueryFileNameCb(const PRJ_CALLBACK_DATA* callbackData) {
	bool isTest = 0 == PrjFileNameCompare(callbackData->FilePathName, L"test");
	wprintf(L"PrjQueryFileNameCb: %s, exists=%d\n", callbackData->FilePathName, isTest);
	if (!isTest) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	return S_OK;
}

int main() {
	HRESULT hr;
	const wchar_t* rootName = L"C:\\Temp\\Virt1";
	const wchar_t* virtFileName = L"C:\\Temp\\Virt1\\test";
	if (FileExists(virtFileName)) DeleteFile(virtFileName);
	if (FileExists(rootName)) RemoveDirectory(rootName);
	if (!CreateDirectoryW(rootName, nullptr)) {
		cout << "cannot create virtualization root" << endl;
		return -1;
	}
	GUID instanceId;
	hr = CoCreateGuid(&instanceId);
	if (FAILED(hr)) {
		cout << "Failed to create instance ID " << hr << endl;
		return -1;
	}
	hr = PrjMarkDirectoryAsPlaceholder(rootName, nullptr, nullptr, &instanceId);
	if (FAILED(hr)) {
		cout << "Failed to mark virtualization root " << hr << endl;
		return -1;
	}
	PRJ_CALLBACKS callbackTable;
	callbackTable.StartDirectoryEnumerationCallback = (PRJ_START_DIRECTORY_ENUMERATION_CB*)PrjStartDirectoryEnumerationCb;
	callbackTable.EndDirectoryEnumerationCallback = (PRJ_END_DIRECTORY_ENUMERATION_CB*)PrjEndDirectoryEnumerationCb;
	callbackTable.GetDirectoryEnumerationCallback = (PRJ_GET_DIRECTORY_ENUMERATION_CB*)PrjGetDirectoryEnumerationCb;
	callbackTable.GetPlaceholderInfoCallback = (PRJ_GET_PLACEHOLDER_INFO_CB*)PrjGetPlaceholderInfoCb;
	callbackTable.GetFileDataCallback = (PRJ_GET_FILE_DATA_CB*)PrjGetFileDataCb;
	callbackTable.QueryFileNameCallback = (PRJ_QUERY_FILE_NAME_CB*)PrjQueryFileNameCb;
	callbackTable.NotificationCallback = nullptr;
	callbackTable.CancelCommandCallback = nullptr;
	PRJ_NAMESPACE_VIRTUALIZATION_CONTEXT instanceHandle;
	hr = PrjStartVirtualizing(rootName, &callbackTable, nullptr, nullptr, &instanceHandle);
	if (FAILED(hr)) {
		cerr << "Failed to start the virtualization instance " << hr << endl;
		return -1;
	}
	std::string str;
	cout << "Virtual FS started. Open file c:\\temp\\virt1\\temp in Microsoft Visual Studio, change and save it. "
		<< " Then Press <enter> here and PrjDeleteFile will be called reverting file to its original state" << endl;
	getline(cin, str);
	cout << "deleting file" << endl;
	hr = PrjDeleteFile(instanceHandle, L"test", PRJ_UPDATE_ALLOW_DIRTY_DATA | PRJ_UPDATE_ALLOW_DIRTY_METADATA | PRJ_UPDATE_ALLOW_READ_ONLY | PRJ_UPDATE_ALLOW_TOMBSTONE, nullptr);
	if (FAILED(hr)) {
		cerr << "Failed to delete file: " << (hr & 0xFFFF) << endl;
		return -1;
	}
	cout << "Now try to open this file again... " << endl;
	cout << "Press enter to stop" << endl;
	getline(cin, str);
	cout << "Stopping virt instance" << endl;
	PrjStopVirtualizing(instanceHandle);
	return 0;
}