#ifndef EXT_SVC_H_
#define EXT_SVC_H_

Result ext_svcInvalidateProcessDataCache(Handle handle, u32 addr, u32 size);
Result ext_svcFlushProcessDataCache(Handle handle, u32 addr, u32 size);
Result ext_svcStartInterProcessDma(Handle* hdma, Handle dstProcess, void* dst, Handle srcProcess, const void* src, u32 size, u32* config);
Result ext_svcGetDmaState(u32* state, Handle dma);

#endif	// EXT_SVC_H_
