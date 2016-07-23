////////////////////////////////////////////////////////////////////////////////////////////////////
/// 
/// XINFLATE - DEFLATE 알고리즘의 압축 해제(inflate) 클래스
///
/// - 버전 : v 2.0 (2016/7/21)
///
/// - 라이선스 : zlib license (https://kippler.com/etc/zlib_license/ 참고)
///
/// - 참고 자료
///  . RFC 1951 (http://www.http-compression.com/rfc1951.pdf)
///  . Halibut(http://www.chiark.greenend.org.uk/~sgtatham/halibut/)의 deflate.c 를 일부 참고하였음
/// 
/// @author   kippler@gmail.com
/// @date     Monday, April 05, 2010  5:59:34 PM
/// 
/// Copyright(C) 2010-2016 Bandisoft, All rights reserved.
/// 
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <windows.h>


// 에러 코드
enum XINFLATE_ERR
{
	XINFLATE_ERR_OK,						// 성공
	XINFLATE_ERR_STREAM_NOT_COMPLETED,		// 스트림이 깔끔하게 끝나지 않았다.
	XINFLATE_ERR_HEADER,					// 헤더에 문제 있음
	XINFLATE_ERR_INVALID_NOCOMP_LEN,		// no compressio 헤더에 문제 있음
	XINFLATE_ERR_ALLOC_FAIL,				// 메모리 ALLOC 실패
	XINFLATE_ERR_INVALID_DIST,				// DIST 정보에서 에러 발생
	XINFLATE_ERR_INVALID_SYMBOL,			// SYMBOL 정보에서 에러 발생
	XINFLATE_ERR_BLOCK_COMPLETED,			// 압축 해제가 이미 완료되었음.
	XINFLATE_ERR_CREATE_CODES,				// code 생성중 에러 발생
	XINFLATE_ERR_CREATE_TABLE,				// table 생성중 에러 발생
	XINFLATE_ERR_INVALID_LEN,				// LEN 정보에 문제 있음
	XINFLATE_ERR_INSUFFICIENT_OUT_BUFFER,	// 외부 출력 버퍼가 모자라다.
	XINFLATE_ERR_USER_STOP,					// 사용자 취소
	XINFLATE_ERR_INTERNAL,
};


struct IDecodeStream
{
	virtual int		Read(BYTE* buf, int len) = 0;
	virtual BOOL	Write(BYTE* buf, int len) = 0;
};


//#ifdef _WIN64
//typedef UINT64 BITSTREAM;
//#define BITSLEN2FILL	(7*8)			// 3bytes*8
//#else
typedef UINT BITSTREAM;
#define BITSLEN2FILL	24				// 3bytes*8
//#endif



class XFastHuffTable;
class XInflate
{
public :
	XInflate();
	~XInflate();
	void					Free();													// 내부에 사용된 모든 객체를 해제한다. 더이상 이 클래스를 사용하지 않을때 호출하면 된다.
	XINFLATE_ERR			Inflate(IDecodeStream* stream);							// 압축해제 작업. 큰 메모리 블럭 압축 해제시 여러번 계속 호출하면 된다.

private :
	XINFLATE_ERR			Init();													// 초기화: 매번 압축 해제를 시작할때마다 호출해 줘야 한다.
	void					CreateStaticTable();
	BOOL					CreateCodes(BYTE* lengths, int numSymbols, int* codes);
	BOOL					CreateTable(BYTE* lengths, int numSymbols, XFastHuffTable*& pTable, XINFLATE_ERR& err);
	BOOL					_CreateTable(int* codes, BYTE* lengths, int numSymbols, XFastHuffTable*& pTable);

private :
	enum	STATE						// 내부 상태
	{
		STATE_START,

		STATE_NO_COMPRESSION,
		STATE_NO_COMPRESSION_LEN,
		STATE_NO_COMPRESSION_NLEN,
		STATE_NO_COMPRESSION_BYPASS,

		STATE_FIXED_HUFFMAN,

		STATE_GET_SYMBOL_ONLY,
		STATE_GET_LEN,
		STATE_GET_DISTCODE,
		STATE_GET_DIST,
		STATE_GET_SYMBOL,

		STATE_DYNAMIC_HUFFMAN,
		STATE_DYNAMIC_HUFFMAN_LENLEN,
		STATE_DYNAMIC_HUFFMAN_LEN,
		STATE_DYNAMIC_HUFFMAN_LENREP,

		STATE_COMPLETED,
	};


private :
	XINFLATE_ERR FastInflate(IDecodeStream* stream, LPBYTE& inBuffer, int& inBufferRemain,
		LPBYTE& outBufferCur,
		LPBYTE& outBufferEnd,
		LPBYTE& windowStartPos, 
		LPBYTE& windowCurPos,
		BITSTREAM& bits,
		int& bitLen,
		STATE& state,
		int& windowLen, int& windowDistCode, int& symbol);


private :
	XFastHuffTable*			m_pStaticInfTable;		// static huffman table
	XFastHuffTable*			m_pStaticDistTable;		// static huffman table (dist)
	XFastHuffTable*			m_pDynamicInfTable;		// 
	XFastHuffTable*			m_pDynamicDistTable;	// 
	XFastHuffTable*			m_pCurrentTable;		// 
	XFastHuffTable*			m_pCurrentDistTable;	// 

	BYTE*					m_dicPlusOutBuffer;		// 사전 + 출력 버퍼
	BYTE*					m_outBuffer;			// 출력 시작 위치
	BYTE*					m_inBuffer;				// 입력데이타를 저장할 버퍼

	BOOL					m_bFinalBlock;			// 현재 마지막 블럭을 처리중인가?

	int						m_literalsNum;			// dynamic huffman - # of literal/length codes   (267~286)
	int						m_distancesNum;			// "               - # of distance codes         (1~32)
	int						m_lenghtsNum;			//                 - # of code length codes      (4~19)
	BYTE					m_lengths[286+32];		// literal + distance 의 length 가 같이 들어간다.
	int						m_lenghtsPtr;			// m_lengths 에서 현재 위치
	int						m_lenExtraBits;
	int						m_lenAddOn;
	int						m_lenRep;
	XFastHuffTable*			m_pLenLenTable;
	const char*				m_copyright;

private :
	enum { DEFLATE_WINDOW_SIZE = 32768 };			// RFC 에 정의된 WINDOW 크기
	enum { DEFAULT_OUTBUF_SIZE = 1024 * 1024 };		// 기본 출력 버퍼 크기
	enum { DEFAULT_INBUF_SIZE = 1024*128 };			// 기본 입력 버퍼 크기
	enum { MAX_SYMBOLS = 288 };						// deflate 에 정의된 심볼 수
	enum { MAX_CODELEN = 16 };						// deflate 허프만 트리 최대 코드 길이
	enum { FASTINFLATE_MIN_BUFFER_NEED = 6 };		// FastInflate() 호출시 필요한 최소 입력 버퍼수

#ifdef _DEBUG
	int						m_inputCount;
	int						m_outputCount;
#endif

};

