#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wmultichar"

#include <stdio.h>
#include <3ds.h>
#include "ext_svc.h"

#define CURRENT_PROCESS_HANDLE (0xffff8001)

#define CONVERT_ENDIAN(n) (((n) >> 24 & 0xFF) | ((n) >> 8 & 0xFF00) | (((n) & 0xFF00) << 8) | (((n) & 0xFF) << 24))
const u32 BinFileSigFONT = CONVERT_ENDIAN('CFNT');
const u32 BinRuntimeSigFONT = CONVERT_ENDIAN('CFNU');
const u32 BinBlockSigFINF = CONVERT_ENDIAN('FINF');

bool g_bNew3DS = false;
u32 g_uAddress = 0x18000000;

typedef enum SharedFontType
{
	SHARED_FONT_TYPE_NULL,
	SHARED_FONT_TYPE_STD,
	SHARED_FONT_TYPE_CN,
	SHARED_FONT_TYPE_KR,
	SHARED_FONT_TYPE_TW
} SharedFontType;

u32 g_uSharedFontType = SHARED_FONT_TYPE_NULL;

typedef struct BinaryFileHeader
{
	u32 Signature;
	u16 ByteOrder;
	u16 HeaderSize;
	u32 Version;
	u32 FileSize;
	u16 DataBlocks;
	u16 Reserved;
} __attribute__((packed)) BinaryFileHeader;

typedef struct BinaryBlockHeader
{
	u32 Kind;
	u32 Size;
} __attribute__((packed)) BinaryBlockHeader;

typedef struct CharWidths
{
	s8 Left;
	u8 GlyphWidth;
	s8 CharWidth;
} __attribute__((packed)) CharWidths;

typedef struct FontInformation
{
	u8 FontType;
	s8 Linefeed;
	u16 AlterCharIndex;
	CharWidths DefaultWidth;
	u8 Encoding;
	u32 PtrGlyph;
	u32 PtrWidth;
	u32 PtrMap;
	u8 Height;
	u8 Width;
	u8 Ascent;
	u8 Reserved;
} __attribute__((packed)) FontInformation;

typedef struct FontTextureGlyph
{
	u8 CellWidth;
	u8 CellHeight;
	s8 BaselinePos;
	u8 MaxCharWidth;
	u32 SheetSize;
	u16 SheetNum;
	u16 SheetFormat;
	u16 SheetRow;
	u16 SheetLine;
	u16 SheetWidth;
	u16 SheetHeight;
	u32 SheetImage;
} __attribute__((packed)) FontTextureGlyph;

typedef struct FontCodeMap
{
	u16 CodeBegin;
	u16 CodeEnd;
	u16 MappingMethod;
	u16 Reserved;
	u32 PtrNext;
} __attribute__((packed)) FontCodeMap;


#define ENABLE_GSPWN

#define GSP_HEAP_START_OFFSET_DEFAULT 0x14000000
#define GSP_HEAP_START_OFFSET_FIRM80 0x30000000
#define GSP_HEAP_MAX_LENGTH (g_bNew3DS ? 0xDC00000 : 0x6800000)
#define GSP_HEAP_END_OFFSET_DEFAULT (GSP_HEAP_START_OFFSET_DEFAULT + GSP_HEAP_MAX_LENGTH)
#define GSP_HEAP_END_OFFSET_FIRM80 (GSP_HEAP_START_OFFSET_FIRM80 + GSP_HEAP_MAX_LENGTH)

#define BUFFER_LENGTH 0x1000

static inline bool IsMemoryAddressWithinGSP(u32 address)
{
	return (address >= GSP_HEAP_START_OFFSET_DEFAULT && address < GSP_HEAP_END_OFFSET_DEFAULT) || 
	(address >= GSP_HEAP_START_OFFSET_FIRM80 && address < GSP_HEAP_END_OFFSET_FIRM80);
}

Result DoGsPwn(void * dest, void * src, u32 size)
{
	return GX_TextureCopy(src, 0xFFFFFFFF, dest, 0xFFFFFFFF, size, 8);
}

Result MemoryCopy(void * dest, void * src, u32 size)
{
	bool isDestWithinGSP = IsMemoryAddressWithinGSP((u32)dest);
	bool isSrcWithinGSP = IsMemoryAddressWithinGSP((u32)src);
	if (isDestWithinGSP && !isSrcWithinGSP)
	{
		memcpy(dest, src, size);
		return 0;
	}
	bool eitherWithoutGSP = !isDestWithinGSP || !isSrcWithinGSP;
	if (!eitherWithoutGSP)
	{
		s32 remain = (s32)size;
		s32 offset = 0;
		printf("\E[18;0H");
		printf("Do GSPWN\n");
		printf("copy from %08x to %08x\n",(u32)src, (u32)dest);
		while (remain > 0)
		{
			u32 isize = remain >= BUFFER_LENGTH ? BUFFER_LENGTH : remain;
			DoGsPwn((u8*)dest + offset, (u8*)src + offset, isize);
			GSPGPU_FlushDataCache((u8*)dest + offset, isize);
			svcSleepThread(5 * 1000 * 1000);
			remain -= isize;
			offset += isize;
			printf("\E[20;0H");
			printf("%08x bytes done\n", offset);
		}
	}
	
}

