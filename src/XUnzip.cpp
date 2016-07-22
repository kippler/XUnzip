#include "stdafx.h"
#include "XUnzip.h"
#include "XInflate.h"
#include "fast_crc32.h"

/**********************************************************************************

                                    -= history =-

* 2010/05/14
	- 작업 시작. 원래 목표한대로 대충 구현 완료

* 2010/05/19
	- 다이렉트 메모리 압축 해제 구현 완료

* 2010/05/24
	- 메모리 버퍼 입력기능 추가

* 2010/05/25
	- v 1.0 공개

* 2010/06/08
	- 경로명의 / 를 \ 로 바꾸도록 기능 추가

* 2010/12/31
	- extra field 관련 버그 수정

* 2015/10/23
	- 입/출력을 XStream 을 사용하고, XUnzip2 로 이름 바꿈

* 2016/7/22
	- XInflate 2.0 적용

***********************************************************************************/

#define DOERR(err) {m_err = err; ASSERT(0); return FALSE;}
#define DOERR2(err, ret) {m_err = err; ASSERT(0); return ret;}
#define DOFAIL(err) {m_err = err; ASSERT(0); goto END;}


static LPSTR Unicode2Ascii(LPCWSTR szInput, int nCodePage)
{
	LPSTR	ret=NULL;
	int		wlen = (int)(wcslen(szInput)+1)*3;
	int		asciilen;
	asciilen = wlen;					// asciilen 은 wlen 보다 크지 않다.
	ret = (LPSTR)malloc(asciilen);		// ASCII 용 버퍼
	if(ret==NULL) {ASSERT(0); return NULL;}
	if(WideCharToMultiByte(nCodePage, 0, szInput,  -1, ret, asciilen, NULL, NULL)==0)	// UCS2->ascii
	{ASSERT(0); free(ret); return NULL;}
	return ret;
}


////////////////////////////////////////////////////////////////////////////
//
//    ZIP 포맷 구조체 정의
//

#pragma pack(1)
struct SEndOfCentralDirectoryRecord
{
	SHORT	numberOfThisDisk;
	SHORT	numberOfTheDiskWithTheStartOfTheCentralDirectory;
	SHORT	centralDirectoryOnThisDisk;
	SHORT	totalNumberOfEntriesInTheCentralDirectoryOnThisDisk;
	DWORD	sizeOfTheCentralDirectory;
	DWORD	offsetOfStartOfCentralDirectoryWithREspectoTotheStartingDiskNumber;
	SHORT	zipFileCommentLength;
};

union _UGeneralPurposeBitFlag
{
	SHORT	data;
	struct 
	{
		BYTE bit0 : 1;	// If set, indicates that the file is encrypted.
		BYTE bit1 : 1;
		BYTE bit2 : 1;
		BYTE bit3 : 1;	// If this bit is set, the fields crc-32, compressed  
						// size and uncompressed size are set to zero in the  
						// local header.  The correct values are put in the   
						// data descriptor immediately following the compressed data. 
		BYTE bit4 : 1;			  
		BYTE bit5 : 1;			   
		BYTE bit6 : 1;
		BYTE bit7 : 1;			   
		BYTE bit8 : 1;
		BYTE bit9 : 1;			   
		BYTE bit10 : 1;			   
		BYTE bit11 : 1;			   
	};
};

struct SLocalFileHeader
{
	SHORT	versionNeededToExtract;
	_UGeneralPurposeBitFlag	generalPurposeBitFlag;
	SHORT	compressionMethod;
	DWORD	dostime;		// lastModFileTime + lastModFileDate*0xffff
	DWORD	crc32;
	DWORD	compressedSize;
	DWORD	uncompressedSize;
	SHORT	fileNameLength;
	SHORT	extraFieldLength;
};

struct SCentralDirectoryStructureHead
{
	SHORT	versionMadeBy;
	SHORT	versionNeededToExtract;
	_UGeneralPurposeBitFlag	generalPurposeBitFlag;
	SHORT	compressionMethod;
	UINT	dostime;
	DWORD	crc32;
	DWORD	compressedSize;
	DWORD	uncompressedSize;
	SHORT	fileNameLength;
	SHORT	extraFieldLength;
	SHORT	fileCommentLength;
	SHORT	diskNumberStart;
	SHORT	internalFileAttributes;
	DWORD	externalFileAttributes;
	DWORD	relativeOffsetOfLocalHeader;
};

#pragma pack()


////////////////////////////////////////////////////////////////////////////
//
//       XUNZIP
//

