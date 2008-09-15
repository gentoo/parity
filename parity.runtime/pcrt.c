#include <windows.h>
#include <stdio.h>
#include <intrin.h>

#define RING_SIZE 8

typedef void (*cygwin_init_func_t)();
typedef void (*cygwin_conv_func_t)(const char*, char*);

const char* PcrtPathToNative(const char* ptr) {
	//
	// either return the same string, or allocate another one and convert.
	//
#ifdef _WIN32
	static char* pRing[RING_SIZE] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	static int iRingNum = -1;

	int szRoot = 0;

	if(!ptr)
		return 0;

	if(ptr[0] != '/')
		return ptr;

	iRingNum = ++iRingNum % RING_SIZE;

	if(pRing[iRingNum])
		HeapFree(GetProcessHeap(), 0, pRing[iRingNum]);

	//
	// try converting ourselves if possible.
	//
	if((szRoot = GetEnvironmentVariable("INTERIX_ROOT_WIN", 0, 0)) != 0)
	{
		char* ptrRoot = HeapAlloc(GetProcessHeap(), 0, szRoot);

		if(!GetEnvironmentVariable("INTERIX_ROOT_WIN", ptrRoot, szRoot)) {
			HeapFree(GetProcessHeap(), 0, ptrRoot);
		} else {
			char* ptrWalk = 0;

			if(CompareString(LOCALE_USER_DEFAULT, 0, "/dev/fs", 7, ptr, 7) == CSTR_EQUAL)
			{
				ptr += 8;

				//
				// now we have the drive letter under our cursor...
				//
				pRing[iRingNum] = HeapAlloc(GetProcessHeap(), 0, lstrlen(ptr) + 2);
				
				lstrcpyn(pRing[iRingNum], ptr, 2);
				lstrcpyn(&pRing[iRingNum][1], ":", 2);
				lstrcpyn(&pRing[iRingNum][2], &ptr[1], lstrlen(ptr)); // not +1 since we're allread copying one cahr more.
			} else {
				int sub = 0;
				pRing[iRingNum] = HeapAlloc(GetProcessHeap(), 0, szRoot + lstrlen(ptr) + 1);

				if(ptrRoot[lstrlen(ptrRoot) - 1] == '\\' || ptrRoot[lstrlen(ptrRoot) - 1] == '/')
					sub = 1;
				
				lstrcpyn(pRing[iRingNum], ptrRoot, szRoot);
				lstrcpyn(&pRing[iRingNum][lstrlen(ptrRoot) - sub], ptr, lstrlen(ptr) + 1);
			}

			ptrWalk = pRing[iRingNum];

			while(ptrWalk && *ptrWalk != '\0') {
				if(*ptrWalk == '/')
					*ptrWalk = '\\';

				++ptrWalk;
			}

			HeapFree(GetProcessHeap(), 0, ptrRoot);

			return pRing[iRingNum];
		}
	} else {
		static HMODULE hCygLib = NULL;
		static int inited = 0;
		
		if(!hCygLib)
			hCygLib = LoadLibrary("cygwin1.dll");

		if(hCygLib)
		{
			static cygwin_init_func_t init = NULL; 
			static cygwin_conv_func_t conv = NULL; 

			if(!init)
				init = (cygwin_init_func_t)GetProcAddress(hCygLib, "cygwin_dll_init");
			if(!conv)
				conv = (cygwin_conv_func_t)GetProcAddress(hCygLib, "cygwin_conv_to_full_win32_path");

			if(init && conv)
			{
				//
				// allocate a ring slot, and then convert using cygwin dll.
				//
				pRing[iRingNum] = HeapAlloc(GetProcessHeap(), 0, _MAX_PATH);

				if(!inited++)
					init();

				conv(ptr, pRing[iRingNum]);

				return pRing[iRingNum];
			} else {
				fprintf(stderr, "Cannot load all required functions from cygwin1.dll, cannot convert absolute UNIX paths!\n");
			}
		} else {
			fprintf(stderr, "Neither Interix Installation nor Cygwin DLL found, cannot convert absolute UNIX paths!\n");
		}
	}

	//
	// still around?
	//
	fprintf(stderr, "would need real path conversion for %s. Please report this!\n", ptr);

	return ptr;
#else
	return ptr;
#endif
}