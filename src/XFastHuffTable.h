////////////////////////////////////////////////////////////////////////////////////////////////////
/// 
/// fast huff table 분리
/// 
/// @author   parkkh
/// @date     Sun Jul 24 02:03:46 2016
/// 
/// Copyright(C) 2016 Bandisoft, All rights reserved.
/// 
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once


/////////////////////////////////////////////////////
//
// 허프만 트리를 빨리 탐색할 수 있게 하기 위해서
// 트리 전체를 하나의 배열로 만들어 버린다.
//   

#define HUFFTABLE_VALUE_NOT_EXIST	-1

struct XFastHuffItem									// XFastHuffTable 의 아이템
{
	// 깨진 데이타에서 버퍼 오버플로우를 막기 위해서 항상 초기화를 해줘야 한다 ㅠ.ㅠ
	XFastHuffItem()
	{
		bitLen = 0;
		symbol = HUFFTABLE_VALUE_NOT_EXIST;
	}
	SHORT	bitLen;										// 유효한 비트수
	SHORT	symbol;										// 유효한 심볼
};



class XFastHuffTable
{
public:
	XFastHuffTable()
	{
		pItems = NULL;
	}
	~XFastHuffTable()
	{
		if (pItems) { delete[] pItems; pItems = NULL; }
	}
	// 배열 생성
	void	Create(int _bitLenMin, int _bitLenMax)
	{
		if (pItems) ASSERT(0);							// 발생 불가

		bitLenMin = _bitLenMin;
		bitLenMax = _bitLenMax;
		mask = (1 << bitLenMax) - 1;					// 마스크
		itemCount = 1 << bitLenMax;						// 조합 가능한 최대 테이블 크기
		pItems = new XFastHuffItem[itemCount];		// 2^maxBitLen 만큼 배열을 만든다.
	}
	// 심볼 데이타를 가지고 전체 배열을 채운다.
	__forceinline BOOL SetValue(int symbol, int bitLen, UINT bitCode)
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
		if (bitCode >= ((UINT)1 << bitLenMax))
		{
			ASSERT(0); return FALSE;
		}			// bitLenmax 가 3 이라면.. 111 까지만 가능하다


		UINT revBitCode = 0;
		// 뒤집기
		int i;
		for (i = 0; i<bitLen; i++)
		{
			revBitCode <<= 1;
			revBitCode |= (bitCode & 0x01);
			bitCode >>= 1;
		}

		int		add2code = (1 << bitLen);		// bitLen 이 3 이라면 add2code 에는 1000(bin) 이 들어간다

												// 배열 채우기
		for (;;)
		{
#ifdef _DEBUG
			if (revBitCode >= itemCount) ASSERT(0);

			if (pItems[revBitCode].symbol != HUFFTABLE_VALUE_NOT_EXIST)
			{
				ASSERT(0); return FALSE;
			}
#endif
			pItems[revBitCode].bitLen = bitLen;
			pItems[revBitCode].symbol = symbol;

			// 조합 가능한 bit code 를 처리하기 위해서 값을 계속 더한다.
			revBitCode += add2code;

			// 조합 가능한 범위가 벗어난 경우 끝낸다
			if (revBitCode >= itemCount)
				break;
		}
		return TRUE;
	}

	__forceinline int HUFFLOOKUP(int& bitLen, BITSTREAM& bits)
	{
		/*
		// 데이타 부족 											
		if (this->bitLenMin > bitLen)
		{
			//ASSERT(0);
			return -1;
		}
		else
		*/
		{
			XFastHuffItem* pItem = &(this->pItems[this->mask & bits]);
			// 데이타 부족 
			if (pItem->bitLen > bitLen)
			{
				//ASSERT(0);
				return -1;
			}
			else
			{
				//BS_REMOVEBITS(pItem->bitLen);
				bits >>= (pItem->bitLen);
				bitLen -= (pItem->bitLen);
				ASSERT(!(bitLen < 0));
				return pItem->symbol;
			}
		}
	}


	XFastHuffItem*	pItems;							// (huff) code 2 symbol 아이템, code가 배열의 위치 정보가 된다.
	int				bitLenMin;						// 유효한 최소 비트수
	int				bitLenMax;						// 유효한 최대 비트수
	UINT			mask;							// bitLenMax 에 대한 bit mask
	UINT			itemCount;						// bitLenMax 로 생성 가능한 최대 아이템 숫자
};


inline int HUFFLOOKUP_FAST(XFastHuffItem* pItems, int mask, int& bitLen, BITSTREAM& bits)
{
	XFastHuffItem* pItem = &(pItems[mask & bits]);
	//BS_REMOVEBITS(pItem->bitLen);
	bits >>= (pItem->bitLen);
	bitLen -= (pItem->bitLen);
	return pItem->symbol;
}

