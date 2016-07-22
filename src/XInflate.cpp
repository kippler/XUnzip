#include "stdafx.h"
#include "XInflate.h"

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
	BTYPE_NOCOMP			= 0,
	BTYPE_FIXEDHUFFMAN		= 1,
	BTYPE_DYNAMICHUFFMAN	= 2,
	BTYPE_RESERVED			= 3		// ERROR
};


#define LENCODES_SIZE		29
#define DISTCODES_SIZE		30

static int lencodes_extrabits	[LENCODES_SIZE]  = {  0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   2,   2,   2,   2,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   0};
static int lencodes_min			[LENCODES_SIZE]  = {  3,   4,   5,   6,   7,   8,   9,  10,  11,  13,  15,  17,  19,  23,  27,  31,  35,  43,  51,  59,  67,  83,  99, 115, 131, 163, 195, 227, 258};
static int distcodes_extrabits	[DISTCODES_SIZE] = {0, 0, 0, 0, 1, 1,  2,  2,  3,  3,  4,  4,  5,   5,   6,   6,   7,   7,   8,    8,    9,    9,   10,   10,   11,   11,    12,    12,    13,    13};
static int distcodes_min		[DISTCODES_SIZE] = {1, 2, 3, 4, 5, 7,  9, 13, 17, 25, 33, 49, 65,  97, 129, 193, 257, 385, 513,  769, 1025, 1537, 2049, 3073, 4097, 6145,  8193, 12289, 16385, 24577};


// code length 
static const unsigned char lenlenmap[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};


// 유틸 매크로
#define HUFFTABLE_VALUE_NOT_EXIST	-1
#ifndef ASSERT
#	define ASSERT(x) {}
#endif
#define SAFE_DEL(x) if(x) {delete x; x=NULL;}
#define SAFE_FREE(x) if(x) {free(x); x=NULL;}



////////////////////////////////////////////////
//
// 비트 스트림 매크로화
//
#define BS_EATBIT()								\
	bits & 0x01;								\
	bitLen --;									\
	bits >>= 1;									\
	if(bitLen<0) ASSERT(0);

#define BS_EATBITS(count)						\
	(bits & ((1<<(count))-1));					\
	bitLen -= count;							\
	bits >>= count;								\
	if(bitLen<0) ASSERT(0);

#define BS_REMOVEBITS(count)					\
	bits >>= (count);							\
	bitLen -= (count);							\
	if(bitLen<0) ASSERT(0); 

#define BS_MOVETONEXTBYTE						\
	BS_REMOVEBITS((bitLen % 8));

#define BS_ADDBYTE(byte)						\
	bits |= (byte) << bitLen;					\
	bitLen += 8;
//
// 비트 스트림 매크로 처리
//
////////////////////////////////////////////////


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


////////////////////////////////////////////////
//
// 반복 작업 매크로화
//
#define FILLBUFFER()							\
	while(bitLen<=24)							\
	{											\
		if(inBufferRemain==0)					\
			break;								\
		BS_ADDBYTE(*inBuffer);					\
		inBufferRemain--;						\
		inBuffer++;								\
		ADD_INPUT_COUNT;						\
	}											\
	if(bitLen<=0)								\
		goto END;


#define HUFFLOOKUP(result, pTable)								\
		/* 데이타 부족 */										\
		if(pTable->bitLenMin > bitLen)							\
		{														\
			result = -1;										\
		}														\
		else													\
		{														\
			pItem = &(pTable->pItem[pTable->mask & bits]);		\
			/* 데이타 부족 */									\
			if(pItem->bitLen > bitLen)							\
			{													\
				result = -1;									\
			}													\
			else												\
			{													\
				result = pItem->symbol;							\
				BS_REMOVEBITS(pItem->bitLen);					\
			}													\
		}


// 출력 버퍼에 한바이트 쓰기
#define WRITE(byte)												\
	ADD_OUTPUT_COUNT;											\
	WIN_ADD;													\
	CHECK_AND_FLUSH_OUT_BUFFER									\
	*outBufferCur = byte;										\
	outBufferCur++;


