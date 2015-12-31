#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wmultichar"

#include <stdio.h>
#include <string.h>
#include <3ds.h>

typedef enum SharedFontType
{
	SHARED_FONT_TYPE_NULL,
	SHARED_FONT_TYPE_STD,
	SHARED_FONT_TYPE_CN,
	SHARED_FONT_TYPE_KR,
	SHARED_FONT_TYPE_TW
} SharedFontType;

typedef enum SharedFontLoadState
{
	SHARED_FONT_LOAD_STATE_NULL,
	SHARED_FONT_LOAD_STATE_LOADING,
	SHARED_FONT_LOAD_STATE_LOADED,
	SHARED_FONT_LOAD_STATE_FAILED,
	SHARED_FONT_LOAD_STATE_MAX_BIT = (1u << 31)
} SharedFontLoadState;

typedef struct SharedFontBufferHeader
{
	u32 SharedFontLoadState;
	u32 SharedFontType;
	u32 Size;
	u8 Padding[116];
} __attribute__((packed)) SharedFontBufferHeader;

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

#define CONVERT_ENDIAN(n) (((n) >> 24 & 0xFF) | ((n) >> 8 & 0xFF00) | (((n) & 0xFF00) << 8) | (((n) & 0xFF) << 24))
#define ADDRESS_NON_NEW_3DS 0x18000000
#define ADDRESS_NEW_3DS 0x1bc00000
#define GSP_HEAP_START_OFFSET_DEFAULT 0x14000000
#define GSP_HEAP_START_OFFSET_FIRM80 0x30000000
#define GSP_HEAP_MAX_LENGTH (g_bNew3DS ? 0xDC00000 : 0x6800000)
#define GSP_HEAP_END_OFFSET_DEFAULT (GSP_HEAP_START_OFFSET_DEFAULT + GSP_HEAP_MAX_LENGTH)
#define GSP_HEAP_END_OFFSET_FIRM80 (GSP_HEAP_START_OFFSET_FIRM80 + GSP_HEAP_MAX_LENGTH)

const u32 g_kBinFileSigFONT = CONVERT_ENDIAN('CFNT');
const u32 g_kBinRuntimeSigFONT = CONVERT_ENDIAN('CFNU');
const u32 g_kBinBlockSigFINF = CONVERT_ENDIAN('FINF');
const u32 g_kSharedFontMemorySize = 1024 * 4 * 818;	// Shared memory size for shared fonts: 3,272 KB
const s32 g_kBufferSize = 0x1000;

u32 g_uSharedFontType = SHARED_FONT_TYPE_NULL;
bool g_bNew3DS = false;
u32 g_uAddress = ADDRESS_NON_NEW_3DS;
u32 g_uFontAddress = 0;
s32 g_nCurrentLine = 1;

void clearOutput()
{
	static const char* c_pLine = "                                        ";
	for (s32 i = 13; i < 25; i++)
	{
		printf("\E[%d;0H%s", i, c_pLine);
	}
	g_nCurrentLine = 13;
	printf("\E[%d;0H", g_nCurrentLine);
}

static inline bool isMemoryAddressWithinGSP(u32 a_uAddress)
{
	return (a_uAddress >= GSP_HEAP_START_OFFSET_DEFAULT && a_uAddress < GSP_HEAP_END_OFFSET_DEFAULT) || (a_uAddress >= GSP_HEAP_START_OFFSET_FIRM80 && a_uAddress < GSP_HEAP_END_OFFSET_FIRM80);
}

Result doGSPWN(void* a_pDest, void* a_pSrc, u32 a_uSize)
{
	return GX_SetTextureCopy(NULL, a_pSrc, 0xFFFFFFFF, a_pDest, 0xFFFFFFFF, a_uSize, 8);
}

