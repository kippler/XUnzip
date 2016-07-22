#include "stdafx.h"
#include "../src/XUnzip.h"
#include "myutil.h"

void PrintUsage()
{
	printf("xunzip.exe [zip_path_name] [folder_path_to_extract]\n");
}


BOOL ExtractAllTo(XUnzip& unzip, CString targetFolder)
{
	int count = unzip.GetFileCount();

	for (int i = 0; i < count; i++)
	{
		const XUnzipFileInfo* fileInfo = unzip.GetFileInfo(i);
		CString fileName = fileInfo->fileName;
		CString filePathName = AddPath(targetFolder, fileName, L"\\");
		CString fileFolder = ::GetParentPath(filePathName);
		::DigPath(fileFolder);

		XFileWriteStream outFile;
		if (outFile.Open(filePathName) == FALSE)
		{
			wprintf(L"ERROR: Can't open %s\n", filePathName.GetString());
			return FALSE;
		}

		wprintf(L"Extracting: %s..", filePathName.GetString());
		if (unzip.ExtractTo(i, &outFile))
		{
			wprintf(L"done\n");
		}
		else
		{
			wprintf(L"Error\n");
			return FALSE;
		}
	}
	return TRUE;
}



BOOL Unzip(CString zipPathName, CString targetFolder)
{
	XUnzip		unzip;

	if (unzip.Open(zipPathName) == FALSE)
	{
		wprintf(L"ERROR: Can't open %s\n", zipPathName.GetString());
		return FALSE;
	}

	INT64 tick = ::GetTickCount64();
	BOOL ret = ExtractAllTo(unzip, targetFolder);
	INT64 elapsed = GetTickCount64() - tick;
	if (ret)
		wprintf(L"Elapsed time : %dms\n", (int)elapsed);
	return ret;
}




int wmain(int argc, WCHAR* argv[])
{
	if (argc < 3)
	{
		PrintUsage();
		return 0;
	}

	CString zipPathName = argv[1];
	CString targetFolder = argv[2];

	BOOL ret = Unzip(zipPathName, targetFolder);

    return ret ? 0 : 1;
}