#ifndef ENABLE_GSPWN

u32 memoryProtect(Handle a_hDest, u32 a_uAddress, u32 a_uSize)
{
	for (u32 i = 0; i < a_uSize; i += 0x1000)
	{
		u32 uRet = svcControlProcessMemory(a_hDest, a_uAddress + i, a_uAddress + i, 0x1000, 6, 7);
		if (uRet != 0 && uRet != 0xD900060C)
		{
			return uRet;
		}
	}
	return 0;
}

u32 memoryCopy(Handle a_hDest, void* a_pDest, Handle a_hSrc, void* a_pSrc, u32 a_uSize)
{
	u32 uRet = ext_svcFlushProcessDataCache(a_hSrc, (u32)a_pSrc, a_uSize);
	if (uRet != 0)
	{
		return uRet;
	}
	uRet = ext_svcFlushProcessDataCache(a_hDest, (u32)a_pDest, a_uSize);
	if (uRet != 0)
	{
		return uRet;
	}
	u32 hDma = 0;
	u32 uDmaConfig[20] = {};
	uRet = ext_svcStartInterProcessDma(&hDma, a_hDest, a_pDest, a_hSrc, a_pSrc, a_uSize, uDmaConfig);
	if (uRet != 0)
	{
		return uRet;
	}
	u32 uState = 0;
	static u32 uInterProcessDmaFinishState = 0;
	if (uInterProcessDmaFinishState == 0)
	{
		uRet = ext_svcGetDmaState(&uState, hDma);
		svcSleepThread(1000000000);
		uRet = ext_svcGetDmaState(&uState, hDma);
		uInterProcessDmaFinishState = uState;
		printf("InterProcessDmaFinishState: %08x\n", uState);
	}
	else
	{
		u32 i = 0;
		for (i = 0; i < 10000; i++)
		{
			uState = 0;
			uRet = ext_svcGetDmaState(&uState, hDma);
			if (uState == uInterProcessDmaFinishState)
			{
				break;
			}
			svcSleepThread(1000000);
		}
		if (i >= 10000)
		{
			printf("memoryCopy time out %08x\n", uState);
			return 1;
		}
	}
	svcCloseHandle(hDma);
	return ext_svcInvalidateProcessDataCache(a_hDest, (u32)a_pDest, a_uSize);
}

#endif

void clearOutput()
{
	
	static const char* c_pLine = "                                        ";
	for (int i = 13; i < 25; i++)
	{
		printf("\E[%d;0H%s", i, c_pLine);
	}
	printf("\E[13;0H");
}

u32 initSharedFontType()
{
	clearOutput();
	
#ifdef ENABLE_GSPWN
	void * buffer = linearAlloc(BUFFER_LENGTH);
	MemoryCopy(buffer, (void*)g_uAddress, BUFFER_LENGTH);
	memcpy(&g_uSharedFontType, (u8*)buffer + 4, sizeof(g_uSharedFontType));
	linearFree(buffer);
#else
	Handle hHomeMenu = 0;
	u32 uRet = svcOpenProcess(&hHomeMenu, 0xf);
	if (uRet != 0)
	{
		printf("OpenProcess error: 0x%08X\n", uRet);
		return uRet;
	}
	else
	{
		printf("OpenProcess ok\n");
	}
	//uRet = svcControlProcessMemory(hHomeMenu, g_uAddress, g_uAddress, 0x332000, 6, 7);
	uRet = memoryProtect(hHomeMenu, g_uAddress, 0x1000);
	if (uRet != 0)
	{
		svcCloseHandle(hHomeMenu);
		printf("ControlProcessMemory error: 0x%08X\n", uRet);
		return uRet;
	}
	else
	{
		printf("ControlProcessMemory ok\n");
	}
	uRet = memoryCopy(CURRENT_PROCESS_HANDLE, &g_uSharedFontType, hHomeMenu, (void*)(g_uAddress + 4), 4);
	if (uRet != 0)
	{
		svcCloseHandle(hHomeMenu);
		printf("memoryCopy error: 0x%08X\n", uRet);
		return uRet;
	}
	svcCloseHandle(hHomeMenu);
	clearOutput();
#endif
	return 0;
}

