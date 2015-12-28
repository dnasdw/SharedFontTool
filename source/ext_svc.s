
.global ext_svcInvalidateProcessDataCache
.type ext_svcInvalidateProcessDataCache, %function
ext_svcInvalidateProcessDataCache:
	svc 0x52
	bx lr

.global ext_svcFlushProcessDataCache
.type ext_svcFlushProcessDataCache, %function
ext_svcFlushProcessDataCache:
	svc 0x54
	bx lr

.global ext_svcStartInterProcessDma
.type ext_svcStartInterProcessDma, %function
ext_svcStartInterProcessDma:
	stmfd sp!, {r0, r4, r5}
	ldr r0, [sp, #0xC]
	ldr r4, [sp, #0xC + 0x4]
	ldr r5, [sp, #0xC + 0x8]
	svc 0x55
	ldmfd sp!, {r2, r4, r5}
	str	r1, [r2]
	bx lr

.global ext_svcGetDmaState
.type ext_svcGetDmaState, %function
ext_svcGetDmaState:
	str r0, [sp, #-0x4]!
	svc 0x57
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr 
