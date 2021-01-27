
#include <SetupAPI.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <winioctl.h>

BOOL DumpVidPidMi(LPCTSTR pszDeviceInstanceId)
{
	TCHAR szDeviceInstanceId[MAX_DEVICE_ID_LEN];
	const static LPCTSTR arPrefix[3] = { TEXT("VID_"), TEXT("PID_"), TEXT("MI_") };
	LPTSTR pszToken, pszNextToken;
	int j;
	BOOL bRet = 0, bVid = 1, bPid = 1;

	lstrcpy(szDeviceInstanceId, pszDeviceInstanceId);

	pszToken = _tcstok_s(szDeviceInstanceId, TEXT("\\#&"), &pszNextToken);
	while (pszToken != NULL) {
		for (j = 0; j < 3; j++) {
			if (_tcsncmp(pszToken, arPrefix[j], lstrlen(arPrefix[j])) == 0) {
				switch (j) {
				case 0:
					_tprintf(TEXT("        vid: \"%s\"\n"), pszToken + lstrlen(arPrefix[j]));
					bVid = lstrcmp(VID, pszToken + lstrlen(arPrefix[j]));
					//_tprintf(L"VID: %s\n", bVid ? L"False":L"True");
					break;
				case 1:
					_tprintf(TEXT("        pid: \"%s\"\n"), pszToken + lstrlen(arPrefix[j]));
					bPid = lstrcmp(PID, pszToken + lstrlen(arPrefix[j]));
					break;
				case 2:
					_tprintf(TEXT("        mi: \"%s\"\n"), pszToken + lstrlen(arPrefix[j]));
					break;
				default:
					break;
				}
			}
		}
		pszToken = _tcstok_s(NULL, TEXT("\\#&"), &pszNextToken);
	}
	// Check whether VID and PID are MeeNote.
	if (bVid == 0 && bPid == 0) {
		return 1;
	}
	else {
		return 0;
	}
}

void FildVolumeName(LPCTSTR pszDeviceName)
{
	TCHAR szVolumeName[MAX_PATH] = TEXT("");
	TCHAR szDeviceName[MAX_PATH] = TEXT("");
	HANDLE hFind = INVALID_HANDLE_VALUE;
	DWORD dwCharCount;
	BOOL bSuccess;

	hFind = FindFirstVolume(szVolumeName, ARRAYSIZE(szVolumeName));
	if (hFind == INVALID_HANDLE_VALUE) return;

	while (TRUE) {
		//  Skip the \\?\ prefix and remove the trailing backslash.
		size_t Index = lstrlen(szVolumeName) - 1;
		if (szVolumeName[0] != TEXT('\\') ||
			szVolumeName[1] != TEXT('\\') ||
			szVolumeName[2] != TEXT('?') ||
			szVolumeName[3] != TEXT('\\') ||
			szVolumeName[Index] != TEXT('\\')) return; // error

		//  QueryDosDeviceW doesn't allow a trailing backslash,
		//  so temporarily remove it.
		szVolumeName[Index] = TEXT('\0');
		dwCharCount = QueryDosDevice(&szVolumeName[4], szDeviceName, ARRAYSIZE(szDeviceName));
		szVolumeName[Index] = TEXT('\\');
		if (dwCharCount == 0) return; // error

		PWCHAR PathNames = GetVolumePaths(szVolumeName);
		UINT DrvType = GetDriveType(PathNames);
		if (lstrcmp(pszDeviceName, szDeviceName) == 0) {
			/*if (DRIVE_REMOVABLE == DrvType) {
				_tprintf(TEXT("    Volume Device Name: %s\n"), szVolumeName);
				_tprintf(TEXT("    Found a volume (type=%d) : %S\n"), DrvType, PathNames);
			}*/
			return;
		}

		bSuccess = FindNextVolume(hFind, szVolumeName, ARRAYSIZE(szVolumeName));
		if (!bSuccess) {
			DWORD dwErrorCode = GetLastError();
			if (dwErrorCode == ERROR_NO_MORE_ITEMS)
				break;
			else
				break;  // ERROR!!!
		}
	}
}