void* memoryCopy(void* a_pDest, void* a_pSrc, u32 a_uSize)
{
	bool bIsDestWithinGSP = isMemoryAddressWithinGSP((u32)a_pDest);
	bool bIsSrcWithinGSP = isMemoryAddressWithinGSP((u32)a_pSrc);
	if (bIsDestWithinGSP)
	{
		if (bIsSrcWithinGSP)
		{
			s64 nRemain = a_uSize;
			s32 nOffset = 0;
			printf("\E[%d;0H""Do GSPWN\n", g_nCurrentLine++);
			printf("\E[%d;0H""copy from %08x to %08x\n", g_nCurrentLine++, (u32)a_pSrc, (u32)a_pDest);
			bool bChangeLine = false;
			while (nRemain > 0)
			{
				u32 uSize = nRemain >= g_kBufferSize ? g_kBufferSize : (u32)nRemain;
				doGSPWN((u8*)a_pDest + nOffset, (u8*)a_pSrc + nOffset, uSize);
				GSPGPU_FlushDataCache(NULL, (u8*)a_pDest + nOffset, uSize);
				svcSleepThread(5 * 1000 * 1000);
				nRemain -= uSize;
				nOffset += uSize;
				printf("\E[%d;0H""%08x bytes done\n", g_nCurrentLine, nOffset);
				bChangeLine = true;
			}
			if (bChangeLine)
			{
				g_nCurrentLine++;
			}
		}
		else
		{
			return NULL;
		}
	}
	else
	{
		if (bIsSrcWithinGSP)
		{
			return NULL;
		}
		else
		{
			memcpy(a_pDest, a_pSrc, a_uSize);
		}
	}
	return a_pDest;
}

void initSharedFontType()
{
	clearOutput();
	SharedFontBufferHeader* pHeader = (SharedFontBufferHeader*)linearAlloc((sizeof(SharedFontBufferHeader) + g_kBufferSize - 1) / g_kBufferSize * g_kBufferSize);
	memoryCopy(pHeader, (void*)g_uAddress, (sizeof(SharedFontBufferHeader) + g_kBufferSize - 1) / g_kBufferSize * g_kBufferSize);
	g_uSharedFontType = pHeader->SharedFontType;
	linearFree(pHeader);
}

