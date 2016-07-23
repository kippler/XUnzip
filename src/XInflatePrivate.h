////////////////////////////////////////////////////////////////////////////////////////////////////
/// 
/// 
/// 
/// @author   parkkh
/// @date     Sun Jul 24 02:19:48 2016
/// 
/// Copyright(C) 2016 Bandisoft, All rights reserved.
/// 
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

/**********************************************************************************

-= history =-

* 2010/4/5 코딩 시작
* 2010/4/9 1차 완성
- 속도 비교 (34.5MB 짜리 gzip 파일, Q9400 CPU)
. zlib : 640ms
. xinflate : 1900ms

* 2010/4/29
- 속도 향상 최적화
. 일부 함수 호출을 매크로화 시키기 : 1900ms
. 비트 스트림을 로컬 변수화 시키기: 1800ms
. Write 함수 매크로화: 1750ms
. Window 멤버 변수화: 1680ms
. Window 매크로화 + 로컬 변수화 : 1620ms
. InputBuffer 로컬 변수화 : 1610ms
. 출력 윈도우 로컬 변수화 : 1500ms;
. while() 루프에서 m_bCompleted 비교 제거 : 1470ms;
. while() 루프내에서 m_error 비교 제거 : 1450ms
. m_state 로컬 변수화 : 1450ms
. m_windowLen, m_windowDistCode 로컬 변수화 : 1390ms

- 나름 최적화 후 속도 차이 비교 (34.5MB 짜리 gzip 파일, Q9400 CPU)
. zlib : 640MS
. Halibut : 1750ms
. XInflate : 1370ms

* 2010/4/30
- 코드 정리
- v1.0 최초 공개

* 2010/5/11
- static table 이 필요할때만 생성하도록 수정
- 최적화
. 최적화 전 : 1340ms
. CHECK_TABLE 제거 : 1330ms
. FILLBUFFER() 개선: 1320ms
. table을 링크드 리스트에서 배열로 수정 : 1250ms

* 2010/5/12
- 최적화 계속
. STATE_GET_LEN 의 break 제거 : 1220ms
. STATE_GET_DISTCODE 의 break 제거 : 1200ms
. STATE_GET_DIST 의 break 제거 : 1170ms

* 2010/5/13
- 최적화 계속
. FastInflate() 로 분리후 FILLBUFFER_FAST, HUFFLOOKUP_FAST 적용 : 1200ms
. 허프만 트리 처리 방식 개선(단일 테이블 참조로 트리 탐색은 없애고 메모리 사용량도 줄이고) : 900ms
. HUFFLOOKUP_FAST 다시 적용 : 890ms
. WRITE_FAST 적용 : 810ms
. lz77 윈도우 출력시 while() 을 do-while 로 변경 : 800ms

* 2010/5/19
- 출력 버퍼를 내부 alloc 대신 외부 버퍼를 이용할 수 있도록 기능 추가
- m_error 변수 제거

* 2010/5/25
- 외부버퍼 출력 기능쪽 버그 수정
- direct copy 쪽 약간 개선

* 2010/08/31
- 테이블 생성시 이상한값 들어오면 에러리턴하도록 수정

* 2011/08/01
- STATE_GET_LEN 다음에 FILLBUFFER 를 안하고 STATE_GET_DISTCODE 로 넘어가면서 압축을 제대로 풀지 못하는 경우가 있던
버그 수정 ( Inflate() 랑 FastInflate() 두곳 다 )

* 2011/08/16
- coderecord 의 구조체를 사용하지 않고, len 과 dist에 접근하도록 수정
142MB 샘플 : 1820ms -> 1720ms 로 빨라짐
- HUFFLOOKUP_FAST 에서 m_pCurrentTable 를 참조하지 않고, pItems 를 참조하도록 수정
1720ms -> 1700ms 로 빨라짐 (zlib 는 1220ms)

* 2012/01/28
- windowDistCode 가 32, 33 등이 될때 에러 처리 코드 수정
- _CreateTable 에서 테이블이 손상된 경우 에러 처리 코드 사용

* 2012/02/7
- XFastHuffItem::XFastHuffItem() 를 주석으로 막았던거 풀어줌.

* 2016/7/21
- 기존의 zlib 스타일의 루프 방식을 콜백 호출 방식으로 수정
- 사전 버퍼도 따로 할당하지 않고, 출력 버퍼를 같이 사용하도록 수정

* 2016/7/22
- BYPASS 할때 루프 대신 WRITE_BLOCK 로 memcpy 로 속도 향상

***********************************************************************************/


// DEFLATE 의 압축 타입
enum BTYPE
{
	BTYPE_NOCOMP = 0,
	BTYPE_FIXEDHUFFMAN = 1,
	BTYPE_DYNAMICHUFFMAN = 2,
	BTYPE_RESERVED = 3		// ERROR
};


#define LENCODES_SIZE		29
#define DISTCODES_SIZE		30

static int lencodes_extrabits[LENCODES_SIZE] = { 0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   2,   2,   2,   2,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   0 };
static int lencodes_min[LENCODES_SIZE] = { 3,   4,   5,   6,   7,   8,   9,  10,  11,  13,  15,  17,  19,  23,  27,  31,  35,  43,  51,  59,  67,  83,  99, 115, 131, 163, 195, 227, 258 };
static int distcodes_extrabits[DISTCODES_SIZE] = { 0, 0, 0, 0, 1, 1,  2,  2,  3,  3,  4,  4,  5,   5,   6,   6,   7,   7,   8,    8,    9,    9,   10,   10,   11,   11,    12,    12,    13,    13 };
static int distcodes_min[DISTCODES_SIZE] = { 1, 2, 3, 4, 5, 7,  9, 13, 17, 25, 33, 49, 65,  97, 129, 193, 257, 385, 513,  769, 1025, 1537, 2049, 3073, 4097, 6145,  8193, 12289, 16385, 24577 };


// code length 
static const unsigned char lenlenmap[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };


// 유틸 매크로
#ifndef ASSERT
#	define ASSERT(x) {}
#endif
#define SAFE_DEL(x) if(x) {delete x; x=NULL;}
#define SAFE_FREE(x) if(x) {free(x); x=NULL;}

////////////////////////////////////////////////
//
// 디버깅용 매크로
//
#ifdef _DEBUG
#	define ADD_INPUT_COUNT			m_inputCount ++
#	define ADD_OUTPUT_COUNT			m_outputCount ++
#	define ADD_OUTPUT_COUNTX(x)		m_outputCount += x
#else
#	define ADD_INPUT_COUNT	
#	define ADD_OUTPUT_COUNT
#	define ADD_OUTPUT_COUNTX(x)
#endif

#define	DOERR(x) { ASSERT(0); ret = x; goto END; }



