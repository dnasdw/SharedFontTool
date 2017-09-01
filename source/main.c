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
	u8 Padding[0x74];
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

#define FONT_COUNT 11

const u32 g_kBinFileSigFONT = CONVERT_ENDIAN('CFNT');
const u32 g_kBinRuntimeSigFONT = CONVERT_ENDIAN('CFNU');
const u32 g_kBinBlockSigFINF = CONVERT_ENDIAN('FINF');
const u32 g_kSharedFontMemorySize = 1024 * 4 * 818;	// Shared memory size for shared fonts: 3,272 KB
const s32 g_kBufferSize = 0x1000;

const s32 g_nTopColumn = 50;
const s32 g_nBottomColumn = 40;

PrintConsole g_TopScreen = {};
PrintConsole g_BottomScreen = {};

u32 g_uSharedFontType = SHARED_FONT_TYPE_NULL;
bool g_bNew3DS = false;
u32 g_uAddress = ADDRESS_NON_NEW_3DS;
u32 g_uFontAddress = 0;
s32 g_nTopCurrentLine = 2;
s32 g_nBottomCurrentLine = 2;
s32 g_nCursorIndex = 0;

void clearTop()
{
	consoleSelect(&g_TopScreen);
	static const char* c_pEraser = "  ";
	static const char* c_pCursorLeft = "=>";
	static const char* c_pCursorRight = "<=";
	for (s32 i = 0; i < FONT_COUNT; i++)
	{
		printf("\E[%d;3H%s", i + 8, c_pEraser);
		printf("\E[%d;47H%s", i + 8, c_pEraser);
	}
	g_nTopCurrentLine = g_nCursorIndex + 8;
	printf("\E[%d;3H%s", g_nTopCurrentLine, c_pCursorLeft);
	printf("\E[%d;47H%s", g_nTopCurrentLine, c_pCursorRight);
}

void clearBottom()
{
	consoleSelect(&g_BottomScreen);
	static const char* c_pLine = "                                        ";
	for (s32 i = 5; i <= 30; i++)
	{
		printf("\E[%d;1H%s", i, c_pLine);
	}
	g_nBottomCurrentLine = 5;
	// bottom 5,1
	printf("\E[%d;1H", g_nBottomCurrentLine);
}

static inline bool isMemoryAddressWithinGSP(u32 a_uAddress)
{
	return (a_uAddress >= GSP_HEAP_START_OFFSET_DEFAULT && a_uAddress < GSP_HEAP_END_OFFSET_DEFAULT) || (a_uAddress >= GSP_HEAP_START_OFFSET_FIRM80 && a_uAddress < GSP_HEAP_END_OFFSET_FIRM80);
}