u32 font2runtime(u8* a_pFont)
{
	BinaryFileHeader* pBinaryFileHeader = (BinaryFileHeader*)(a_pFont);
	if (pBinaryFileHeader->Signature != BinFileSigFONT)
	{
		return 1;
	}
	BinaryBlockHeader* pBinaryBlockHeader = (BinaryBlockHeader*)(pBinaryFileHeader + 1);
	if (pBinaryBlockHeader->Kind != BinBlockSigFINF)
	{
		return 1;
	}
	FontInformation* pFontInformation = (FontInformation*)(pBinaryBlockHeader + 1);
	FontTextureGlyph *pFontTextureGlyph = (FontTextureGlyph*)(a_pFont + pFontInformation->PtrGlyph);
	pFontTextureGlyph->SheetImage += g_uAddress + 0x80;
	u32 uOffset = pFontInformation->PtrMap;
	while (uOffset != 0)
	{
		FontCodeMap* pFontCodeMap = (FontCodeMap*)(a_pFont + uOffset);
		uOffset = pFontCodeMap->PtrNext;
		if (uOffset != 0)
		{
			pFontCodeMap->PtrNext += g_uAddress + 0x80;
		}
	}
	pFontInformation->PtrGlyph += g_uAddress + 0x80;
	pFontInformation->PtrWidth += g_uAddress + 0x80;
	pFontInformation->PtrMap += g_uAddress + 0x80;
	pBinaryFileHeader->Signature = BinRuntimeSigFONT;
	return 0;
}

