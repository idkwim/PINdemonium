// ScyllaWrapper.cpp: definisce le funzioni esportate per l'applicazione DLL.
//

#pragma once
#include "stdafx.h"
#include "ScyllaWrapper.h"
#include "debug.h"
#include "Log.h"


#define SCYLLA_ERROR_FILE_FROM_PID -4
#define SCYLLA_ERROR_DUMP -3
#define SCYLLA_ERROR_IAT_NOT_FOUND -2
#define SCYLLA_ERROR_IAT_NOT_FIXED -1
#define SCYLLA_SUCCESS_FIX 0

void SetCurrentLogDirectory(const CHAR * currentPath){
	
}

/**
Extract the .EXE file which has lauched the process having PID pid
**/
BOOL GetFilePathFromPID(DWORD dwProcessId, WCHAR *filename){
	
	HANDLE processHandle = NULL;

	processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwProcessId);
	if (processHandle) {
	if (GetModuleFileNameEx(processHandle,NULL, filename, MAX_PATH) == 0) {
    //if (GetProcessImageFileName(processHandle, filename, MAX_PATH) == 0) {
		INFO("Failed to get module filename.");
		return false;
	}
	CloseHandle(processHandle);
	} else {
		INFO("Failed to open process." );
		return false;
	}

	return true;
	
}


DWORD_PTR GetExeModuleBase(DWORD dwProcessId)
{
	MODULEENTRY32 lpModuleEntry = { 0 };
	HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, dwProcessId);
	lpModuleEntry.dwSize = sizeof(lpModuleEntry);
	Module32First(hSnapShot, &lpModuleEntry);

	CloseHandle(hSnapShot);

	return (DWORD_PTR)lpModuleEntry.modBaseAddr;
}



UINT32 IATAutoFix(DWORD pid, DWORD_PTR oep, WCHAR *outputFile, WCHAR * cur_path)
{

	Log::getInstance()->initLogPath(cur_path);//Initialize the log File NEED TO BE BEFORE ANY INFO(),WARN(),ERROR()

	INFO("\n----------------IAT Fixing at %08x----------------",oep);

	
	
	DWORD_PTR iatStart = 0;
	DWORD iatSize = 0;
	WCHAR originalExe[MAX_PATH]; // Path of the original PE which as launched the current process
	WCHAR *dumpFile = L"./tmp_dump_file.exe";  //Path of the file where the process will be dumped during the Dumping Process

	//getting the Base Address
	DWORD_PTR hMod = GetExeModuleBase(pid);
	if(!hMod){
		INFO("Can't find PID");
	}
	INFO("GetExeModuleBase %X", hMod);

	//Dumping Process
	BOOL success = GetFilePathFromPID(pid,originalExe);
	if(!success){
		INFO("Error in getting original Path from Pid: %d",pid);
		return SCYLLA_ERROR_FILE_FROM_PID;
	}
	INFO("Original Exe Path: %S",originalExe);
		
	success = ScyllaDumpProcessW(pid,originalExe,hMod,oep,dumpFile);
	if(!success){
		INFO("[SCYLLA DUMP] Error Dumping  Pid: %d, FileToDump: %S, Hmod: %X, oep: %X, output: %S ",pid,originalExe,hMod,oep,dumpFile);
		return SCYLLA_ERROR_DUMP;
	}
	INFO("[SCYLLA DUMP] Successfully dumped Pid: %d, FileToDump: %S, Hmod: %X, oep: %X, output: %S ",pid,originalExe,hMod,oep,dumpFile);
		
	//DebugBreak();
	//Searching the IAT
	int error = ScyllaIatSearch(pid, &iatStart, &iatSize, hMod + 0x00001028, TRUE);
	if(error){
		INFO("[SCYLLA SEARCH] error %d ",error);
		return SCYLLA_ERROR_IAT_NOT_FOUND;
	}
	//DebugBreak();
	INFO("[SCYLLA SEARCH] iat_start : %08x\t iat_size : %08x\t pid : %d", iatStart,iatSize,pid,outputFile);

	//Fixing the IAT
	error = ScyllaIatFixAutoW(iatStart,iatSize,pid,dumpFile,outputFile);
	if(error){
		INFO("[SCYLLA FIX] error %d",error);
		return SCYLLA_ERROR_IAT_NOT_FIXED;
	}
	INFO("[SCYLLA FIX] Success  fixed file at %S",outputFile);
	return SCYLLA_SUCCESS_FIX;
	
}




UINT32 ScyllaDumpAndFix(int pid, int oep, WCHAR * output_file,WCHAR * cur_path){
	
	return IATAutoFix(pid, oep, output_file,cur_path);
}


void ScyllaWrapAddSection(const WCHAR * dump_path , const CHAR * sectionName, DWORD sectionSize, UINT32 offset, BYTE * sectionData){
	ScyllaAddSection(dump_path , sectionName, sectionSize, offset , sectionData);
}