u32 font2runtime(u8* a_pFont)
{
	BinaryFileHeader* pBinaryFileHeader = (BinaryFileHeader*)(a_pFont);
	if (pBinaryFileHeader->Signature != g_kBinFileSigFONT)
	{
		return 1;
	}
	BinaryBlockHeader* pBinaryBlockHeader = (BinaryBlockHeader*)(pBinaryFileHeader + 1);
	if (pBinaryBlockHeader->Kind != g_kBinBlockSigFINF)
	{
		return 2;
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
	pBinaryFileHeader->Signature = g_kBinRuntimeSigFONT;
	return 0;
}

u32 changeSharedFont(SharedFontType a_eType)
{
	static const char* c_kPath[] = { "", "sdmc:/font/std/cbf_std.bcfnt", "sdmc:/font/cn/cbf_zh-Hans-CN.bcfnt", "sdmc:/font/kr/cbf_ko-Hang-KR.bcfnt", "sdmc:/font/tw/cbf_zh-Hant-TW.bcfnt" };
	if (a_eType <= SHARED_FONT_TYPE_NULL || a_eType > SHARED_FONT_TYPE_TW)
	{
		return 1;
	}
	clearOutput();
	static s32 c_nTextColor = 0;
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
	printf("\E[%d;0H""Start change shared font %d\n", g_nCurrentLine++, a_eType);
	FILE* fp = fopen(c_kPath[a_eType], "rb");
	if (fp == NULL)
	{
		printf("\E[%d;0H""open %s error\n", g_nCurrentLine++, c_kPath[a_eType]);
		return 2;
	}
	fseek(fp, 0, SEEK_END);
	u32 uFileSize = ftell(fp);
	printf("\E[%d;0H""font size 0x%X\n", g_nCurrentLine++, uFileSize);
	if (uFileSize > g_kSharedFontMemorySize - sizeof(SharedFontBufferHeader))
	{
		fclose(fp);
		printf("\E[%d;0H""font size 0x%X > 0x%08X\n", g_nCurrentLine++, uFileSize, g_kSharedFontMemorySize - sizeof(SharedFontBufferHeader));
		return 3;
	}
	fseek(fp, 0, SEEK_SET);
	if (g_uFontAddress == 0)
	{
		g_uFontAddress = (u32)linearAlloc((g_kSharedFontMemorySize + g_kBufferSize - 1) / g_kBufferSize * g_kBufferSize);
		if (g_uFontAddress == 0)
		{
			fclose(fp);
			printf("\E[%d;0H""linearAlloc error\n", g_nCurrentLine++);
			return 4;
		}
		else
		{
			printf("\E[%d;0H""linearAlloc ok\n", g_nCurrentLine++);
			printf("\E[%d;0H""font address %X\n", g_nCurrentLine++, g_uFontAddress);
			for (int i = 0xC; i < sizeof(SharedFontBufferHeader); i += 4)
			{
				*(u32*)(g_uFontAddress + i) = 0;
			}
			*(u32*)g_uFontAddress = SHARED_FONT_LOAD_STATE_LOADED;
			*(u32*)(g_uFontAddress + 4) = g_uSharedFontType;
		}
	}
	*(u32*)(g_uFontAddress + 8) = uFileSize;
	fread((void*)(g_uFontAddress + sizeof(SharedFontBufferHeader)), 1, uFileSize, fp);
	fclose(fp);
	u32 uResult = font2runtime((u8*)(g_uFontAddress + sizeof(SharedFontBufferHeader)));
	if (uResult != 0)
	{
		return 5;
	}
	memoryCopy((void*)g_uAddress, (void*)g_uFontAddress, (sizeof(SharedFontBufferHeader) + uFileSize + g_kBufferSize - 1) / g_kBufferSize * g_kBufferSize);
	printf("\E[%d;0H""change font ok\n", g_nCurrentLine++);
	return 0;
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);
	printf("\E[%d;0H""        Shared Font Tool v1.2\n", g_nCurrentLine++);
	printf("\E[%d;0H""\n", g_nCurrentLine++);
	printf("\E[%d;0H""Press START : exit\n", g_nCurrentLine++);
	u32 uKernelVersion = 0;
	uKernelVersion = osGetKernelVersion();
	if (uKernelVersion > SYSTEM_VERSION(2, 44, 6))
	{
		u8 uOut;
		Result result = APT_CheckNew3DS(0, &uOut);
		if (result == 0 && uOut != 0)
		{
			g_bNew3DS = true;
		}
	}
	g_nCurrentLine = 10;
	printf("\E[%d;0H""New3DS: %s\n", g_nCurrentLine++, g_bNew3DS ? "true" : "false");
	if (g_bNew3DS)
	{
		g_uAddress = ADDRESS_NEW_3DS;
	}
	initSharedFontType();
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
		else if ((kDown & KEY_DLEFT) != 0 && bCanChangeToTW)
		{
			changeSharedFont(SHARED_FONT_TYPE_TW);
		}
		else if ((kDown & KEY_DUP) != 0 && bCanChangeToSTD)
		{
			changeSharedFont(SHARED_FONT_TYPE_STD);
		}
		else if ((kDown & KEY_DDOWN) != 0 && bCanChangeToKR)
		{
			changeSharedFont(SHARED_FONT_TYPE_KR);
		}
		//else if (kDown != 0)
		//{
		//	printf("\E[25;0H\E[0m%-10u\n", kDown);
		//}
		gfxFlushBuffers();
		gfxSwapBuffers();
	}
	if (g_uFontAddress != 0)
	{
		linearFree((void*)g_uFontAddress);
	}
	gfxExit();
	return 0;
}
