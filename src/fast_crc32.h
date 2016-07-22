/* crc32.c -- compute the CRC-32 of a data stream
* Copyright (C) 1995-2006, 2010, 2011, 2012 Mark Adler
* For conditions of distribution and use, see copyright notice in zlib.h
*
* Thanks to Rodney Brown <rbrown64@csc.com.au> for his contribution of faster
* CRC methods: exclusive-oring 32 bits of data at a time, and pre-computing
* tables for updating the shift register in one step with three exclusive-ors
* instead of four steps with four exclusive-ors.  This results in about a
* factor of two increase in speed on a Power PC G4 (PPC7455) using gcc -O3.
*/

#pragma once
UINT32	fast_crc32(UINT32 crc, const BYTE *buf, UINT len);

