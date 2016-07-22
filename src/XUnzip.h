////////////////////////////////////////////////////////////////////////////////////////////////////
/// 
///  XUNZIP - common zip file extractor
///
/// @author   park
/// @date     Friday, May 14, 2010  12:27:28 PM
/// 
/// Copyright(C) 2010 Bandisoft, All rights reserved.
/// 
////////////////////////////////////////////////////////////////////////////////////////////////////

/*
	지원 하는 스펙
		- RAW 데이타
		- DEFLATE 압축
		- 메모리 버퍼로 압축 해제
		- 파일 핸들로 압축 해제

	지원 안하는 스펙
		- ZIP64
		- 2GB 이상의 파일
		- 파일목록에 디렉토리는 포함하지 않음
		- 뒷부분이 손상된 파일 지원 안함
		- 분할 zip 파일
		- 암호 걸린 파일
		- UTF 파일명 지원 안함
		- File comment 가 있는 파일
		- DEFLATE64, LZMA, BZIP2 PPMD, ... 등등 알고리즘
*/

#pragma once

#include "XStream.h"

// 에러 코드
enum XUNZIP_ERR
{
	XUNZIP_ERR_OK,							// 에러 없음
	XUNZIP_ERR_CANT_OPEN_FILE,				// 파일 열기 실패
	XUNZIP_ERR_CANT_READ_FILE,				// 파일 읽기 실패
	XUNZIP_ERR_BROKEN_FILE,					// 손상된 파일임 (혹은 지원하지 않는 확장 포맷의 zip 파일)
	XUNZIP_ERR_INVALID_ZIP_FILE,			// ZIP 파일이 아님
	XUNZIP_ERR_UNSUPPORTED_FORMAT,			// 지원하지 않는 파일 포맷
	XUNZIP_ERR_ALLOC_FAILED,				// 메모리 alloc 실패
	XUNZIP_ERR_INVALID_PARAM,				// 잘못된 입력 파라메터
	XUNZIP_ERR_CANT_WRITE_FILE,				// 파일 출력중 에러 발생
	XUNZIP_ERR_INFLATE_FAIL,				// inflate 에러 발생
	XUNZIP_ERR_INVALID_CRC,					// crc 에러 발생
	XUNZIP_ERR_MALLOC_FAIL,					// 메모리 alloc 실패
	XUNZIP_ERR_INSUFFICIENT_OUTBUFFER,		// 출력 버퍼가 모자람
};

// 압축 방식
enum XUNZIP_COMPRESSION_METHOD
{
	XUNZIP_COMPRESSION_METHOD_STORE		=	0,
	XUNZIP_COMPRESSION_METHOD_DEFLATE	=	8,
};

///////////////////////////////////
//
// 파일 정보 구조체
//
struct XUnzipFileInfo
{
	CHAR*	fileName;						// 파일명 
	int		compressedSize;					// 압축된 크기
	int		uncompressedSize;				// 압축 안된 크기
	UINT32	crc32;
	XUNZIP_COMPRESSION_METHOD	method;		// 압축 알고리즘
	BOOL	encrypted;						// 암호 걸렸남?
	int		offsetLocalHeader;				// local header의 옵셋
	int		offsetData;						// 압축 데이타의 옵셋
};

class XInflate;
class XUnzip
{
public :
	XUnzip();
	~XUnzip();

	BOOL					Open(LPCWSTR szPathFileName);
	BOOL					Open(BYTE* data, int dataLen);
	BOOL					Open(XReadStream* stream);
	void					Close();
	XUNZIP_ERR				GetError() const { return m_err; }
	int						GetFileCount() { return m_fileCount; }
	const XUnzipFileInfo*	GetFileInfo(int index);
	BOOL					ExtractTo(int index, HANDLE hFile);
	BOOL					ExtractTo(int index, XWriteStream* outFile);
	BOOL					ExtractTo(int index, XBuffer& buf, BOOL addNull=FALSE);
	BOOL					ExtractTo(LPCWSTR fileName, XBuffer& buf);
	int						FindIndex(LPCSTR fileName);
	int						FindIndex(LPCWSTR fileName, int codePage=CP_OEMCP);


private :
	enum SIGNATURE			// zip file signature
	{
		SIG_ERROR								= 0x00000000,
		SIG_EOF									= 0xffffffff,
		SIG_LOCAL_FILE_HEADER					= 0x04034b50,
		SIG_CENTRAL_DIRECTORY_STRUCTURE			= 0x02014b50,
		SIG_ENDOF_CENTRAL_DIRECTORY_RECORD		= 0x06054b50,
	};

	enum ZIP_FILE_ATTRIBUTE
	{
		ZIP_FILEATTR_READONLY	= 0x1,
		ZIP_FILEATTR_HIDDEN		= 0x2,
		ZIP_FILEATTR_DIRECTORY	= 0x10,
		ZIP_FILEATTR_FILE		= 0x20,			
	};

private :
	BOOL			_Open(XReadStream* pInput);
	BOOL			SearchCentralDirectory();
	BOOL			ReadCentralDirectoryStructure();
	BOOL			_ExtractToStream(int index, XWriteStream* stream);
	XUnzipFileInfo*	_GetFileInfo(int index);
	BOOL			ReadLocalHeader(XUnzipFileInfo* pFileInfo);

private :
	BOOL			FileListAdd(XUnzipFileInfo* pFileInfo);
	void			FileListClear();

private :
	BOOL			ReadUINT32(UINT32& value);
	BOOL			ReadUINT8(BYTE& value);
	BOOL			Read(void* data, int len);
	BOOL			Skip(int len);

private :
	enum {DEFAULT_FILE_LIST_SIZE = 1024 };		// 기본 파일 목록 배열 크기
	enum {INPUT_BUF_SIZE = 1024*32};			// 압축데이타 한번에 읽는 크기
	enum {INVALID_OFFSET = -1};					// 압축데이타 한번에 읽는 크기

private :
	XReadStream*		m_pInput;
	BOOL				m_bDeleteInput;			// m_pInput 를 내가 delete 할지 여부.
	XUNZIP_ERR			m_err;

	XUnzipFileInfo**	m_ppFileList;
	int					m_fileCount;
	int					m_fileListSize;
	BOOL				m_checkCRC32;

	XInflate*			m_inflate;
};
