// Benchmark.cpp : 콘솔 응용 프로그램에 대한 진입점을 정의합니다.
//

#include "stdafx.h"


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



int main()
{
    return 0;
}