XUnzip::XUnzip()
{
	m_pInput = NULL;
	m_bDeleteInput = FALSE;
	m_ppFileList = NULL;
	m_fileCount = 0;
	m_fileListSize = 0;
	m_inflate = NULL;
	m_err = XUNZIP_ERR_OK;
}


XUnzip::~XUnzip()
{
	Close();

	if(m_inflate)
		delete m_inflate;

}

void XUnzip::Close()
{
	if(m_pInput && m_bDeleteInput)
	{
		delete m_pInput;
		m_pInput = NULL;
	}
	m_bDeleteInput = FALSE;

	FileListClear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         파일 열기
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  1:00:26 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::Open(LPCWSTR szPathFileName)
{
	Close();

	XFileReadStream* pInput = new XFileReadStream();
	if(pInput->Open(szPathFileName)==FALSE)
	{
		delete pInput;
		DOERR(XUNZIP_ERR_CANT_OPEN_FILE);
	}
	m_bDeleteInput = TRUE;
	return _Open(pInput);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         메모리 버퍼로 데이타 열기 - 메모리 버퍼는 Close() 할때까지 유효해야 한다.
/// @param  
/// @return 
/// @date   Monday, May 24, 2010  12:56:12 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::Open(BYTE* data, int dataLen)
{
	Close();

	XMemoryReadStream* pInput = new XMemoryReadStream();
	pInput->Attach(data, dataLen);
	m_bDeleteInput = TRUE;
	return _Open(pInput);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///         stream의 delete 는 xunzip 이 안한다!
/// @param  
/// @return 
/// @date   Tuesday, February 24, 2015  5:09:29 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::Open(XReadStream* stream)
{
	return _Open(stream);
}

BOOL XUnzip::_Open(XReadStream* pInput)
{
	m_pInput = pInput;
	SIGNATURE	sig;

	// 파일 헤더 확인하기
	if(ReadUINT32((UINT32&)(sig))==FALSE)
		DOERR(XUNZIP_ERR_CANT_OPEN_FILE);
	if(sig!=SIG_LOCAL_FILE_HEADER)
		DOERR(XUNZIP_ERR_INVALID_ZIP_FILE);

	// 파일 정보 위치 찾기
	if(SearchCentralDirectory()==FALSE)
		DOERR(XUNZIP_ERR_BROKEN_FILE);

	// 파일 정보 읽기
	for(;;)
	{
		// signature 읽기
		if(ReadUINT32((UINT32&)(sig))==FALSE)
			DOERR(XUNZIP_ERR_CANT_OPEN_FILE);

		if(sig==SIG_CENTRAL_DIRECTORY_STRUCTURE)
		{
			if(ReadCentralDirectoryStructure()==FALSE)
				return FALSE;
		}
		else if(sig==SIG_ENDOF_CENTRAL_DIRECTORY_RECORD)
		{
			// 정상 종료하기
			break;
		}
		else
		{
			DOERR(XUNZIP_ERR_BROKEN_FILE);
		}
	}
	return TRUE;
}

BOOL XUnzip::ReadUINT32(UINT32& value)
{
	return m_pInput->Read((BYTE*)&value, sizeof(UINT32));
}

BOOL XUnzip::ReadUINT8(BYTE& value)
{
	return m_pInput->Read((BYTE*)&value, sizeof(BYTE));
}

BOOL XUnzip::Read(void* data, int len)
{
	return m_pInput->Read((BYTE*)data, len);
}

BOOL XUnzip::Skip(int len)
{
	if(len==0) return TRUE;
	INT64 newPos = m_pInput->GetPos() + len;
	return (m_pInput->SetPos(newPos)==newPos) ? TRUE : FALSE;	// 항상 TRUE 이다. -.-;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         센트럴 디렉토리 찾기 - zip comment 가 있을경우 처리하지 못한다.
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  1:27:26 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::SearchCentralDirectory()
{
	// end of central directory record 읽기
	int size = sizeof(SEndOfCentralDirectoryRecord) + sizeof(UINT32);
	m_pInput->SetPos(-(int)(sizeof(SEndOfCentralDirectoryRecord) + sizeof(UINT32)), FILE_END);

	// end of central directory record 정보 읽기
	SIGNATURE	sig;
	if(ReadUINT32((UINT32&)(sig))==FALSE)
		DOERR(XUNZIP_ERR_CANT_OPEN_FILE);
	if(sig!=SIG_ENDOF_CENTRAL_DIRECTORY_RECORD)
		DOERR(XUNZIP_ERR_INVALID_ZIP_FILE);

	SEndOfCentralDirectoryRecord	rec;

	if(Read(&rec, sizeof(rec))==FALSE)
		DOERR(XUNZIP_ERR_CANT_READ_FILE);

	// central directory 위치를 찾았다!
	m_pInput->SetPos(rec.offsetOfStartOfCentralDirectoryWithREspectoTotheStartingDiskNumber, FILE_BEGIN);

	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         파일 정보 읽기
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  2:24:39 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::ReadCentralDirectoryStructure()
{
	CHAR*				fileName=NULL;
	int					fileOffset=0;
	BOOL				ret = FALSE;
	XUnzipFileInfo*		pFileInfo = NULL;
	SCentralDirectoryStructureHead head;
	
	// 정보 읽고
	if(Read(&head, sizeof(head))==FALSE)
		DOERR(XUNZIP_ERR_BROKEN_FILE);

	// 지원하는 포맷인가?
	if(	head.compressionMethod!=XUNZIP_COMPRESSION_METHOD_STORE &&
		head.compressionMethod!=XUNZIP_COMPRESSION_METHOD_DEFLATE)
		DOERR(XUNZIP_ERR_UNSUPPORTED_FORMAT);

	// 암호는 지원하지 않는다
	if(head.generalPurposeBitFlag.bit0)
		DOERR(XUNZIP_ERR_UNSUPPORTED_FORMAT);


	// read file name
	if(head.fileNameLength)
	{
		fileName = (char*)malloc(head.fileNameLength+sizeof(char));
		if(fileName==NULL)
			DOERR(XUNZIP_ERR_BROKEN_FILE);

		if(Read(fileName,  head.fileNameLength)==FALSE)
			DOFAIL(XUNZIP_ERR_BROKEN_FILE);

		fileName[head.fileNameLength] = NULL;
	}

	// extra field;
	if(Skip(head.extraFieldLength)==FALSE)
		DOFAIL(XUNZIP_ERR_BROKEN_FILE);

	// file comment;
	if(Skip(head.fileCommentLength)==FALSE)
		DOFAIL(XUNZIP_ERR_BROKEN_FILE);

	// 디렉토리는 목록에 추가하지 않는다 (귀찮아서)
	if(head.externalFileAttributes & ZIP_FILEATTR_DIRECTORY)
	{ret = TRUE; goto END;}

	// 파일 목록에 추가하기.
	pFileInfo = new XUnzipFileInfo;

	pFileInfo->fileName = fileName;									// 파일명
	pFileInfo->compressedSize = head.compressedSize;
	pFileInfo->uncompressedSize = head.uncompressedSize;
	pFileInfo->crc32 = head.crc32;
	pFileInfo->method = (XUNZIP_COMPRESSION_METHOD)head.compressionMethod;
	pFileInfo->encrypted = head.generalPurposeBitFlag.bit0 ? TRUE : FALSE;
	pFileInfo->offsetLocalHeader = head.relativeOffsetOfLocalHeader;
	pFileInfo->offsetData = INVALID_OFFSET;							// 실제 압축 데이타 위치 - 나중에 계산한다.

	if(FileListAdd(pFileInfo)==FALSE)
		goto END;

	// 정리
	pFileInfo = NULL;
	fileName = NULL;
	ret = TRUE;

END :
	if(fileName) free(fileName);
	if(pFileInfo) delete pFileInfo;
	return ret;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///         파일 목록에 추가하기
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  3:13:53 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::FileListAdd(XUnzipFileInfo* pFileInfo)
{
	// 파일명의 '/' 를 \ 로 바꾸기
	char* p = pFileInfo->fileName;
	while(*p)
	{
		if(*p=='/') *p='\\';
		p++;
	}

	// 최초 호출?
	if(m_ppFileList==NULL)
	{
		m_fileListSize = DEFAULT_FILE_LIST_SIZE;
		m_ppFileList = (XUnzipFileInfo**)malloc (m_fileListSize * sizeof(void*));
		if(m_ppFileList==NULL)
			DOERR(XUNZIP_ERR_ALLOC_FAILED);
	}

	// 꽉찼나?
	if(m_fileCount>=m_fileListSize)
	{
		m_fileListSize = m_fileListSize*2;		// 두배로 잡기
		XUnzipFileInfo** temp;
		temp = (XUnzipFileInfo**)realloc(m_ppFileList, (m_fileListSize * sizeof(void*) ));
		if(temp==NULL)
			DOERR(XUNZIP_ERR_ALLOC_FAILED);

		m_ppFileList = temp;
	}

	// 배열에 추가
	m_ppFileList[m_fileCount] = pFileInfo;
	m_fileCount++;

	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         파일 리스트 지우기
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  3:17:02 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
void XUnzip::FileListClear()
{
	if(m_ppFileList==NULL) return;

	int	i;
	for(i=0;i<m_fileCount;i++)
	{
		XUnzipFileInfo* pFileInfo = *(m_ppFileList + i);
		//ASSERT(pFileInfo);
		free(pFileInfo->fileName);
		delete pFileInfo;
	}

	free(m_ppFileList);
	m_ppFileList = NULL;
	m_fileCount = 0;
	m_fileListSize =0 ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
///         파일 정보 리턴하기
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  3:51:17 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
const XUnzipFileInfo* XUnzip::GetFileInfo(int index)
{
	// 파라메터 검사
	if(m_ppFileList==NULL ||
		index<0 || index >=m_fileCount)
		DOERR2(XUNZIP_ERR_INVALID_PARAM, NULL);

	return m_ppFileList[index];
}
XUnzipFileInfo* XUnzip::_GetFileInfo(int index)
{
	// 파라메터 검사
	if(m_ppFileList==NULL ||
		index<0 || index >=m_fileCount)
		DOERR2(XUNZIP_ERR_INVALID_PARAM, NULL);

	return m_ppFileList[index];
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         파일 핸들로 압축 풀기
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  3:54:47 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::ExtractTo(int index, HANDLE hFile)
{
	XFileWriteStream output;
	output.Attach(hFile);

	return _ExtractToStream(index, &output);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         버퍼 alloc 후 압축 풀기
/// @param  
/// @return 
/// @date   Monday, November 05, 2012  4:36:19 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::ExtractTo(int index, XBuffer& buf, BOOL addNull)
{
	XUnzipFileInfo* info = _GetFileInfo(index);
	if(info==NULL) DOERR2(XUNZIP_ERR_INVALID_PARAM, FALSE);
	if(buf.Alloc(info->uncompressedSize + (addNull?2:0))==FALSE)
		DOERR(XUNZIP_ERR_MALLOC_FAIL);

	if(addNull)				// 문자열 파싱시 처리를 쉽게하기 위해서 끝에 null 하나 넣어주기.
	{
		buf.data[buf.allocSize -1] = 0;
		buf.data[buf.allocSize -2] = 0;
	}
	buf.dataSize = info->uncompressedSize;	// 크기 지정

	XMemoryWriteStream stream;
	stream.Attach(buf.data, buf.dataSize);

	return _ExtractToStream(index, &stream);
}

BOOL XUnzip::ExtractTo(LPCWSTR fileName, XBuffer& buf)
{
	return ExtractTo(FindIndex(fileName), buf);
}

int	XUnzip::FindIndex(LPCSTR fileName)
{
	for(int i=0;i<m_fileCount;i++)
	{
		if(_stricmp(fileName, m_ppFileList[i]->fileName)==0)
			return i;
	}
	ASSERT(0); return -1;
}

int	XUnzip::FindIndex(LPCWSTR fileName, int codePage)
{
	int index = -1;
	char* fileNameA = Unicode2Ascii(fileName, codePage);
	if(fileNameA==NULL)
		DOERR2(XUNZIP_ERR_INVALID_PARAM, -1);
	index = FindIndex(fileNameA);
	free(fileNameA);
	return index;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         로컬 헤더 정보 읽고 압축 데이타 위치 파악하기
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  5:07:36 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::ReadLocalHeader(XUnzipFileInfo* pFileInfo)
{
	SIGNATURE	sig;
	if(ReadUINT32((UINT32&)(sig))==FALSE)
		DOERR(XUNZIP_ERR_CANT_READ_FILE);

	// SIGNATURE 체크
	if(sig!=SIG_LOCAL_FILE_HEADER)
		DOERR(XUNZIP_ERR_BROKEN_FILE);

	SLocalFileHeader	head;

	// 헤더 읽고
	if(Read(&head, sizeof(head))==FALSE)
		DOERR(XUNZIP_ERR_CANT_READ_FILE);

	// 파일명
	if(Skip(head.fileNameLength)==FALSE)
		DOERR(XUNZIP_ERR_CANT_READ_FILE);

	// extraFieldLength
	if(Skip(head.extraFieldLength)==FALSE)
		DOERR(XUNZIP_ERR_CANT_READ_FILE);


	// 압축데이타 위치 파악 완료
	pFileInfo->offsetData = (int)m_pInput->GetPos();

	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///         압축 풀기 
/// @param  
/// @return 
/// @date   Friday, May 14, 2010  4:09:31 PM
////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL XUnzip::_ExtractToStream(int index, XWriteStream* output)
{
	XUnzipFileInfo* pFileInfo = _GetFileInfo(index);
	if(pFileInfo==NULL) return FALSE;


	// 데이타 offset 정보가 잘못된 경우
	if(pFileInfo->offsetData==INVALID_OFFSET)
	{
		// 헤더 위치에 위치 잡고
		if(m_pInput->SetPos(pFileInfo->offsetLocalHeader, FILE_BEGIN)!=pFileInfo->offsetLocalHeader)
			DOERR(XUNZIP_ERR_CANT_READ_FILE);

		// data 위치를 찾아낸다.
		if(ReadLocalHeader(pFileInfo)==FALSE)
			return FALSE;
	}


	// 위치 잡고
	if(m_pInput->SetPos(pFileInfo->offsetData, FILE_BEGIN)==FALSE)
		DOERR(XUNZIP_ERR_CANT_READ_FILE);


	BYTE*	bufIn;
	int		inputRemain;
	int		outputRemain;
	int		toRead;
	DWORD	crc32=0;
	BOOL	ret=FALSE;

	bufIn = (BYTE*)malloc(INPUT_BUF_SIZE);

	inputRemain = pFileInfo->compressedSize;
	outputRemain = pFileInfo->uncompressedSize;

	// 압축 풀기
	if(pFileInfo->method==XUNZIP_COMPRESSION_METHOD_STORE)
	{
		while(inputRemain)
		{
			toRead = min(INPUT_BUF_SIZE, inputRemain);

			// 읽어서
			if(m_pInput->Read(bufIn, toRead)==FALSE)
				DOFAIL(XUNZIP_ERR_CANT_READ_FILE);

			// 그냥 쓴다
			if(output->Write(bufIn, toRead)==FALSE)
				DOFAIL(XUNZIP_ERR_CANT_WRITE_FILE);

			// crc
			crc32 = fast_crc32(crc32, bufIn, toRead);

			inputRemain -= toRead;
			outputRemain -= toRead;
		}
	}
	else if(pFileInfo->method==XUNZIP_COMPRESSION_METHOD_DEFLATE)
	{
		/////////////////////////////////////
		// deflate data 처리
		class MyStream : public IDecodeStream
		{
		public:
			virtual int		Read(BYTE* buf, int len)
			{
				DWORD read = 0;
				len = (int)min(m_inputRemain, len);
				m_input->Read(buf, len, &read);
				m_inputRemain -= read;
				return read;
			}
			virtual BOOL	Write(BYTE* buf, int len)
			{
				m_crc32 = fast_crc32(m_crc32, buf, len);
				m_outputRemain -= len;
				return m_output->Write(buf, len);
			}
			XReadStream*	m_input;
			XWriteStream*	m_output;
			INT64			m_inputRemain;
			INT64			m_outputRemain;
			DWORD			m_crc32;
		};

		// 처음 호출인가?
		if(m_inflate==NULL)
			m_inflate = new XInflate;

		// 초기화
		MyStream mystream;
		mystream.m_input = m_pInput;
		mystream.m_inputRemain = inputRemain;
		mystream.m_output = output;
		mystream.m_outputRemain = outputRemain;
		mystream.m_crc32 = 0;

		// 압축 풀고
		XINFLATE_ERR errInflate = m_inflate->Inflate(&mystream);
		if (errInflate != XINFLATE_ERR_OK)
			DOFAIL(XUNZIP_ERR_INFLATE_FAIL);

		crc32 = mystream.m_crc32;
		outputRemain = (int)mystream.m_outputRemain;

		// 정상적으로 압축이 풀렸는지 그냥 확인용.
		if ((mystream.m_inputRemain || outputRemain))
			ASSERT(0);
	}
	else
		ASSERT(0);

	// crc 검사
	if(crc32!=pFileInfo->crc32)
		DOFAIL(XUNZIP_ERR_INVALID_CRC);				// crc 에러 발생

	// 완료되었음
	if(outputRemain!=0) 
		DOFAIL(XUNZIP_ERR_INFLATE_FAIL);			// 발생 불가.

	// 성공
	ret = TRUE;

END :
	if(bufIn)
		free(bufIn);

	return ret;
}