// 출력 버퍼가 꽉찼으면 -> 비우고 + 출력 버퍼 위치 이동 초기화 + 사전 데이타를 출력 버퍼 앞으로 복사
#define CHECK_AND_FLUSH_OUT_BUFFER								\
	if(outBufferCur>=outBufferEnd)								\
	{															\
		if(stream->Write(windowStartPos, (int)(outBufferCur - windowStartPos))==FALSE)		\
			return XINFLATE_ERR_USER_STOP;						\
		int _dist = (int)(outBufferCur - windowCurPos);			\
		memcpy(windowStartPos-DEFLATE_WINDOW_SIZE, outBufferCur - DEFLATE_WINDOW_SIZE, DEFLATE_WINDOW_SIZE);	\
		windowCurPos = windowStartPos-_dist ;					\
		outBufferCur = windowStartPos;							\
	}															\
	


////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              FastInflate
//


// 입력 버퍼가 충분한지 체크하지 않는다.
#define FILLBUFFER_FAST()								\
	while(bitLen<=24)									\
	{													\
		BS_ADDBYTE(*inBuffer);							\
		inBufferRemain--;								\
		inBuffer++;										\
		ADD_INPUT_COUNT;								\
	}													\

// 비트스트림 데이타(bits + bitLen)가 충분한지 체크하지 않는다.
#define HUFFLOOKUP_FAST(result)							\
		pItem = &(pItems[mask & bits]);					\
		result = pItem->symbol;							\
		BS_REMOVEBITS(pItem->bitLen);					


// 출력 버퍼가 충분한지 체크하지 않는다.
#define WRITE_FAST(byte)							\
	ADD_OUTPUT_COUNT;								\
	WIN_ADD;										\
	*outBufferCur = byte;							\
	outBufferCur++;												


#define WRITE_BLOCK(in, len)						\
	ADD_OUTPUT_COUNTX(len);							\
	memcpy(outBufferCur, in, len);					\
	windowCurPos +=len;								\
	outBufferCur +=len;

////////////////////////////////////////////////
//
// 윈도우 매크로화
//

#define WIN_ADD				(windowCurPos++)
#define WIN_GETBUF(dist)	(windowCurPos - dist)

//
// 윈도우 매크로화
//
////////////////////////////////////////////////



/////////////////////////////////////////////////////
//
// 허프만 트리를 빨리 탐색할 수 있게 하기 위해서
// 트리 전체를 하나의 배열로 만들어 버린다.
//   

struct XFastHuffItem									// XFastHuffTable 의 아이템
{
	// 깨진 데이타에서 버퍼 오버플로우를 막기 위해서 항상 초기화를 해줘야 한다 ㅠ.ㅠ
	XFastHuffItem() 
	{
		bitLen = 0;
		symbol = HUFFTABLE_VALUE_NOT_EXIST;
	}
	int		bitLen;										// 유효한 비트수
	int		symbol;										// 유효한 심볼
};

class XFastHuffTable
{
public :
	XFastHuffTable()
	{
		pItem = NULL;
	}
	~XFastHuffTable()
	{
		if(pItem){ delete[] pItem; pItem=NULL;}
	}
	// 배열 생성
	void	Create(int _bitLenMin, int _bitLenMax)
	{
		if(pItem) ASSERT(0);							// 발생 불가

		bitLenMin = _bitLenMin;
		bitLenMax = _bitLenMax;
		mask	  = (1<<bitLenMax) - 1;					// 마스크
		itemCount = 1<<bitLenMax;						// 조합 가능한 최대 테이블 크기
		pItem     = new XFastHuffItem[itemCount];		// 2^maxBitLen 만큼 배열을 만든다.
	}
	// 심볼 데이타를 가지고 전체 배열을 채운다.
	BOOL	SetValue(int symbol, int bitLen, UINT bitCode)
	{
		/*
			만일 허프만 트리 노드에서 0->1->1 이라는 데이타가 'A' 라면
			symbol = 'A',   bitLen = 3,   bitCode = 3  이 파라메터로 전달된다.

			* 실제 bitstream 은 뒤집어져 들어오기 때문에 나중에 참조를 빨리 하기 위해서
			  0->1->1 을 1<-1<-0 으로 뒤집는다.

			* 만일 bitLenMax 가 5 라면 뒤집어진 1<-1<-0 에 의 앞 2bit 로 조합 가능한
			  00110, 01110, 10110, 11110 에 대해서도 동일한 심볼을 참조할 수 있도록 만든다.
		*/
		//::Ark_DebugW(L"SetValue: %d %d %d", symbol, bitLen, bitCode);
		if(bitCode>=((UINT)1<<bitLenMax))
		{ASSERT(0); return FALSE;}			// bitLenmax 가 3 이라면.. 111 까지만 가능하다


		UINT revBitCode = 0;
		// 뒤집기
		int i;
		for(i=0;i<bitLen;i++)
		{
			revBitCode <<= 1;
			revBitCode |= (bitCode & 0x01);
			bitCode >>= 1;
		}

		int		add2code = (1<<bitLen);		// bitLen 이 3 이라면 add2code 에는 1000(bin) 이 들어간다

		// 배열 채우기
		for(;;)
		{
#ifdef _DEBUG
			if(revBitCode>=itemCount) ASSERT(0);

			if(pItem[revBitCode].symbol!=  HUFFTABLE_VALUE_NOT_EXIST) 
			{ ASSERT(0); return FALSE;}
#endif
			pItem[revBitCode].bitLen = bitLen;
			pItem[revBitCode].symbol = symbol;

			// 조합 가능한 bit code 를 처리하기 위해서 값을 계속 더한다.
			revBitCode += add2code;

			// 조합 가능한 범위가 벗어난 경우 끝낸다
			if(revBitCode >= itemCount)
				break;
		}
		return TRUE;
	}