u32 changeSharedFont(SharedFontType a_eType)
{
	u32 uRet = 0;
	static const char* c_kPath[] = { "", "sdmc:/font/std/cbf_std.bcfnt", "sdmc:/font/cn/cbf_zh-Hans-CN.bcfnt", "sdmc:/font/kr/cbf_ko-Hang-KR.bcfnt", "sdmc:/font/tw/cbf_zh-Hant-TW.bcfnt" };
	if (a_eType <= SHARED_FONT_TYPE_NULL || a_eType > SHARED_FONT_TYPE_TW)
	{
		return 1;
	}
	clearOutput();
	static int c_nTextColor = 0;
	if (c_nTextColor == 32)
	{
		printf("\E[0;%dm", c_nTextColor);
		c_nTextColor = 0;
	}
	else
	{
		printf("\E[0m");
		c_nTextColor = 32;
	}
	printf("Start change shared font %d\n", a_eType);
	FILE* fp = fopen(c_kPath[a_eType], "rb");
	if (fp == NULL)
	{
		printf("open %s error\n", c_kPath[a_eType]);
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	u32 uFileSize = ftell(fp);
	printf("font size 0x%X\n", uFileSize);
	if (uFileSize > 0x332000 - 0x80)
	{
		fclose(fp);
		printf("font size 0x%X > 0x%08X\n", uFileSize, 0x332000 - 0x80);
		return 1;
	}
	fseek(fp, 0, SEEK_SET);
	static u32 c_uFontAddress = 0;
	if (c_uFontAddress == 0)
	{
#ifdef ENABLE_GSPWN
		c_uFontAddress = (u32)linearAlloc(0x332000);
		uRet = c_uFontAddress == 0;
#else
		uRet = svcControlMemory(&c_uFontAddress, 0, 0, 0x332000, 0x10003, 3);
#endif
		if (uRet != 0)
		{
			fclose(fp);
			printf("ControlMemory error: 0x%08X\n", uRet);
			return uRet;
		}
		else
		{
			printf("ControlMemory ok\n");
			printf("font address %X\n", c_uFontAddress);
			for (int i = 0xC; i < 0x80; i += 4)
			{
				*(u32*)(c_uFontAddress + i) = 0;
			}
			*(u32*)c_uFontAddress = 2;
			*(u32*)(c_uFontAddress + 4) = g_uSharedFontType;
		}
	}
	*(u32*)(c_uFontAddress + 8) = uFileSize;
	fread((void*)(c_uFontAddress + 0x80), 1, uFileSize, fp);
	fclose(fp);
	uRet = font2runtime((u8*)(c_uFontAddress + 0x80));
	if (uRet != 0)
	{
		return uRet;
	}
#ifdef ENABLE_GSPWN
	MemoryCopy((void*)g_uAddress, c_uFontAddress, uFileSize + 0x80);
	linearFree((void*)g_uAddress);
#else
	Handle hHomeMenu = 0;
	uRet = svcOpenProcess(&hHomeMenu, 0xf);
	if (uRet != 0)
	{
		printf("OpenProcess error: 0x%08X\n", uRet);
		return uRet;
	}
	else
	{
		printf("OpenProcess ok\n");
	}
	uRet = memoryProtect(hHomeMenu, g_uAddress, 0x332000);
	if (uRet != 0)
	{
		svcCloseHandle(hHomeMenu);
		printf("ControlProcessMemory error: 0x%08X\n", uRet);
		return uRet;
	}
	else
	{
		printf("ControlProcessMemory ok\n");
	}
	uRet = memoryCopy(hHomeMenu, (void*)g_uAddress, CURRENT_PROCESS_HANDLE, (void*)c_uFontAddress, uFileSize + 0x80);
	if (uRet != 0)
	{
		svcCloseHandle(hHomeMenu);
		printf("memoryCopy error: 0x%08X\n", uRet);
		return uRet;
	}
	svcCloseHandle(hHomeMenu);
#endif
	printf("change font ok\n");
	return 0;
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);
	printf("\E[1;0H");
	printf("        Shared Font Tool v1.1\n");
	printf("\n");
	printf("Press START : exit\n");
	u32 uKernelVersion = 0;
	uKernelVersion = osGetKernelVersion();
	if (uKernelVersion > SYSTEM_VERSION(2, 44, 6))
	{
		u8 uOut;
		Result ret = APT_CheckNew3DS(&uOut);
		if (ret == 0)
		{
			if (uOut != 0)
			{
				g_bNew3DS = true;
			}
		}
	}
	printf("\E[10;0H");
	printf("New3DS: %s\n", g_bNew3DS ? "true" : "false");
	if (g_bNew3DS)
	{
		g_uAddress = 0x1bc00000;
	}
	if (initSharedFontType() != 0)
	{
		g_uSharedFontType = SHARED_FONT_TYPE_STD;
		printf("\E[11;0H");
		printf("Get shared font type failed\n");
	}
	else
	{
		printf("\E[11;0H");
		printf("SharedFontType: ");
		switch (g_uSharedFontType)
		{
		case SHARED_FONT_TYPE_NULL:
			printf("null");
			break;
		case SHARED_FONT_TYPE_STD:
			printf("std");
			break;
		case SHARED_FONT_TYPE_CN:
			printf("cn");
			break;
		case SHARED_FONT_TYPE_KR:
			printf("kr");
			break;
		case SHARED_FONT_TYPE_TW:
			printf("tw");
			break;
		default:
			printf("unknown");
			break;
		}
		printf("\n");
	}
	bool bCanRecover = false;
	bool bCanChangeToCN = false;
	bool bCanChangeToTW = false;
	bool bCanChangeToSTD = false;
	bool bCanChangeToKR = false;
	if (g_uSharedFontType > SHARED_FONT_TYPE_NULL)
	{
		bCanRecover = true;
		printf("\E[4;0H");
		printf("Press SELECT: recover font\n");
		if (g_uSharedFontType != SHARED_FONT_TYPE_KR)
		{
			printf("Press RIGHT : change font to cn (C)\n");
			bCanChangeToCN = true;
		}
		if (g_uSharedFontType != SHARED_FONT_TYPE_KR && g_uSharedFontType != SHARED_FONT_TYPE_CN && g_uSharedFontType != SHARED_FONT_TYPE_STD)
		{
			printf("Press LEFT  : change font to tw (T)\n");
			bCanChangeToTW = true;
		}
		if (g_uSharedFontType != SHARED_FONT_TYPE_KR && g_uSharedFontType != SHARED_FONT_TYPE_CN)
		{
			printf("Press UP    : change font to std(J/U/E)\n");
			bCanChangeToSTD = true;
		}
		if (true)
		{
			printf("Press DOWN  : change font to kr (K)\n");
			bCanChangeToKR = true;
		}
	}
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();
		u32 kDown = hidKeysDown();
		if ((kDown & KEY_START) != 0)
		{
			break;
		}
		else if ((kDown & KEY_SELECT) != 0 && bCanRecover)
		{
			changeSharedFont(g_uSharedFontType);
		}
		else if ((kDown & KEY_DRIGHT) != 0 && bCanChangeToCN)
		{
			changeSharedFont(SHARED_FONT_TYPE_CN);
		}
		else if ((kDown & KEY_DLEFT) != 0)
		{
			changeSharedFont(SHARED_FONT_TYPE_TW && bCanChangeToTW);
		}
		else if ((kDown & KEY_DUP) != 0)
		{
			changeSharedFont(SHARED_FONT_TYPE_STD && bCanChangeToSTD);
		}
		else if ((kDown & KEY_DDOWN) != 0)
		{
			changeSharedFont(SHARED_FONT_TYPE_KR && bCanChangeToKR);
		}
		//else if (kDown != 0)
		//{
		//	printf("\E[25;0H\E[0m%-10u\n", kDown);
		//}
		gfxFlushBuffers();
		gfxSwapBuffers();
	}
	gfxExit();
	return 0;
}
