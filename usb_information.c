

#include <SetupAPI.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <winioctl.h>

void DumpVidPidMi(LPCTSTR pszDeviceInstanceId)
{
	TCHAR szDeviceInstanceId[MAX_DEVICE_ID_LEN];
	const static LPCTSTR arPrefix[3] = { TEXT("VID_"), TEXT("PID_"), TEXT("MI_") };
	LPTSTR pszToken, pszNextToken;
	int j;

	lstrcpy(szDeviceInstanceId, pszDeviceInstanceId);

	pszToken = _tcstok_s(szDeviceInstanceId, TEXT("\\#&"), &pszNextToken);
	while (pszToken != NULL) {
		for (j = 0; j < 3; j++) {
			if (_tcsncmp(pszToken, arPrefix[j], lstrlen(arPrefix[j])) == 0) {
				switch (j) {
				case 0:
					_tprintf(TEXT("        vid: \"%s\"\n"), pszToken + lstrlen(arPrefix[j]));
					break;
				case 1:
					_tprintf(TEXT("        pid: \"%s\"\n"), pszToken + lstrlen(arPrefix[j]));
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
			if (DRIVE_REMOVABLE == DrvType) {
				_tprintf(TEXT("    Volume Device Name: %s\n"), szVolumeName);
				_tprintf(TEXT("    Found a volume (type=%d) : %S\n"), DrvType, PathNames);
			}
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
	BOOL bFound = FALSE;
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
				bSuccess = DeviceIoControl(hDev,                           // device to be queried
					IOCTL_STORAGE_GET_DEVICE_NUMBER,
					NULL, 0,                        // no input buffer
					(LPVOID)&sdn, sizeof(sdn),      // output buffer
					&cbBytesReturned,               // # bytes returned
					(LPOVERLAPPED)NULL);           // synchronous I/O
				if (bSuccess) {
					if (sdn.DeviceType == DeviceType && sdn.DeviceNumber == DeviceNumber) {

						DEVINST dnDevInstParent, dnDevInstParentParent;
						CONFIGRET ret;
						// device found !!!
						TCHAR szBuffer[4096];

						_tprintf(TEXT("    DevicePath: %s\n"), pInterfaceDetailData->DevicePath);

						bSuccess = SetupDiGetDeviceInstanceId(hIntDevInfo, &deviceInfoData, pszDeviceInstanceId,
							dwDeviceInstanceIdSize, &dwRequiredSize);
						if (dwRequiredSize > MAX_DEVICE_ID_LEN)
							continue;

						bSuccess = SetupDiGetDeviceRegistryProperty(hIntDevInfo, &deviceInfoData, SPDRP_REMOVAL_POLICY, &dwDataType,
							(PBYTE)pdwRemovalPolicy, sizeof(DWORD), &dwRequiredSize);

						bSuccess = SetupDiGetDeviceRegistryProperty(hIntDevInfo, &deviceInfoData, SPDRP_CLASS, &dwDataType,
							(PBYTE)szBuffer, sizeof(szBuffer), &dwRequiredSize);
						if (bSuccess)
							_tprintf(TEXT("    Class: \"%s\"\n"), szBuffer);
						bSuccess = SetupDiGetDeviceRegistryProperty(hIntDevInfo, &deviceInfoData, SPDRP_HARDWAREID, &dwDataType,
							(PBYTE)szBuffer, sizeof(szBuffer), &dwRequiredSize);
						if (bSuccess) {
							LPCTSTR pszId;
							_tprintf(TEXT("    Hardware IDs:\n"));
							for (pszId = szBuffer;
								*pszId != TEXT('\0') && pszId + dwRequiredSize / sizeof(TCHAR) <= szBuffer + ARRAYSIZE(szBuffer);
								pszId += lstrlen(pszId) + 1) {

								_tprintf(TEXT("        \"%s\"\n"), pszId);
							}
						}
						bSuccess = SetupDiGetDeviceRegistryProperty(hIntDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, &dwDataType,
							(PBYTE)szBuffer, sizeof(szBuffer), &dwRequiredSize);
						if (bSuccess)
							_tprintf(TEXT("    Friendly Name: \"%s\"\n"), szBuffer);

						bSuccess = SetupDiGetDeviceRegistryProperty(hIntDevInfo, &deviceInfoData, SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, &dwDataType,
							(PBYTE)szBuffer, sizeof(szBuffer), &dwRequiredSize);
						if (bSuccess)
							_tprintf(TEXT("    Physical Device Object Name: \"%s\"\n"), szBuffer);

						bSuccess = SetupDiGetDeviceRegistryProperty(hIntDevInfo, &deviceInfoData, SPDRP_DEVICEDESC, &dwDataType,
							(PBYTE)szBuffer, sizeof(szBuffer), &dwRequiredSize);
						if (bSuccess)
							_tprintf(TEXT("    Device Description: \"%s\"\n"), szBuffer);
							
						bFound = TRUE;

						ret = CM_Get_Parent(&dnDevInstParent, deviceInfoData.DevInst, 0);
						if (ret == CR_SUCCESS) {
							TCHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
							ret = CM_Get_Device_ID(dnDevInstParent, szDeviceInstanceID, ARRAY_SIZE(szDeviceInstanceID), 0);
							if (ret == CR_SUCCESS) {
								_tprintf(TEXT("    Parent Device Instance ID: %s\n"), szDeviceInstanceID);
								DumpVidPidMi(szDeviceInstanceID);
								ret = CM_Get_Parent(&dnDevInstParentParent, dnDevInstParent, 0);
								if (ret == CR_SUCCESS) {
									ret = CM_Get_Device_ID(dnDevInstParentParent, szDeviceInstanceID, ARRAY_SIZE(szDeviceInstanceID), 0);
									if (ret == CR_SUCCESS)
										_tprintf(TEXT("    Parent of Parent Device Instance ID: %s\n"), szDeviceInstanceID);
								}
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

	return bFound;
}

BOOL getUSB(){
	unsigned index;
	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA DeviceInfoData;
	TCHAR buffs[1024];
	DWORD dwFlag = (DIGCF_PRESENT | DIGCF_PROFILE);
	GUID* guidDev = (GUID*)&GUID_DEVCLASS_USB;
	// List all connected USB devices 
	//hDevInfo = SetupDiGetClassDevs(NULL, TEXT("USB"), NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
	hDevInfo = SetupDiGetClassDevs(guidDev, NULL, NULL, dwFlag);
	for (index = 0; ; index++) {
		DeviceInfoData.cbSize = sizeof(DeviceInfoData);
		if (!SetupDiEnumDeviceInfo(hDevInfo, index, &DeviceInfoData)) {
			return false;  // no match 
		}
		// print USB all information
		int count = 0;
		_tprintf(TEXT("%d=USB Information ============================================\n"), index);
		for (count = 0; count <= 37; count++) {
			// SPDRP_HARDWAREID
			SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData, count, NULL, (BYTE*)buffs, sizeof(buffs), NULL);
			_tprintf(TEXT("\t \"%s\"\n"), buffs);
		}		
		
		/*if (_tcsstr(buffs, _T("VID_1234&PID_5678"))) {
			return true;  // match 
		}*/
	}
}

void getUSBPath() {
	//Print USB Path
	SP_DEVICE_INTERFACE_DATA devInterfaceData;
	SP_DEVINFO_DATA deviceInfoData, devInfoData;
	DWORD dwDataType, dwRequiredSize;
	PSP_DEVICE_INTERFACE_DETAIL_DATA pDevDetail = NULL;
	BYTE buffer[1024];
	DEVINST devInstParent;
	TCHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
	WCHAR volume[4096];
	int index;
	HDEVINFO hDevInfo;
	
	// volume
	GUID* guid = (GUID*)&GUID_DEVINTERFACE_VOLUME;
	hDevInfo = SetupDiGetClassDevs(guid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	for (index = 0; ; index++) {
		_tprintf(TEXT("%d===USB PATH==============================================\n"), index);
		ZeroMemory(&devInterfaceData, sizeof(devInterfaceData));
		devInterfaceData.cbSize = sizeof(devInterfaceData);

		if (!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, guid, index, &devInterfaceData)) { // Get device Interface data. 		
			break;
		}
		ZeroMemory(&devInfoData, sizeof(devInfoData));
		devInfoData.cbSize = sizeof(devInfoData);

		pDevDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)buffer;
		pDevDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, pDevDetail, sizeof(buffer), &dwRequiredSize, &devInfoData); // SP_DEVINFO_DATA 

		/*CM_Get_Parent(&devInstParent, devInfoData.DevInst, 0); // Get the device instance of parent. This points to USBSTOR. 
		CM_Get_Device_ID(devInstParent, szDeviceInstanceID, ARRAY_SIZE(szDeviceInstanceID), 0);
		_tprintf(TEXT("    Parent Device Path Instance ID: %s\n"), szDeviceInstanceID);
		*/
		size_t nLength = wcslen(pDevDetail->DevicePath);
		pDevDetail->DevicePath[nLength] = L'\\';
		pDevDetail->DevicePath[nLength + 1] = 0;

		if (GetVolumeNameForVolumeMountPoint(pDevDetail->DevicePath, volume, sizeof(volume))) {
			//Here you will get the volume corresponding to the usb 
			_tprintf(TEXT("\tVolume Path: %s\n"), volume);
		}
		//=======================================================================
		
		STORAGE_DEVICE_NUMBER sdn;
		HDEVINFO hDevice;
		BOOL bSuccess;
		BYTE byBuffer[4096];
		DWORD cbBytesReturned;

		hDevice = CreateFile(volume,
			//FILE_READ_DATA, //0 - no access to the drive, for IOCTL_STORAGE_CHECK_VERIFY is FILE_READ_DATA needed
			FILE_READ_ATTRIBUTES, // for IOCTL_STORAGE_CHECK_VERIFY2
			FILE_SHARE_READ | FILE_SHARE_WRITE,   // share mode
			NULL, OPEN_EXISTING, 0, NULL);
		if (hDevice == INVALID_HANDLE_VALUE)
			_tprintf(TEXT("\tCreateFile Error\n"));

		
		bSuccess = DeviceIoControl(hDevice,                         // device to be queried
			IOCTL_STORAGE_GET_DEVICE_NUMBER,
			NULL, 0,                         // no input buffer
			(LPVOID)&sdn, sizeof(sdn),       // output buffer
			&cbBytesReturned,                // # bytes returned
			(LPOVERLAPPED)NULL);            // synchronous I/O

		if (bSuccess) {
			_tprintf(TEXT("    DeviceType: %d, DeviceNumber: %d, PartitionNumber: %d\n"), sdn.DeviceType, sdn.DeviceNumber, sdn.PartitionNumber);
			
		}
		_tprintf(TEXT("%d===USB PATH END==========================================\n"), index);
	}
	//==========================================
}

// Search USB information for all disk letters.
int Get_USB_Device_Information()
{
	HANDLE hDevice = NULL;
	DWORD cbBytesReturned;
	STORAGE_DEVICE_NUMBER sdn;
	BOOL bSuccess;
	LPTSTR pszLogicalDrives = NULL, pszDriveRoot;
	TCHAR szDeviceInstanceId[MAX_DEVICE_ID_LEN];
	GUID* pGuidInferface = NULL, * pGuidClass = NULL;
	LPCTSTR pszEnumerator = NULL;
	TCHAR szNtDeviceName[MAX_PATH + 1];
	DWORD dwRemovalPolicy;

	__try {
		cbBytesReturned = GetLogicalDriveStrings(0, NULL);
		pszLogicalDrives = (LPTSTR)LocalAlloc(LMEM_ZEROINIT, cbBytesReturned * sizeof(TCHAR));
		cbBytesReturned = GetLogicalDriveStrings(cbBytesReturned, pszLogicalDrives);

		for (pszDriveRoot = pszLogicalDrives; *pszDriveRoot != TEXT('\0'); pszDriveRoot += lstrlen(pszDriveRoot) + 1) {
			TCHAR szDeviceName[7] = TEXT("\\\\.\\");
			szDeviceName[4] = pszDriveRoot[0];
			szDeviceName[5] = TEXT(':');
			szDeviceName[6] = TEXT('\0');
			_tprintf(TEXT("Drive %c:\n"), pszDriveRoot[0]);

			cbBytesReturned = QueryDosDevice(&szDeviceName[4], szNtDeviceName, ARRAYSIZE(szNtDeviceName));
			if (cbBytesReturned) {
				_tprintf(TEXT("    Dos Device Name: %s\n"), szNtDeviceName);
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

				bSuccess = FindDiInfos(pGuidInferface, pGuidClass, pszEnumerator,
					sdn.DeviceType,
					sdn.DeviceNumber,
					ARRAY_SIZE(szDeviceInstanceId),
					szDeviceInstanceId,
					&dwRemovalPolicy);
			}
			if (CloseHandle(hDevice))
				hDevice = INVALID_HANDLE_VALUE;
		}
	}
	__finally {
		if (pszLogicalDrives)
			pszLogicalDrives = (LPTSTR)LocalFree(pszLogicalDrives);
		if (hDevice != INVALID_HANDLE_VALUE)
			bSuccess = CloseHandle(hDevice);
	}
	return 0;
}