BOOL FindDiInfos(LPCGUID pGuidInferface,
	LPCGUID pGuidClass,
	LPCTSTR pszEnumerator,
	DEVICE_TYPE DeviceType,
	DWORD DeviceNumber,
	DWORD dwDeviceInstanceIdSize,     // MAX_DEVICE_ID_LEN
	OUT LPTSTR pszDeviceInstanceId,
	OUT PDWORD pdwRemovalPolicy)
{
	HDEVINFO hIntDevInfo = NULL;
	DWORD dwIndex;
	BOOL bRet = FALSE;
	HANDLE hDev = INVALID_HANDLE_VALUE;
	PSP_DEVICE_INTERFACE_DETAIL_DATA pInterfaceDetailData = NULL;

	// set defaults
	*pdwRemovalPolicy = 0;
	pszDeviceInstanceId[0] = TEXT('\0');

	__try {
		hIntDevInfo = SetupDiGetClassDevs(pGuidInferface, pszEnumerator, NULL,
			pGuidInferface != NULL ? DIGCF_PRESENT | DIGCF_DEVICEINTERFACE :
			DIGCF_ALLCLASSES | DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (hIntDevInfo == INVALID_HANDLE_VALUE)
			__leave;

		for (dwIndex = 0; ; dwIndex++) {
			SP_DEVICE_INTERFACE_DATA interfaceData;
			SP_DEVINFO_DATA deviceInfoData;
			DWORD dwDataType, dwRequiredSize;
			BOOL bSuccess;

			ZeroMemory(&interfaceData, sizeof(interfaceData));
			interfaceData.cbSize = sizeof(interfaceData);
			bSuccess = SetupDiEnumDeviceInterfaces(hIntDevInfo, NULL, pGuidInferface, dwIndex, &interfaceData);
			if (!bSuccess) {
				DWORD dwErrorCode = GetLastError();
				if (dwErrorCode == ERROR_NO_MORE_ITEMS)
					break;
				else
					break;  // ERROR!!!
			}

			dwRequiredSize = 0;
			bSuccess = SetupDiGetDeviceInterfaceDetail(hIntDevInfo, &interfaceData, NULL, 0, &dwRequiredSize, NULL);
			if ((!bSuccess && GetLastError() != ERROR_INSUFFICIENT_BUFFER) || dwRequiredSize == 0)
				continue;  // ERROR!!!

			if (pInterfaceDetailData)
				pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalFree(pInterfaceDetailData);

			pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalAlloc(LPTR, dwRequiredSize);
			pInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
			ZeroMemory(&deviceInfoData, sizeof(deviceInfoData));
			deviceInfoData.cbSize = sizeof(deviceInfoData);
			bSuccess = SetupDiGetDeviceInterfaceDetail(hIntDevInfo, &interfaceData,
				pInterfaceDetailData, dwRequiredSize, &dwRequiredSize, &deviceInfoData);
			if (!bSuccess)
				continue;

			hDev = CreateFile(pInterfaceDetailData->DevicePath,
				0,                                   // no access to the drive
				FILE_SHARE_READ | FILE_SHARE_WRITE,  // share mode
				NULL,                                // default security attributes
				OPEN_EXISTING,                       // disposition
				0,                                   // file attributes
				NULL);                               // do not copy file attributes
			if (hDev != INVALID_HANDLE_VALUE) {
				STORAGE_DEVICE_NUMBER sdn;
				DWORD cbBytesReturned;
				bSuccess = DeviceIoControl(hDev,        // device to be queried
					IOCTL_STORAGE_GET_DEVICE_NUMBER,
					NULL, 0,                            // no input buffer
					(LPVOID)&sdn, sizeof(sdn),          // output buffer
					&cbBytesReturned,                   // # bytes returned
					(LPOVERLAPPED)NULL);                // synchronous I/O
				if (bSuccess) {
					if (sdn.DeviceType == DeviceType && sdn.DeviceNumber == DeviceNumber) {
						DEVINST dnDevInstParent, dnDevInstParentParent;
						CONFIGRET ret;
						TCHAR szBuffer[4096];
						//_tprintf(TEXT("    DevicePath: %s\n"), pInterfaceDetailData->DevicePath);

						bSuccess = SetupDiGetDeviceInstanceId(hIntDevInfo, &deviceInfoData, pszDeviceInstanceId,
							dwDeviceInstanceIdSize, &dwRequiredSize);
						if (dwRequiredSize > MAX_DEVICE_ID_LEN)
							continue;
						bSuccess = SetupDiGetDeviceRegistryProperty(hIntDevInfo, &deviceInfoData, SPDRP_REMOVAL_POLICY, &dwDataType,
							(PBYTE)pdwRemovalPolicy, sizeof(DWORD), &dwRequiredSize);

						ret = CM_Get_Parent(&dnDevInstParent, deviceInfoData.DevInst, 0);
						if (ret == CR_SUCCESS) {
							TCHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
							ret = CM_Get_Device_ID(dnDevInstParent, szDeviceInstanceID, ARRAY_SIZE(szDeviceInstanceID), 0);
							if (ret == CR_SUCCESS) {
								//_tprintf(TEXT("    Parent Device Instance ID: %s\n"), szDeviceInstanceID);
								bRet = DumpVidPidMi(szDeviceInstanceID);
								//_tprintf(L"\tDumpVidPidMi: %s\n", bRet ? L"True" : L"Flase");
							}
						}
						break;
					}
				}
				CloseHandle(hDev);
				hDev = INVALID_HANDLE_VALUE;
			}
		}
	}
	__finally {
		if (pInterfaceDetailData)
			pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalFree(pInterfaceDetailData);

		if (hDev != INVALID_HANDLE_VALUE)
			CloseHandle(hDev);

		if (hIntDevInfo)
			SetupDiDestroyDeviceInfoList(hIntDevInfo);
	}

	return bRet;
}

// The USB information of the disk code is passed in by FindUSBVolume.
BOOL Get_USB_Driver_VID_PID(PWCHAR DeviceName)
{
	HANDLE hDevice = NULL;
	DWORD cbBytesReturned;
	STORAGE_DEVICE_NUMBER sdn;
	BOOL bSuccess, bRet = FALSE;
	LPTSTR pszLogicalDrives = NULL, pszDriveRoot;
	TCHAR szDeviceInstanceId[MAX_DEVICE_ID_LEN];
	GUID* pGuidInferface = NULL, * pGuidClass = NULL;
	LPCTSTR pszEnumerator = NULL;
	TCHAR szNtDeviceName[MAX_PATH + 1];
	DWORD dwRemovalPolicy;

	__try {
		TCHAR szDeviceName[7] = TEXT("\\\\.\\");
		WCHAR drive[16];
		wsprintf(drive, L"\\\\.\\%c:", DeviceName[0]);
		szDeviceName[4] = drive[4];
		szDeviceName[5] = TEXT(':');
		szDeviceName[6] = TEXT('\0');
		//_tprintf(TEXT("Drive %s:\n"), drive);

		cbBytesReturned = QueryDosDevice(&szDeviceName[4], szNtDeviceName, ARRAYSIZE(szNtDeviceName));
		if (cbBytesReturned) {
			//_tprintf(TEXT("    Dos Device Name: %s\n"), szNtDeviceName);
			FildVolumeName(szNtDeviceName);
		}

		//_tprintf(TEXT("    Volume Name: %s\n"), szDeviceName);
		hDevice = CreateFile(szDeviceName,
			//FILE_READ_DATA,					// 0 - no access to the drive, for IOCTL_STORAGE_CHECK_VERIFY is FILE_READ_DATA needed
			FILE_READ_ATTRIBUTES,               // for IOCTL_STORAGE_CHECK_VERIFY2
			FILE_SHARE_READ | FILE_SHARE_WRITE, // share mode
			NULL, OPEN_EXISTING, 0, NULL);
		bSuccess = DeviceIoControl(hDevice,     // device to be queried
			IOCTL_STORAGE_GET_DEVICE_NUMBER,
			NULL, 0,                            // no input buffer
			(LPVOID)&sdn, sizeof(sdn),          // output buffer
			&cbBytesReturned,                   // # bytes returned
			(LPOVERLAPPED)NULL);                // synchronous I/O

		if (bSuccess) {
			//_tprintf(TEXT("    DeviceType: %d, DeviceNumber: %d, PartitionNumber: %d\n"), sdn.DeviceType, sdn.DeviceNumber, sdn.PartitionNumber);
			pGuidInferface = NULL;
			pGuidClass = NULL;
			pGuidInferface = (GUID*)&GUID_DEVINTERFACE_DISK;
			pGuidClass = (GUID*)&GUID_DEVCLASS_DISKDRIVE;

			bRet = FindDiInfos(pGuidInferface, pGuidClass, pszEnumerator,
				sdn.DeviceType,
				sdn.DeviceNumber,
				ARRAY_SIZE(szDeviceInstanceId),
				szDeviceInstanceId,
				&dwRemovalPolicy);
		}
		if (CloseHandle(hDevice))
			hDevice = INVALID_HANDLE_VALUE;
	}
	__finally {
		if (hDevice != INVALID_HANDLE_VALUE)
			bSuccess = CloseHandle(hDevice);
	}
	return bRet;
}

// Main
void FindUSBVolume(HWND hWnd)
{
	DWORD  Error = ERROR_SUCCESS;
	HANDLE FindHandle = INVALID_HANDLE_VALUE;
	size_t Index = 0;
	BOOL   Success = FALSE;
	WCHAR  VolumeName[MAX_PATH] = L"";
	PWCHAR PathNames = NULL;
	BOOL  Connected = 0;

	//
	//  Enumerate all volumes in the system.
	FindHandle = FindFirstVolume(VolumeName, ARRAYSIZE(VolumeName));

	if (FindHandle == INVALID_HANDLE_VALUE)
	{
		Error = GetLastError();
		printf("FindFirstVolume error=%d\n", Error);
		return;
	}

	for (;;)
	{
		//
		//  Skip the \\?\ prefix and remove the trailing backslash.
		Index = wcslen(VolumeName) - 1;

		if (VolumeName[0] != L'\\' ||
			VolumeName[1] != L'\\' ||
			VolumeName[2] != L'?' ||
			VolumeName[3] != L'\\' ||
			VolumeName[Index] != L'\\')
		{
			Error = ERROR_BAD_PATHNAME;
			printf("FindFirstVolume/FindNextVolume returned a bad path: %S\n", VolumeName);
			break;
		}

		PathNames = GetVolumePaths(VolumeName);
		UINT DrvType = GetDriveType(PathNames);
		if (DRIVE_REMOVABLE == DrvType) {
			printf("\nFound a volume (type=%d) : %S\n", DrvType, PathNames);
			//wprintf(L"VolumeName: %s\n", VolumeName);
			if (Get_USB_Driver_VID_PID(PathNames)) {
				Connected = Connect(PathNames);
			}
		}

		if (PathNames) {
			delete[] PathNames;
			PathNames = NULL;
		}

		if (Connected)
		{
			break;
		}

		//  Move on to the next volume.
		Success = FindNextVolume(FindHandle, VolumeName, ARRAYSIZE(VolumeName));

		if (!Success)
		{
			Error = GetLastError();

			if (Error != ERROR_NO_MORE_FILES)
			{
				printf("FindNextVolume failed with error code %d\n", Error);
				break;
			}

			//
			//  Finished iterating
			//  through all the volumes. 
			Error = ERROR_SUCCESS;
			break;
		}
	}

	FindVolumeClose(FindHandle);
	FindHandle = INVALID_HANDLE_VALUE;

	if (Connected) {
		PostMessage(hWnd, WM_USER_CONNECTED, 0, 0);
	}
	else {
		UpdateUI(0);
	}
}