	XFastHuffItem*	pItem;							// (huff) code 2 symbol 아이템, code가 배열의 위치 정보가 된다.
	int				bitLenMin;						// 유효한 최소 비트수
	int				bitLenMax;						// 유효한 최대 비트수
	UINT			mask;							// bitLenMax 에 대한 bit mask
	UINT			itemCount;						// bitLenMax 로 생성 가능한 최대 아이템 숫자
};

static const char* xinflate_copyright = 
"[XInflate - Copyright(C) 2016, by kippler]";

XInflate::XInflate()
{
	m_pStaticInfTable = NULL;
	m_pStaticDistTable = NULL;
	m_pDynamicInfTable = NULL;
	m_pDynamicDistTable = NULL;
	m_pCurrentTable = NULL;
	m_pCurrentDistTable = NULL;

	m_pLenLenTable = NULL;
	m_dicPlusOutBuffer = NULL;
	m_outBuffer = NULL;
	m_inBuffer = NULL;
	m_copyright = NULL;
}

XInflate::~XInflate()
{
	Free();
}

// 내부 메모리 alloc 해제
void XInflate::Free()
{
	SAFE_DEL(m_pStaticInfTable);
	SAFE_DEL(m_pStaticDistTable);
	SAFE_DEL(m_pDynamicInfTable);
	SAFE_DEL(m_pDynamicDistTable);
	SAFE_DEL(m_pLenLenTable);

	SAFE_FREE(m_dicPlusOutBuffer);
	SAFE_FREE(m_inBuffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///         내부 변수 초기화
/// @param  
/// @return 
/// @date   Thursday, April 08, 2010  4:17:46 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
XINFLATE_ERR XInflate::Init()
{
	SAFE_DEL(m_pLenLenTable);
	SAFE_DEL(m_pDynamicInfTable);
	SAFE_DEL(m_pDynamicDistTable);

	m_pCurrentTable = NULL;
	m_pCurrentDistTable = NULL;

	m_bFinalBlock = FALSE;
	m_copyright = xinflate_copyright;

	// 출력 버퍼 잡기
	if (m_dicPlusOutBuffer == NULL)
	{
		m_dicPlusOutBuffer = (BYTE*)malloc(DEFAULT_OUTBUF_SIZE + DEFLATE_WINDOW_SIZE);
		if (m_dicPlusOutBuffer == NULL){ASSERT(0); return XINFLATE_ERR_ALLOC_FAIL;}

		/*
		실제 버퍼 alloc 한 위치에서 윈도우 크기만큼 뒤쪽에 쓰기 시작한다.

		alloc 위치
		|		outBuffer 위치
		↓       ↓
		+-------+-----------------------------+
		| 사전  |                             |
		+-------+-----------------------------+
		*/

		// 버퍼의 시작은 윈도우 버퍼시작위치
		m_outBuffer = m_dicPlusOutBuffer + DEFLATE_WINDOW_SIZE;
	}

	if (m_inBuffer == NULL)
	{
		m_inBuffer = (BYTE*)malloc(DEFAULT_INBUF_SIZE);
		if (m_inBuffer == NULL){ASSERT(0); return XINFLATE_ERR_ALLOC_FAIL;}
	}

#ifdef _DEBUG
	m_inputCount = 0;
	m_outputCount = 0;
#endif

	return XINFLATE_ERR_OK;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///         압축 해제 - 호출전 Init() 을 반드시 호출해 줘야 한다.
/// @param  
/// @return 
/// @date   Thursday, April 08, 2010  4:18:55 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
#define	DOERR(x) { ASSERT(0); ret = x; goto END; }
XINFLATE_ERR XInflate::Inflate(IDecodeStream* stream)
{
	XINFLATE_ERR	ret = XINFLATE_ERR_OK;

	// 초기화
	ret = Init();
	if (ret != XINFLATE_ERR_OK) return ret;

	UINT		bits=0;
	int			bitLen=0;
	STATE		state= STATE_START;
	int			windowLen=0;
	int			windowDistCode=0;
	int			symbol=0;
	BYTE*		inBuffer = m_inBuffer;
	int			inBufferRemain = 0;
	UINT		uncompRemain = 0;

	BYTE*		outBufferCur = m_outBuffer;
	BYTE*		outBufferEnd = m_outBuffer + DEFAULT_OUTBUF_SIZE;
	BYTE*		windowStartPos = m_outBuffer;
	BYTE*		windowCurPos = m_outBuffer;

	// 로컬 변수
	int					extrabits;
	int					dist;
	XFastHuffItem*		pItem;			// HUFFLOOKUP() 에서 사용
	BYTE				byte;			// 임시 변수
	XINFLATE_ERR		err;

	// 루프 돌면서 압축 해제
	for(;;)
	{
		// 버퍼 채우기
		if (inBufferRemain == 0)
		{
			inBuffer = m_inBuffer;
			inBufferRemain = stream->Read(inBuffer, DEFAULT_INBUF_SIZE);
		}
		
		FILLBUFFER();

		switch(state)
		{
			// 헤더 분석 시작
		case STATE_START :
			if(bitLen<3) 
				goto END;

			// 마지막 블럭 여부
			m_bFinalBlock = BS_EATBIT();

			// 블럭타입 헤더는 2bit 
			{
				int bType = BS_EATBITS(2);

				if(bType==BTYPE_DYNAMICHUFFMAN)
					state = STATE_DYNAMIC_HUFFMAN;
				else if(bType==BTYPE_NOCOMP)
					state = STATE_NO_COMPRESSION;
				else if(bType==BTYPE_FIXEDHUFFMAN)
					state = STATE_FIXED_HUFFMAN;
				else
					DOERR(XINFLATE_ERR_HEADER);
			}
			break;

			// 압축 안됨
		case STATE_NO_COMPRESSION :
			BS_MOVETONEXTBYTE;
			state = STATE_NO_COMPRESSION_LEN;
			break;

		case STATE_NO_COMPRESSION_LEN :
			// LEN
			if(bitLen<16) 
				goto END;
			uncompRemain = BS_EATBITS(16);
			state = STATE_NO_COMPRESSION_NLEN;

			break;

		case STATE_NO_COMPRESSION_NLEN :
			// NLEN
			if(bitLen<16) 
				goto END;
			{
				UINT32 nlen = BS_EATBITS(16);
				// one's complement 
				if( (nlen^0xffff) != uncompRemain) 
					DOERR(XINFLATE_ERR_INVALID_NOCOMP_LEN);
			}
			state = STATE_NO_COMPRESSION_BYPASS;
			break;

		case STATE_NO_COMPRESSION_BYPASS :
			// 데이타 가져오기
			if(bitLen<8) 
				goto END;

			//////////////////////////////////////////////
			//
			// 원래 코드
			//
			/*
			{
				byte = BS_EATBITS(8);
				WRITE(byte);
			}
			uncompRemain--;
			*/
			if(bitLen%8!=0) ASSERT(0);			// 발생불가, 그냥 확인용

			//////////////////////////////////////////////
			//
			// 아래는 개선된 코드. 그런데 별 차이가 없다 ㅠ.ㅠ
			//

			// 비트 스트림을 먼저 비우고
			while(bitLen && uncompRemain)
			{
				byte = BS_EATBITS(8);
				WRITE(byte);
				uncompRemain--;
			}

			// 나머지 데이타는 바이트 그대로 쓰기
			{
				int	toCopy, toCopy2;
				toCopy = toCopy2 = min((int)uncompRemain, inBufferRemain);

				/* 원래코드
				while(toCopy)
				{
					WRITE(*inBuffer);
					inBuffer++;
					toCopy--;
				}
				*/

				if(outBufferEnd - outBufferCur > toCopy)
				{
					// 출력 버퍼가 충분한 경우 - 출력 버퍼가 충분한지 여부를 체크하지 않는다.
					/*
					while(toCopy)
					{
						WRITE_FAST(*inBuffer);
						inBuffer++;
						toCopy--;
					}
					*/
					WRITE_BLOCK(inBuffer, toCopy);
					inBuffer += toCopy;
					toCopy = 0;
				}
				else
				{
					while(toCopy)
					{
						WRITE(*inBuffer);
						inBuffer++;
						toCopy--;
					}
				}

				uncompRemain-=toCopy2;
				inBufferRemain-=toCopy2;
			}
			//
			// 개선된 코드 끝
			//
			//////////////////////////////////////////////

			if(uncompRemain==0)
			{
				if(m_bFinalBlock)
					state = STATE_COMPLETED;
				else
					state = STATE_START;
			}
			break;

			// 고정 허프만
		case STATE_FIXED_HUFFMAN :
			if(m_pStaticInfTable==NULL)
				CreateStaticTable();

			m_pCurrentTable = m_pStaticInfTable;
			m_pCurrentDistTable = m_pStaticDistTable;
			state = STATE_GET_SYMBOL;
			break;

			// 길이 가져오기
		case STATE_GET_LEN :
			// zlib 의 inflate_fast 호출 조건 흉내내기
			if(inBufferRemain>FASTINFLATE_MIN_BUFFER_NEED)
			{
				if(symbol<=256)	ASSERT(0);

				XINFLATE_ERR result = FastInflate(stream, inBuffer, inBufferRemain, 
										outBufferCur, 
										outBufferEnd,
										windowStartPos, windowCurPos,
										bits, bitLen, state, windowLen, windowDistCode, symbol);

				if(result!=XINFLATE_ERR_OK)
					return result;

				break;
			}

			extrabits = lencodes_extrabits[symbol - 257];
			if (bitLen < extrabits) 
				goto END;

			// RFC 1951 3.2.5
			// 기본 길이에 extrabit 만큼의 비트의 내용을 더하면 진짜 길이가 나온다
			windowLen = lencodes_min[symbol - 257] + BS_EATBITS(extrabits);

			state = STATE_GET_DISTCODE;
			FILLBUFFER();
			//break;	필요 없다..

			// 거리 코드 가져오기
		case STATE_GET_DISTCODE :
			HUFFLOOKUP(windowDistCode, m_pCurrentDistTable);

			if(windowDistCode<0)
				goto END;

			//if(windowDistCode==30 || windowDistCode==31)	// 30 과 31은 생길 수 없다. RFC1951 3.2.6
			if(windowDistCode>=30)							
				DOERR(XINFLATE_ERR_INVALID_DIST);

			state = STATE_GET_DIST;

			FILLBUFFER();
			//break;		// 필요없다

			// 거리 가져오기
		case STATE_GET_DIST:
			extrabits = distcodes_extrabits[windowDistCode];

			// DIST 구하기
			if(bitLen<extrabits)
				goto END;

			dist = distcodes_min[windowDistCode] + BS_EATBITS(extrabits);

			// lz77 출력
			while(windowLen)
			{
				byte = *WIN_GETBUF(dist);
				WRITE(byte);
				windowLen--;
			}
	
			state = STATE_GET_SYMBOL;

			FILLBUFFER();
			//break;		// 필요 없다

			// 심볼 가져오기.
		case STATE_GET_SYMBOL :
			HUFFLOOKUP(symbol, m_pCurrentTable);

			if(symbol<0) 
				goto END;
			else if(symbol<256)
			{
				byte = (BYTE)symbol;
				WRITE(byte);
				break;
			}
			else if(symbol==256)	// END OF BLOCK
			{
				if(m_bFinalBlock)
					state = STATE_COMPLETED;
				else
					state = STATE_START;
				break;
			}
			else if(symbol<286)
			{
				state = STATE_GET_LEN;
			}
			else
				DOERR(XINFLATE_ERR_INVALID_SYMBOL);		// 발생 불가

			break;

			// 다이나믹 허프만 시작
		case STATE_DYNAMIC_HUFFMAN :
			if(bitLen<5+5+4) 
				goto END;

			// 테이블 초기화
			SAFE_DEL(m_pDynamicInfTable);
			SAFE_DEL(m_pDynamicDistTable);

			m_literalsNum  = 257 + BS_EATBITS(5);		// 최대 288 (257+11111)
			m_distancesNum = 1   + BS_EATBITS(5);		// 최대 32
			m_lenghtsNum   = 4   + BS_EATBITS(4);

			if(m_literalsNum > 286 || m_distancesNum > 30)
				DOERR(XINFLATE_ERR_INVALID_LEN);

			memset(m_lengths, 0, sizeof(m_lengths));
			m_lenghtsPtr = 0;

			state = STATE_DYNAMIC_HUFFMAN_LENLEN ;
			break;

			// 길이의 길이 가져오기.
		case STATE_DYNAMIC_HUFFMAN_LENLEN :

			if(bitLen<3) 
				goto END;

			// 3bit 씩 코드 길이의 코드 길이 정보 가져오기.
			while (m_lenghtsPtr < m_lenghtsNum && bitLen >= 3) 
			{
				if(m_lenghtsPtr>sizeof(lenlenmap))						// 입력값 체크..
					DOERR(XINFLATE_ERR_INVALID_LEN);
#ifdef _DEBUG
				if(lenlenmap[m_lenghtsPtr]>=286+32) ASSERT(0);
				if(m_lenghtsPtr>=sizeof(lenlenmap)) ASSERT(0);
#endif

				m_lengths[lenlenmap[m_lenghtsPtr]] = BS_EATBITS(3);
				m_lenghtsPtr++;
			}

			// 다 가져왔으면..
			if (m_lenghtsPtr == m_lenghtsNum)
			{
				// 길이에 대한 허프만 테이블 만들기
				if(CreateTable(m_lengths, 19, m_pLenLenTable, err)==FALSE)
					DOERR(err);
				state = STATE_DYNAMIC_HUFFMAN_LEN;
				m_lenghtsPtr = 0;
			}
			break;

			// 압축된 길이 정보를 m_pLenLenTable 를 거쳐서 가져오기.
		case STATE_DYNAMIC_HUFFMAN_LEN:

			// 다 가져왔으면
			if (m_lenghtsPtr >= m_literalsNum + m_distancesNum) 
			{
				// 최종 다이나믹 테이블 생성 (literal + distance)
				if(	CreateTable(m_lengths, m_literalsNum, m_pDynamicInfTable, err)==FALSE ||
					CreateTable(m_lengths + m_literalsNum, m_distancesNum, m_pDynamicDistTable, err)==FALSE)
					DOERR(err);

				// lenlen 테이블은 이제 더이상 필요없다.
				SAFE_DEL(m_pLenLenTable);

				// 테이블 세팅
				m_pCurrentTable = m_pDynamicInfTable;
				m_pCurrentDistTable = m_pDynamicDistTable;

				// 진짜 압축 해제 시작
				state = STATE_GET_SYMBOL;
				break;
			}

			{
				// 길이 정보 코드 가져오기
				int code=-1;
				HUFFLOOKUP(code, m_pLenLenTable);

				if (code == -1)
					goto END;

				if (code < 16) 
				{
					if(m_lenghtsPtr>sizeof(m_lengths))		// 값 체크
						DOERR(XINFLATE_ERR_INVALID_LEN);

					m_lengths[m_lenghtsPtr] = code;
					m_lenghtsPtr++;
				} 
				else 
				{
					m_lenExtraBits = (code == 16 ? 2 : code == 17 ? 3 : 7);
					m_lenAddOn = (code == 18 ? 11 : 3);
					m_lenRep = (code == 16 && m_lenghtsPtr > 0 ? m_lengths[m_lenghtsPtr - 1] : 0);
					state = STATE_DYNAMIC_HUFFMAN_LENREP;
				}
			}
			break;

		case STATE_DYNAMIC_HUFFMAN_LENREP:
			if (bitLen < m_lenExtraBits)
				goto END;

			{
				int repeat = m_lenAddOn + BS_EATBITS(m_lenExtraBits);

				while (repeat > 0 && m_lenghtsPtr < m_literalsNum + m_distancesNum) 
				{
					m_lengths[m_lenghtsPtr] = m_lenRep;
					m_lenghtsPtr++;
					repeat--;
				}
			}

			state = STATE_DYNAMIC_HUFFMAN_LEN;
			break;

		case STATE_COMPLETED :
			goto END;
			break;

		default :
			ASSERT(0);
		}
	}

END :
	if (stream->Write(windowStartPos, (int)(outBufferCur - windowStartPos)) == FALSE)
		return XINFLATE_ERR_USER_STOP;

	if(state!= STATE_COMPLETED)			// 뭔가 잘못됬다.
	{ASSERT(0); return XINFLATE_ERR_STREAM_NOT_COMPLETED;}

	if (inBufferRemain) ASSERT(0);

	return ret;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///         length 정보만 가지고 code 를 생성한다. 
//          RFC 1951 3.2.2
/// @param  
/// @return 
/// @date   Wednesday, April 07, 2010  1:53:36 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XInflate::CreateCodes(BYTE* lengths, int numSymbols, int* codes)
{
	int		bits;
	int		code = 0;
	int		bitLenCount[MAX_CODELEN+1];
	int		next_code[MAX_CODELEN+1];
	int		i;
	int		n;
	int		len;

	memset(bitLenCount, 0, sizeof(bitLenCount));
	for(i=0;i<numSymbols;i++)
	{
		bitLenCount[lengths[i]] ++;
	}

	bitLenCount[0] = 0;

	for(bits=1;bits<=MAX_CODELEN;bits++)
	{
		code = (code + bitLenCount[bits-1]) << 1;
		next_code[bits] = code;
	}

	for(n=0; n<numSymbols; n++)
	{
		len = lengths[n];
		if(len!=0)
		{
			codes[n] = next_code[len];
			next_code[len]++;
		}
	}

	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         codes + lengths 정보를 가지고 테이블을 만든다.
/// @param  
/// @return 
/// @date   Wednesday, April 07, 2010  3:43:21 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XInflate::CreateTable(BYTE* lengths, int numSymbols, XFastHuffTable*& pTable, XINFLATE_ERR& err)
{
	int			codes[MAX_SYMBOLS];
	// lengths 정보를 가지고 자동으로 codes 정보를 생성한다.
	if(CreateCodes(lengths, numSymbols, codes)==FALSE) 
	{	
		err = XINFLATE_ERR_CREATE_CODES;
		ASSERT(0); 
		return FALSE;
	}
	if(_CreateTable(codes, lengths, numSymbols, pTable)==FALSE)
	{
		err = XINFLATE_ERR_CREATE_TABLE;
		ASSERT(0); 
		return FALSE;
	}
	return TRUE;
}
BOOL XInflate::_CreateTable(int* codes, BYTE* lengths, int numSymbols, XFastHuffTable*& pTable)
{
	int		bitLenCount[MAX_CODELEN+1];
	int		symbol;
	int		bitLen;

	// bit length 구하기
	memset(bitLenCount, 0, sizeof(bitLenCount));
	for(symbol=0;symbol<numSymbols;symbol++)
		bitLenCount[lengths[symbol]] ++;


	// 허프만 트리에서 유효한 최소 bitlen 과 최대 bitlen 구하기
	int	bitLenMax = 0;
	int	bitLenMin = MAX_CODELEN;

	for(bitLen=1;bitLen<=MAX_CODELEN;bitLen++)
	{
		if(bitLenCount[bitLen])
		{
			bitLenMax = max(bitLenMax, bitLen);
			bitLenMin = min(bitLenMin, bitLen);
		}
	}

	// 테이블 생성
	pTable = new XFastHuffTable;
	if(pTable==0) {ASSERT(0); return FALSE;}			// 발생 불가.
	pTable->Create(bitLenMin, bitLenMax);


	// 테이블 채우기
	for(symbol=0;symbol<numSymbols;symbol++)
	{
		bitLen = lengths[symbol];
		if(bitLen)
		{
			if(pTable->SetValue(symbol, bitLen, codes[symbol])==FALSE)
				return FALSE;
		}
	}

//#ifdef _DEBUG
	// 데이타가 손상된 경우 테이블이 정상적으로 만들어지지 않을 수 있으므로 에러 처리 필요.
	for(UINT i=0;i<pTable->itemCount;i++)
	{
		if(pTable->pItem[i].symbol==HUFFTABLE_VALUE_NOT_EXIST)
		{
			ASSERT(0);
			return FALSE;
		}
	}
//#endif

	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         static 허프만에 사용할 테이블 만들기
/// @param  
/// @return 
/// @date   Thursday, April 08, 2010  4:17:06 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
void XInflate::CreateStaticTable()
{
	BYTE		lengths[MAX_SYMBOLS];

	// static symbol 테이블 만들기
	// RFC1951 3.2.6
    memset(lengths, 8, 144);
    memset(lengths + 144, 9, 256 - 144);
    memset(lengths + 256, 7, 280 - 256);
    memset(lengths + 280, 8, 288 - 280);

	XINFLATE_ERR err;
	CreateTable(lengths, MAX_SYMBOLS, m_pStaticInfTable, err);

	// static dist 테이블 만들기
	// RFC1951 3.2.6
	memset(lengths, 5, 32);
	CreateTable(lengths, 32, m_pStaticDistTable, err);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///         입력 버퍼가 충분할 경우 빠른 디코딩을 수행한다.
/// @param  
/// @return 
/// @date   Thursday, May 13, 2010  1:43:34 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
XINFLATE_ERR XInflate::FastInflate(IDecodeStream* stream, LPBYTE& inBuffer, int& inBufferRemain,
		LPBYTE& outBufferCur, 
		LPBYTE& outBufferEnd,
		LPBYTE& windowStartPos, LPBYTE& windowCurPos,
		UINT&		bits, int& bitLen,
		STATE&		state,
		int& windowLen, int& windowDistCode, int& symbol)
{
	XINFLATE_ERR	ret = XINFLATE_ERR_OK;
	int				extrabits;
	int				dist;
	XFastHuffItem*	pItem;								// HUFFLOOKUP() 에서 사용
	XFastHuffItem*	pItems = m_pCurrentTable->pItem;	// HUFFLOOKUP 에서 빨리 접근할수있게 로컬 변수에 복사해놓기
	int				mask = m_pCurrentTable->mask;		// ...마찬가지.
	BYTE			byte;								// 임시 변수

	// 루프 돌면서 압축 해제
	while(inBufferRemain>FASTINFLATE_MIN_BUFFER_NEED)
	{
		FILLBUFFER_FAST();

		/////////////////////////////////////
		// 길이 가져오기
		extrabits = lencodes_extrabits[symbol - 257];

		// RFC 1951 3.2.5
		// 기본 길이에 extrabit 만큼의 비트의 내용을 더하면 진짜 길이가 나온다
		windowLen = lencodes_min[symbol - 257] + BS_EATBITS(extrabits);

		FILLBUFFER_FAST();

		
		/////////////////////////////////////
		// 거리 코드 가져오기
		HUFFLOOKUP(windowDistCode, m_pCurrentDistTable);

		//if(windowDistCode==30 || windowDistCode==31)	// 30 과 31은 생길 수 없다. RFC1951 3.2.6
		if(windowDistCode>=30)							// 이론상 32, 33 등도 생길 수 있는듯?
			DOERR(XINFLATE_ERR_INVALID_DIST);
		if(windowDistCode<0)
			DOERR(XINFLATE_ERR_INVALID_DIST);											// fast inflate 실패..

		FILLBUFFER_FAST();


		/////////////////////////////////////
		// 거리 가져오기
		
		//rec = &distcodes[windowDistCode];
		extrabits = distcodes_extrabits[windowDistCode];

		// DIST 구하기
		dist = distcodes_min[windowDistCode] + BS_EATBITS(extrabits);


		/////////////////////////////////////
		// lz77 출력
		if(outBufferEnd - outBufferCur > windowLen)
		{
			// 출력 버퍼가 충분한 경우 - 출력 버퍼가 충분한지 여부를 체크하지 않는다.
			do
			{
				byte = *WIN_GETBUF(dist);
				WRITE_FAST(byte);
			}while(--windowLen);
		}
		else
		{
			// 출력 버퍼가 충분하지 않은 경우
			do
			{
				byte = *WIN_GETBUF(dist);
				WRITE(byte);
			}while(--windowLen);
		}


		/////////////////////////////////////
		// 심볼 가져오기.
		for(;;)
		{
			// 입력 버퍼 체크
			if(!(inBufferRemain>FASTINFLATE_MIN_BUFFER_NEED)) 
			{
				state = STATE_GET_SYMBOL;
				goto END;
			}

			FILLBUFFER_FAST();

			HUFFLOOKUP_FAST(symbol);

			if(symbol<0) 
			{ASSERT(0); goto END;}					// 발생 불가.
			else if(symbol<256)
			{
				byte = (BYTE)symbol;
				WRITE(byte);
			}
			else if(symbol==256)	// END OF BLOCK
			{
				if(m_bFinalBlock)
					state = STATE_COMPLETED;
				else
					state = STATE_START;
				// 함수 종료
				goto END;
			}
			else if(symbol<286)
			{
				// 길이 가져오기로 진행한다.
				state = STATE_GET_LEN;
				break;
			}
			else
				DOERR(XINFLATE_ERR_INVALID_SYMBOL);		// 발생 불가
		}
	}

END :
	return ret;
}