Result doGSPWN(void* a_pDest, void* a_pSrc, u32 a_uSize)
{
	return GX_TextureCopy(a_pSrc, 0xFFFFFFFF, a_pDest, 0xFFFFFFFF, a_uSize, 8);
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
			printf("\E[%d;1H""Do GSPWN\n", g_nBottomCurrentLine++);
			printf("\E[%d;1H""copy from %08x to %08x\n", g_nBottomCurrentLine++, (u32)a_pSrc, (u32)a_pDest);
			bool bChangeLine = false;
			while (nRemain > 0)
			{
				u32 uSize = nRemain >= g_kBufferSize ? g_kBufferSize : (u32)nRemain;
				doGSPWN((u8*)a_pDest + nOffset, (u8*)a_pSrc + nOffset, uSize);
				GSPGPU_FlushDataCache((u8*)a_pDest + nOffset, uSize);
				svcSleepThread(5 * 1000 * 1000);
				nRemain -= uSize;
				nOffset += uSize;
				printf("\E[%d;1H""%08x bytes done\n", g_nBottomCurrentLine, nOffset);
				bChangeLine = true;
			}
			if (bChangeLine)
			{
				g_nBottomCurrentLine++;
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
	clearBottom();
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

u32 changeSharedFont(s32 a_nFontIndex)
{
	static const char* c_kPath[FONT_COUNT] =
	{
		"romfs:/font/cj/cbf_cj.bcfnt"
		, "romfs:/font/cj/cbf_zh-Hans-CN.bcfnt"
		, "romfs:/font/cj/cbf_std.bcfnt"
		, "romfs:/font/cn/cbf_zh-Hans-CN.bcfnt"
		, "romfs:/font/tw/cbf_zh-Hant-TW.bcfnt"
		, "romfs:/font/std/cbf_std.bcfnt"
		, "romfs:/font/kr/cbf_ko-Hang-KR.bcfnt"
		, "sdmc:/font/cn/cbf_zh-Hans-CN.bcfnt"
		, "sdmc:/font/tw/cbf_zh-Hant-TW.bcfnt"
		, "sdmc:/font/std/cbf_std.bcfnt"
		, "sdmc:/font/kr/cbf_ko-Hang-KR.bcfnt"
	};
	if (a_nFontIndex < 0 || a_nFontIndex > FONT_COUNT)
	{
		return 1;
	}
	clearBottom();
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
	printf("\E[%d;1H""Start change shared font %d\n", g_nBottomCurrentLine++, a_nFontIndex);
	FILE* fp = fopen(c_kPath[a_nFontIndex], "rb");
	if (fp == NULL)
	{
		printf("\E[%d;1H""open %s error\n", g_nBottomCurrentLine++, c_kPath[a_nFontIndex]);
		return 2;
	}
	fseek(fp, 0, SEEK_END);
	u32 uFileSize = ftell(fp);
	printf("\E[%d;1H""font size 0x%X\n", g_nBottomCurrentLine++, uFileSize);
	if (uFileSize > g_kSharedFontMemorySize - sizeof(SharedFontBufferHeader))
	{
		fclose(fp);
		printf("\E[%d;1H""font size 0x%X > 0x%08X\n", g_nBottomCurrentLine++, uFileSize, g_kSharedFontMemorySize - sizeof(SharedFontBufferHeader));
		return 3;
	}
	fseek(fp, 0, SEEK_SET);
	if (g_uFontAddress == 0)
	{
		g_uFontAddress = (u32)linearAlloc((g_kSharedFontMemorySize + g_kBufferSize - 1) / g_kBufferSize * g_kBufferSize);
		if (g_uFontAddress == 0)
		{
			fclose(fp);
			printf("\E[%d;1H""linearAlloc error\n", g_nBottomCurrentLine++);
			return 4;
		}
		else
		{
			printf("\E[%d;1H""linearAlloc ok\n", g_nBottomCurrentLine++);
			printf("\E[%d;1H""font address %X\n", g_nBottomCurrentLine++, g_uFontAddress);
			for (s32 i = 0xC; i < sizeof(SharedFontBufferHeader); i += 4)
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
	printf("\E[%d;1H""change font ok\n", g_nBottomCurrentLine++);
	return 0;
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	consoleInit(GFX_TOP, &g_TopScreen);
	consoleInit(GFX_BOTTOM, &g_BottomScreen);
	romfsInit();
	const char* pTitle = "Shared Font Tool v2.0";
	const char* pAuthors = "dnasdw, enler";
	consoleSelect(&g_BottomScreen);
	// bottom 2,1
	printf("\E[%d;1H", g_nBottomCurrentLine++);
	for (s32 i = 0; i < (g_nBottomColumn - strlen(pTitle)) / 2; i++)
	{
		printf(" ");
	}
	printf("%s", pTitle);
	// bottom 3,1
	printf("\E[%d;1H", g_nBottomCurrentLine++);
	for (s32 i = 0; i < (g_nBottomColumn - strlen(pAuthors)) / 2; i++)
	{
		printf(" ");
	}
	printf("%s", pAuthors);
	// bottom 4,1
	printf("\E[%d;1H", g_nBottomCurrentLine++);
	consoleSelect(&g_TopScreen);
	// top 2,1
	printf("\E[%d;1H", g_nTopCurrentLine++);
	for (s32 i = 0; i < (g_nTopColumn - strlen(pTitle)) / 2; i++)
	{
		printf(" ");
	}
	printf("%s", pTitle);
	// top 3,1
	printf("\E[%d;1H", g_nTopCurrentLine++);
	for (s32 i = 0; i < (g_nTopColumn - strlen(pAuthors)) / 2; i++)
	{
		printf(" ");
	}
	printf("%s", pAuthors);
	// top 4,1
	printf("\E[%d;1H", g_nTopCurrentLine++);
	APT_CheckNew3DS(&g_bNew3DS);
	if (g_bNew3DS)
	{
		g_uAddress = ADDRESS_NEW_3DS;
	}
	// top 5,6
	printf("\E[%d;6H""New3DS: %s", g_nTopCurrentLine++, g_bNew3DS ? "true" : "false");
	initSharedFontType();
	s32 nRecoverPossibility = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	s32 nChangeFontPossibility[FONT_COUNT] = {};
	nChangeFontPossibility[0] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	nChangeFontPossibility[1] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	nChangeFontPossibility[2] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	nChangeFontPossibility[3] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	nChangeFontPossibility[4] = g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	nChangeFontPossibility[5] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	nChangeFontPossibility[6] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 2 : 0;
	nChangeFontPossibility[7] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 1 : 0;
	nChangeFontPossibility[8] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 1 : 0;
	nChangeFontPossibility[9] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 1 : 0;
	nChangeFontPossibility[10] = g_uSharedFontType == SHARED_FONT_TYPE_STD || g_uSharedFontType == SHARED_FONT_TYPE_CN || g_uSharedFontType == SHARED_FONT_TYPE_KR || g_uSharedFontType == SHARED_FONT_TYPE_TW ? 1 : 0;
	s32 nColor[3] = {31, 33, 32};	// red, yellow, green
	s32 nRecoverIndex = -1;
	consoleSelect(&g_TopScreen);
	// top 6,6
	printf("\E[%d;6H""SharedFontType: ", g_nTopCurrentLine++);
	switch (g_uSharedFontType)
	{
	case SHARED_FONT_TYPE_NULL:
		printf("null");
		break;
	case SHARED_FONT_TYPE_STD:
		nRecoverIndex = 5;
		printf("std");
		break;
	case SHARED_FONT_TYPE_CN:
		nRecoverIndex = 3;
		printf("cn");
		break;
	case SHARED_FONT_TYPE_KR:
		nRecoverIndex = 4;
		printf("kr");
		break;
	case SHARED_FONT_TYPE_TW:
		nRecoverIndex = 6;
		printf("tw");
		break;
	default:
		printf("unknown");
		break;
	}
	// top 7,1
	printf("\E[%d;1H", g_nTopCurrentLine++);
	// top 8,6
	printf("\E[%d;6H""\E[0;%dm"" 0. romfs: Chinese&Japanese (C/T/J/U/E)""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[0]]);
	// top 9,6
	printf("\E[%d;6H""\E[0;%dm"" 1. romfs: modified official cn (C)""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[1]]);
	// top 10,6
	printf("\E[%d;6H""\E[0;%dm"" 2. romfs: modified official std (J/U/E)""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[2]]);
	// top 11,6
	printf("\E[%d;6H""\E[0;%dm"" 3. romfs: official cn (C)""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[3]]);
	// top 12,6
	printf("\E[%d;6H""\E[0;%dm"" 4. romfs: official tw (T)""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[4]]);
	// top 13,6
	printf("\E[%d;6H""\E[0;%dm"" 5. romfs: official std (J/U/E)""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[5]]);
	// top 14,6
	printf("\E[%d;6H""\E[0;%dm"" 6. romfs: official kr (K)""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[6]]);
	// top 15,6
	printf("\E[%d;6H""\E[0;%dm"" 7. sdmc:  /font/cn/cbf_zh-Hans-CN.bcfnt""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[7]]);
	// top 16,6
	printf("\E[%d;6H""\E[0;%dm"" 8. sdmc:  /font/tw/cbf_zh-Hant-TW.bcfnt""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[8]]);
	// top 17,6
	printf("\E[%d;6H""\E[0;%dm"" 9. sdmc:  /font/std/cbf_std.bcfnt""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[9]]);
	// top 18,6
	printf("\E[%d;6H""\E[0;%dm""10. sdmc:  /font/kr/cbf_ko-Hang-KR.bcfnt""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[10]]);
	// top 19,1
	printf("\E[%d;1H", g_nTopCurrentLine++);
	// top 20,6
	printf("\E[%d;6H""\E[0;32m""Color Green  : will not crash""\E[0m", g_nTopCurrentLine++);
	// top 21,6
	printf("\E[%d;6H""\E[0;33m""Color Yellow : maybe crash""\E[0m", g_nTopCurrentLine++);
	// top 22,6
	printf("\E[%d;6H""\E[0;31m""Color Red    : will crash""\E[0m", g_nTopCurrentLine++);
	// top 23,1
	printf("\E[%d;1H", g_nTopCurrentLine++);
	// top 24,6
	printf("\E[%d;6H""DPAD : UP DOWN LEFT RIGHT", g_nTopCurrentLine++);
	// top 25,6
	printf("\E[%d;1H", g_nTopCurrentLine++);
	// top 26,6
	printf("\E[%d;6H""Press DPAD   : move the cursor =>", g_nTopCurrentLine++);
	// top 27,6
	printf("\E[%d;6H""\E[0;%dm""Press A      : perform change font""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[g_nCursorIndex]]);
	// top 28,6
	printf("\E[%d;6H""\E[0;%dm""Press SELECT : recover font""\E[0m", g_nTopCurrentLine++, nColor[nRecoverPossibility]);
	// top 29,6
	printf("\E[%d;6H""Press START  : exit", g_nTopCurrentLine++);
	clearTop();
	consoleSelect(&g_BottomScreen);
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();
		u32 uKeysDown = hidKeysDown();
		if ((uKeysDown & KEY_START) != 0)
		{
			break;
		}
		else if ((uKeysDown & KEY_SELECT) != 0)
		{
			if (nRecoverPossibility == 0)
			{
				clearBottom();
				// bottom 5,1
				printf("\E[%d;1H""\E[0;31m""can not change font\E[0m\n", g_nBottomCurrentLine++);
			}
			else
			{
				changeSharedFont(nRecoverIndex);
			}
		}
		else if ((uKeysDown & KEY_A) != 0)
		{
			if (nChangeFontPossibility[g_nCursorIndex] == 0)
			{
				clearBottom();
				// bottom 5,1
				printf("\E[%d;1H""\E[0;31m""can not change font\E[0m\n", g_nBottomCurrentLine++);
			}
			else
			{
				changeSharedFont(g_nCursorIndex);
			}
		}
		else if ((uKeysDown & KEY_DRIGHT) != 0)
		{
			g_nCursorIndex = 10;
			clearTop();
			g_nTopCurrentLine = 27;
			// top 27,6
			printf("\E[%d;6H""\E[0;%dm""Press A      : perform change font""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[g_nCursorIndex]]);
		}
		else if ((uKeysDown & KEY_DLEFT) != 0)
		{
			g_nCursorIndex = 0;
			clearTop();
			g_nTopCurrentLine = 27;
			// top 27,6
			printf("\E[%d;6H""\E[0;%dm""Press A      : perform change font""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[g_nCursorIndex]]);
		}
		else if ((uKeysDown & KEY_DUP) != 0)
		{
			g_nCursorIndex = (g_nCursorIndex - 1 + FONT_COUNT) % FONT_COUNT;
			clearTop();
			g_nTopCurrentLine = 27;
			// top 27,6
			printf("\E[%d;6H""\E[0;%dm""Press A      : perform change font""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[g_nCursorIndex]]);
		}
		else if ((uKeysDown & KEY_DDOWN) != 0)
		{
			g_nCursorIndex = (g_nCursorIndex + 1) % FONT_COUNT;
			clearTop();
			g_nTopCurrentLine = 27;
			// top 27,6
			printf("\E[%d;6H""\E[0;%dm""Press A      : perform change font""\E[0m", g_nTopCurrentLine++, nColor[nChangeFontPossibility[g_nCursorIndex]]);
		}
		// else if (uKeysDown != 0)
		// {
		// 	consoleSelect(&g_BottomScreen);
		// 	// bottom 29,1
		// 	printf("\E[29;1H\E[0m%-10u\n", uKeysDown);
		// }
		gfxFlushBuffers();
		gfxSwapBuffers();
	}
	if (g_uFontAddress != 0)
	{
		linearFree((void*)g_uFontAddress);
	}
	romfsExit();
	gfxExit();
	return 0;
}
