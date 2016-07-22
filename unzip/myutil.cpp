#include "stdafx.h"
#include "myutil.h"


CString AddPath(CString sLeft, CString sRight, CString sPathChar)
{
	CString sRet;
	sRight =
		sRight.GetLength() == 0 ? _T("") :
		(sRight.GetLength() == 1 && sRight.Left(1) == sPathChar) ? _T("") :
		sRight.Left(1) == sPathChar ? sRight.Mid(1) : sRight;
	sRet = (sLeft.Right(1) == sPathChar) ? sLeft + sRight : sLeft + sPathChar + sRight;
	return sRet;
}


BOOL	DigPath(CString sFolderPath)
{
	if (PathIsDirectory(sFolderPath)) return TRUE;	// ¶ÕÀ» ÇÊ¿ä ¾ø´Ù.

	CString	sSubPath;
	int		nFrom, nTo;

	nFrom = 0;
	nTo = 0;
	nFrom = sFolderPath.Find(_T("\\"), 0) + 1;
	for (;;)
	{
		nTo = sFolderPath.Find(_T("\\"), nFrom);
		if (nTo == -1) break;
		sSubPath = sFolderPath.Left(nTo);

		::CreateDirectory(sSubPath, NULL);
		nFrom = nTo + 1;
	}

	CreateDirectory(sFolderPath, NULL);
	return PathIsDirectory(sFolderPath);
}

CString GetParentPath(const CString& sPathName)
{
	CString sRet;
	sRet = sPathName;
	if (sRet.Right(1) == _T("\\"))
		sRet = sRet.Left(sRet.GetLength() - 1);			// ³¡ÀÇ \\ ¶¼±â 
	sRet = sRet.Left(sRet.ReverseFind('\\') + 1);
	return sRet;
}


LARGE_INTEGER benchmark_start_real;
FILETIME benchmark_start_cpu;
INT64 benchmark_real_time_us = 0;
INT64 benchmark_cpu_time_us = 0;

void StartBenchmarkTiming() 
{
	QueryPerformanceCounter(&benchmark_start_real);
	FILETIME dummy;
	GetProcessTimes(GetCurrentProcess(), &dummy, &dummy, &dummy, &benchmark_start_cpu);
}


void StopBenchmarkTiming() 
{
	LARGE_INTEGER benchmark_stop_real;
	LARGE_INTEGER benchmark_frequency;
	QueryPerformanceCounter(&benchmark_stop_real);
	QueryPerformanceFrequency(&benchmark_frequency);

	double elapsed_real = static_cast<double>(
		benchmark_stop_real.QuadPart - benchmark_start_real.QuadPart) /
		benchmark_frequency.QuadPart;
	benchmark_real_time_us += (INT)(elapsed_real * 1e6 + 0.5);

	FILETIME benchmark_stop_cpu, dummy;
	GetProcessTimes(GetCurrentProcess(), &dummy, &dummy, &dummy, &benchmark_stop_cpu);

	ULARGE_INTEGER start_ulargeint;
	start_ulargeint.LowPart = benchmark_start_cpu.dwLowDateTime;
	start_ulargeint.HighPart = benchmark_start_cpu.dwHighDateTime;

	ULARGE_INTEGER stop_ulargeint;
	stop_ulargeint.LowPart = benchmark_stop_cpu.dwLowDateTime;
	stop_ulargeint.HighPart = benchmark_stop_cpu.dwHighDateTime;

	benchmark_cpu_time_us += (stop_ulargeint.QuadPart - start_ulargeint.QuadPart + 5) / 10;
}
