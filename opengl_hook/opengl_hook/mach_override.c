// mach_override.c semver:1.2.0
//   Copyright (c) 2003-2012 Jonathan 'Wolf' Rentzsch: http://rentzsch.com
//   Some rights reserved: http://opensource.org/licenses/mit
//   https://github.com/rentzsch/mach_override

#include "mach_override.h"

#include <mach-o/dyld.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/vm_map.h>
#include <sys/mman.h>
#include <dlfcn.h>

#include <CoreServices/CoreServices.h>

/**************************
 *
 *	Constants
 *
 **************************/
#pragma mark	-
#pragma mark	(Constants)

#if defined(__ppc__) || defined(__POWERPC__)

long kIslandTemplate[] = {
    0x9001FFFC,	//	stw		r0,-4(SP)
    0x3C00DEAD,	//	lis		r0,0xDEAD
    0x6000BEEF,	//	ori		r0,r0,0xBEEF
    0x7C0903A6,	//	mtctr	r0
    0x8001FFFC,	//	lwz		r0,-4(SP)
    0x60000000,	//	nop		; optionally replaced
    0x4E800420 	//	bctr
};

#define kAddressHi			3
#define kAddressLo			5
#define kInstructionHi		10
#define kInstructionLo		11

#elif defined(__i386__)

#define kOriginalInstructionsSize 16

char kIslandTemplate[] = {
    // kOriginalInstructionsSize nop instructions so that we
    // should have enough space to host original instructions
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    // Now the real jump instruction
    0xE9, 0xEF, 0xBE, 0xAD, 0xDE
};

#define kInstructions	0
#define kJumpAddress    kInstructions + kOriginalInstructionsSize + 1
#elif defined(__x86_64__)

#define kOriginalInstructionsSize 32

#define kJumpAddress    kOriginalInstructionsSize + 6

char kIslandTemplate[] = {
    // kOriginalInstructionsSize nop instructions so that we
    // should have enough space to host original instructions
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    // Now the real jump instruction
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

#endif

#define	kAllocateHigh		1
#define	kAllocateNormal		0

/**************************
 *
 *	Data Types
 *
 **************************/
#pragma mark	-
#pragma mark	(Data Types)

typedef	struct	{
    char	instructions[sizeof(kIslandTemplate)];
    int		allocatedHigh;
}	BranchIsland;

/**************************
 *
 *	Funky Protos
 *
 **************************/
#pragma mark	-
#pragma mark	(Funky Protos)

mach_error_t
allocateBranchIsland(
                     BranchIsland	**island,
                     int				allocateHigh,
                     void *originalFunctionAddress);

mach_error_t
freeBranchIsland(
                 BranchIsland	*island );

#if defined(__ppc__) || defined(__POWERPC__)
mach_error_t
setBranchIslandTarget(
                      BranchIsland	*island,
                      const void		*branchTo,
                      long			instruction );
#endif

#if defined(__i386__) || defined(__x86_64__)
mach_error_t
setBranchIslandTarget_i386(
                           BranchIsland	*island,
                           const void		*branchTo,
                           char*			instructions );
void
atomic_mov64(
             uint64_t *targetAddress,
             uint64_t value );

static Boolean
eatKnownInstructions(
                     unsigned char	*code,
                     uint64_t		*newInstruction,
                     int				*howManyEaten,
                     char			*originalInstructions,
                     int				*originalInstructionCount,
                     uint8_t			*originalInstructionSizes );

static void
fixupInstructions(
                  void		*originalFunction,
                  void		*escapeIsland,
                  void		*instructionsToFix,
                  int			instructionCount,
                  uint8_t		*instructionSizes );
#endif

/*******************************************************************************
 *
 *	Interface
 *
 *******************************************************************************/
#pragma mark	-
#pragma mark	(Interface)

/*mach_error_t
 mach_override(
 char *originalFunctionSymbolName,
 const char *originalFunctionLibraryNameHint,
 const void *overrideFunctionAddress,
 void **originalFunctionReentryIsland )
 {
	assert( originalFunctionSymbolName );
	assert( strlen( originalFunctionSymbolName ) );
	assert( overrideFunctionAddress );
	
	//	Lookup the original function's code pointer.
	long	*originalFunctionPtr;
	if( originalFunctionLibraryNameHint )
 _dyld_lookup_and_bind_with_hint(
 originalFunctionSymbolName,
 originalFunctionLibraryNameHint,
 (void*) &originalFunctionPtr,
 NULL );
	else{
 _dyld_lookup_and_bind(
 originalFunctionSymbolName,
 (void*) &originalFunctionPtr,
 NULL );
	}
	
	//printf ("In mach_override\n");
	return mach_override_ptr( originalFunctionPtr, overrideFunctionAddress,
 originalFunctionReentryIsland );
 }//*/

mach_error_t
mach_override(
              char *originalFunctionSymbolName,
              const char *originalFunctionLibraryNameHint,
              const void *overrideFunctionAddress,
              void **originalFunctionReentryIsland )
{
    //assert( originalFunctionSymbolName );
    //assert( strlen( originalFunctionSymbolName ) );
    //assert( overrideFunctionAddress );
    
    //	Lookup the original function's code pointer.
    long	*originalFunctionPtr = NULL;
    if( originalFunctionLibraryNameHint )
        dlsym(originalFunctionReentryIsland,
              originalFunctionLibraryNameHint);
    else{
        _dyld_lookup_and_bind(
                              originalFunctionSymbolName,
                              (void*) &originalFunctionPtr,
                              NULL );
    }
    
    //printf ("In mach_override\n");
    return mach_override_ptr( originalFunctionPtr, overrideFunctionAddress,
                             originalFunctionReentryIsland );
}

#if defined(__i386__) || defined(__x86_64__)
mach_error_t makeIslandExecutable(void *address) {
    mach_error_t err = err_none;
    vm_size_t pageSize;
    host_page_size( mach_host_self(), &pageSize );
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)(pageSize-1);
    int e = err_none;
    e |= mprotect((void *)page, pageSize, PROT_EXEC | PROT_READ | PROT_WRITE);
    e |= msync((void *)page, pageSize, MS_INVALIDATE );
    if (e) {
        err = err_cannot_override;
    }
    return err;
}
#endif

mach_error_t
mach_override_ptr(
                  void *originalFunctionAddress,
                  const void *overrideFunctionAddress,
                  void **originalFunctionReentryIsland )
{
    //assert( originalFunctionAddress );
    //assert( overrideFunctionAddress );
    
    // this addresses overriding such functions as AudioOutputUnitStart()
    // test with modified DefaultOutputUnit project
#if defined(__x86_64__)
    for(;;){
        if(*(uint16_t*)originalFunctionAddress==0x25FF)    // jmp qword near [rip+0x????????]
            originalFunctionAddress=*(void**)((char*)originalFunctionAddress+6+*(int32_t *)((uint16_t*)originalFunctionAddress+1));
        else break;
    }
#elif defined(__i386__)
    for(;;){
        if(*(uint16_t*)originalFunctionAddress==0x25FF)    // jmp *0x????????
            originalFunctionAddress=**(void***)((uint16_t*)originalFunctionAddress+1);
        else break;
    }
#endif
    
    long	*originalFunctionPtr = (long*) originalFunctionAddress;
    mach_error_t	err = err_none;
    
#if defined(__ppc__) || defined(__POWERPC__)
    //	Ensure first instruction isn't 'mfctr'.
#define	kMFCTRMask			0xfc1fffff
#define	kMFCTRInstruction	0x7c0903a6
    
    long	originalInstruction = *originalFunctionPtr;
    if( !err && ((originalInstruction & kMFCTRMask) == kMFCTRInstruction) )
        err = err_cannot_override;
#elif defined(__i386__) || defined(__x86_64__)
    int eatenCount = 0;
    int originalInstructionCount = 0;
    char originalInstructions[kOriginalInstructionsSize];
    uint8_t originalInstructionSizes[kOriginalInstructionsSize];
    uint64_t jumpRelativeInstruction = 0; // JMP
    
    Boolean overridePossible = eatKnownInstructions ((unsigned char *)originalFunctionPtr,
                                                     &jumpRelativeInstruction, &eatenCount,
                                                     originalInstructions, &originalInstructionCount,
                                                     originalInstructionSizes );
    if (eatenCount > kOriginalInstructionsSize) {
        //printf ("Too many instructions eaten\n");
        overridePossible = false;
    }
    if (!overridePossible) err = err_cannot_override;
    if (err) {
        //fprintf(stderr, "err = %x %s:%d\n", err, __FILE__, __LINE__);
    }
#endif
    
    //	Make the original function implementation writable.
    if( !err ) {
        err = vm_protect( mach_task_self(),
                         (vm_address_t) originalFunctionPtr, 8, false,
                         (VM_PROT_ALL | VM_PROT_COPY) );
        if( err )
            err = vm_protect( mach_task_self(),
                             (vm_address_t) originalFunctionPtr, 8, false,
                             (VM_PROT_DEFAULT | VM_PROT_COPY) );
    }
    if (err) {
        //fprintf(stderr, "err = %x %s:%d\n", err, __FILE__, __LINE__);
    }
    
    //	Allocate and target the escape island to the overriding function.
    BranchIsland	*escapeIsland = NULL;
    if( !err )
        err = allocateBranchIsland( &escapeIsland, kAllocateHigh, originalFunctionAddress );
    if (err) {
        //fprintf(stderr, "err = %x %s:%d\n", err, __FILE__, __LINE__);
    }
    
    
#if defined(__ppc__) || defined(__POWERPC__)
    if( !err )
        err = setBranchIslandTarget( escapeIsland, overrideFunctionAddress, 0 );
    
    //	Build the branch absolute instruction to the escape island.
    long	branchAbsoluteInstruction = 0; // Set to 0 just to silence warning.
    if( !err ) {
        long escapeIslandAddress = ((long) escapeIsland) & 0x3FFFFFF;
        branchAbsoluteInstruction = 0x48000002 | escapeIslandAddress;
    }
#elif defined(__i386__) || defined(__x86_64__)
    if (err) {
        //fprintf(stderr, "err = %x %s:%d\n", err, __FILE__, __LINE__);
    }
    
    if( !err )
        err = setBranchIslandTarget_i386( escapeIsland, overrideFunctionAddress, 0 );
    
    if (err) {
        //fprintf(stderr, "err = %x %s:%d\n", err, __FILE__, __LINE__);
        
    }
    // Build the jump relative instruction to the escape island
#endif
    
    
#if defined(__i386__) || defined(__x86_64__)
    if (!err) {
        uint32_t addressOffset = ((char*)escapeIsland - (char*)originalFunctionPtr - 5);
        addressOffset = OSSwapInt32(addressOffset);
        
        jumpRelativeInstruction |= 0xE900000000000000LL;
        jumpRelativeInstruction |= ((uint64_t)addressOffset & 0xffffffff) << 24;
        jumpRelativeInstruction = OSSwapInt64(jumpRelativeInstruction);
    }
#endif
    
    //	Optionally allocate & return the reentry island. This may contain relocated
    //  jmp instructions and so has all the same addressing reachability requirements
    //  the escape island has to the original function, except the escape island is
    //  technically our original function.
    BranchIsland	*reentryIsland = NULL;
    if( !err && originalFunctionReentryIsland ) {
        err = allocateBranchIsland( &reentryIsland, kAllocateHigh, escapeIsland);
        if( !err )
            *originalFunctionReentryIsland = reentryIsland;
    }
    
#if defined(__ppc__) || defined(__POWERPC__)
    //	Atomically:
    //	o If the reentry island was allocated:
    //		o Insert the original instruction into the reentry island.
    //		o Target the reentry island at the 2nd instruction of the
    //		  original function.
    //	o Replace the original instruction with the branch absolute.
    if( !err ) {
        int escapeIslandEngaged = false;
        do {
            if( reentryIsland )
                err = setBranchIslandTarget( reentryIsland,
                                            (void*) (originalFunctionPtr+1), originalInstruction );
            if( !err ) {
                escapeIslandEngaged = CompareAndSwap( originalInstruction,
                                                     branchAbsoluteInstruction,
                                                     (UInt32*)originalFunctionPtr );
                if( !escapeIslandEngaged ) {
                    //	Someone replaced the instruction out from under us,
                    //	re-read the instruction, make sure it's still not
                    //	'mfctr' and try again.
                    originalInstruction = *originalFunctionPtr;
                    if( (originalInstruction & kMFCTRMask) == kMFCTRInstruction)
                        err = err_cannot_override;
                }
            }
        } while( !err && !escapeIslandEngaged );
    }
#elif defined(__i386__) || defined(__x86_64__)
    // Atomically:
    //	o If the reentry island was allocated:
    //		o Insert the original instructions into the reentry island.
    //		o Target the reentry island at the first non-replaced
    //        instruction of the original function.
    //	o Replace the original first instructions with the jump relative.
    //
    // Note that on i386, we do not support someone else changing the code under our feet
    if ( !err ) {
        fixupInstructions(originalFunctionPtr, reentryIsland, originalInstructions,
                          originalInstructionCount, originalInstructionSizes );
        
        if( reentryIsland )
            err = setBranchIslandTarget_i386( reentryIsland,
                                             (void*) ((char *)originalFunctionPtr+eatenCount), originalInstructions );
        // try making islands executable before planting the jmp
#if defined(__x86_64__) || defined(__i386__)
        if( !err )
            err = makeIslandExecutable(escapeIsland);
        if( !err && reentryIsland )
            err = makeIslandExecutable(reentryIsland);
#endif
        if ( !err )
            atomic_mov64((uint64_t *)originalFunctionPtr, jumpRelativeInstruction);
    }
#endif
    
    //	Clean up on error.
    if( err ) {
        if( reentryIsland )
            freeBranchIsland( reentryIsland );
        if( escapeIsland )
            freeBranchIsland( escapeIsland );
    }
    
    return err;
}

/*******************************************************************************
 *
 *	Implementation
 *
 *******************************************************************************/
#pragma mark	-
#pragma mark	(Implementation)

/*******************************************************************************
 Implementation: Allocates memory for a branch island.
 
 @param	island			<-	The allocated island.
 @param	allocateHigh	->	Whether to allocate the island at the end of the
 address space (for use with the branch absolute
 instruction).
 @result					<-	mach_error_t
 
 ***************************************************************************/

mach_error_t
allocateBranchIsland(
                     BranchIsland	**island,
                     int				allocateHigh,
                     void *originalFunctionAddress)
{
    //assert( island );
    
    mach_error_t	err = err_none;
    
    if( allocateHigh ) {
        vm_size_t pageSize;
        err = host_page_size( mach_host_self(), &pageSize );
        if( !err ) {
            //assert( sizeof( BranchIsland ) <= pageSize );
#if defined(__ppc__) || defined(__POWERPC__)
            vm_address_t first = 0xfeffffff;
            vm_address_t last = 0xfe000000 + pageSize;
#elif defined(__x86_64__)
            vm_address_t first = ((uint64_t)originalFunctionAddress & ~(uint64_t)(((uint64_t)1 << 31) - 1)) | ((uint64_t)1 << 31); // start in the middle of the page?
            vm_address_t last = 0x0;
#else
            vm_address_t first = 0xffc00000;
            vm_address_t last = 0xfffe0000;
#endif
            
            vm_address_t page = first;
            int allocated = 0;
            vm_map_t task_self = mach_task_self();
            
            while( !err && !allocated && page != last ) {
                
                err = vm_allocate( task_self, &page, pageSize, 0 );
                if( err == err_none )
                    allocated = 1;
                else if( err == KERN_NO_SPACE ) {
#if defined(__x86_64__)
                    page -= pageSize;
#else
                    page += pageSize;
#endif
                    err = err_none;
                }
            }
            if( allocated )
                *island = (BranchIsland*) page;
            else if( !allocated && !err )
                err = KERN_NO_SPACE;
        }
    } else {
        void *block = malloc( sizeof( BranchIsland ) );
        if( block )
            *island = block;
        else
            err = KERN_NO_SPACE;
    }
    if( !err )
        (**island).allocatedHigh = allocateHigh;
    
    return err;
}

/*******************************************************************************
 Implementation: Deallocates memory for a branch island.
 
 @param	island	->	The island to deallocate.
 @result			<-	mach_error_t
 
 ***************************************************************************/

mach_error_t
freeBranchIsland(
                 BranchIsland	*island )
{
    //assert( island );
    //assert( (*(long*)&island->instructions[0]) == kIslandTemplate[0] );
    //assert( island->allocatedHigh );
    
    mach_error_t	err = err_none;
    
    if( island->allocatedHigh ) {
        vm_size_t pageSize;
        err = host_page_size( mach_host_self(), &pageSize );
        if( !err ) {
            //assert( sizeof( BranchIsland ) <= pageSize );
            err = vm_deallocate(
                                mach_task_self(),
                                (vm_address_t) island, pageSize );
        }
    } else {
        free( island );
    }
    
    return err;
}

/*******************************************************************************
 Implementation: Sets the branch island's target, with an optional
 instruction.
 
 @param	island		->	The branch island to insert target into.
 @param	branchTo	->	The address of the target.
 @param	instruction	->	Optional instruction to execute prior to branch. Set
 to zero for nop.
 @result				<-	mach_error_t
 
 ***************************************************************************/
#if defined(__ppc__) || defined(__POWERPC__)
mach_error_t
setBranchIslandTarget(
                      BranchIsland	*island,
                      const void		*branchTo,
                      long			instruction )
{
    //	Copy over the template code.
    bcopy( kIslandTemplate, island->instructions, sizeof( kIslandTemplate ) );
    
    //	Fill in the address.
    ((short*)island->instructions)[kAddressLo] = ((long) branchTo) & 0x0000FFFF;
    ((short*)island->instructions)[kAddressHi]
    = (((long) branchTo) >> 16) & 0x0000FFFF;
    
    //	Fill in the (optional) instuction.
    if( instruction != 0 ) {
        ((short*)island->instructions)[kInstructionLo]
        = instruction & 0x0000FFFF;
        ((short*)island->instructions)[kInstructionHi]
        = (instruction >> 16) & 0x0000FFFF;
    }
    
    //MakeDataExecutable( island->instructions, sizeof( kIslandTemplate ) );
    msync( island->instructions, sizeof( kIslandTemplate ), MS_INVALIDATE );
    
    return err_none;
}
#endif

#if defined(__i386__)
mach_error_t
setBranchIslandTarget_i386(
                           BranchIsland	*island,
                           const void		*branchTo,
                           char*			instructions )
{
    
    //	Copy over the template code.
    bcopy( kIslandTemplate, island->instructions, sizeof( kIslandTemplate ) );
    
    // copy original instructions
    if (instructions) {
        bcopy (instructions, island->instructions + kInstructions, kOriginalInstructionsSize);
    }
    
    // Fill in the address.
    int32_t addressOffset = (char *)branchTo - (island->instructions + kJumpAddress + 4);
    *((int32_t *)(island->instructions + kJumpAddress)) = addressOffset;
    
    msync( island->instructions, sizeof( kIslandTemplate ), MS_INVALIDATE );
    return err_none;
}

#elif defined(__x86_64__)
mach_error_t
setBranchIslandTarget_i386(
                           BranchIsland	*island,
                           const void		*branchTo,
                           char*			instructions )
{
    // Copy over the template code.
    bcopy( kIslandTemplate, island->instructions, sizeof( kIslandTemplate ) );
    
    // Copy original instructions.
    if (instructions) {
        bcopy (instructions, island->instructions, kOriginalInstructionsSize);
    }
    
    //	Fill in the address.
    *((uint64_t *)(island->instructions + kJumpAddress)) = (uint64_t)branchTo;
    msync( island->instructions, sizeof( kIslandTemplate ), MS_INVALIDATE );
    
    return err_none;
}
#endif


#if defined(__i386__) || defined(__x86_64__)
// simplistic instruction matching
typedef struct {
    unsigned int length; // max 15
    unsigned char mask[15]; // sequence of bytes in memory order
    unsigned char constraint[15]; // sequence of bytes in memory order
}	AsmInstructionMatch;

#if defined(__i386__)
static AsmInstructionMatch possibleInstructions[] = {//f8000001
    { 0x1, {0xFF}, {0x90} },							// nop
    { 0x1, {0xFF}, {0x55} },							// push %esp
    { 0x2, {0xFF, 0xFF}, {0x89, 0xE5} },				                // mov %esp,%ebp
    { 0x1, {0xFF}, {0x53} },							// push %ebx
    { 0x3, {0xFF, 0xFF, 0x00}, {0x83, 0xEC, 0x00} },	                        // sub 0x??, %esp
    { 0x1, {0xFF}, {0x57} },							// push %edi
    { 0x1, {0xFF}, {0x56} },							// push %esi
    
    // added by Tanaris4!
    { 0x3, {0xFF, 0x00, 0x00}, {0x8B, 0x00, 0x00} },							// mov eax, [ebp+arg_0]
    { 0x5, {0xFF, 0x00, 0x00, 0x00, 0x00}, {0xA1, 0x00, 0x00, 0x00, 0x00} },	// mov %eax, dword_
    { 0x5, {0xFF, 0x00, 0x00, 0x00, 0x00}, {0xB9, 0x00, 0x00, 0x00, 0x00} },	// call    $+5
    { 0x5, {0xFF, 0x00, 0x00, 0x00, 0x00}, {0xA1, 0x00, 0x00, 0x00, 0x00} },	// mov     ecx, offset byte_B33150
    
    { 0x5, {0xFF, 0x00, 0x00, 0x00, 0x00}, {0x83, 0x00, 0x00, 0x00, 0x00} },	// add     [esp+arg_0], 0FFFFFFF8h
    // Added by loco!
    { 0x7, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, {0x55, 0x89, 0xE5, 0x0F, 0xB6, 0x45, 0x08} },// this one doesn't work.
    //
    
    // end added by loco section
    
    { 0x6, {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00}, {0x81, 0xEC, 0x00, 0x00, 0x00, 0x00} },	//sub     esp, 0C8h
    { 0x2, {0xFF, 0x00}, {0x31, 0x00} },			// xor edi edi
    { 0x2, {0xFF, 0xFF}, {0x89, 0xC7} },				                // mov     edi, eax
    { 0x5, {0xFF, 0x00, 0x00, 0x00, 0x00}, {0xBA, 0x00, 0x00, 0x00, 0x00} },	// mov     edx, 1
    { 0x0 }
};
#elif defined(__x86_64__)
static AsmInstructionMatch possibleInstructions[] = {
    { 0x5, {0xFF, 0x00, 0x00, 0x00, 0x00}, {0xE9, 0x00, 0x00, 0x00, 0x00} },	// jmp 0x????????
    { 0x1, {0xFF}, {0x90} },							// nop
    { 0x1, {0xF8}, {0x50} },							// push %rX
    { 0x3, {0xFF, 0xFF, 0xFF}, {0x48, 0x89, 0xE5} },				// mov %rsp,%rbp
    { 0x4, {0xFF, 0xFF, 0xFF, 0x00}, {0x48, 0x83, 0xEC, 0x00} },	                // sub 0x??, %rsp
    { 0x4, {0xFB, 0xFF, 0x00, 0x00}, {0x48, 0x89, 0x00, 0x00} },	                // move onto rbp
    { 0x4, {0xFF, 0xFF, 0xFF, 0xFF}, {0x40, 0x0f, 0xbe, 0xce} },			// movsbl %sil, %ecx
    { 0x2, {0xFF, 0x00}, {0x41, 0x00} },						// push %rXX
    { 0x2, {0xFF, 0x00}, {0x85, 0x00} },						// test %rX,%rX
    { 0x5, {0xF8, 0x00, 0x00, 0x00, 0x00}, {0xB8, 0x00, 0x00, 0x00, 0x00} },   // mov $imm, %reg
    { 0x3, {0xFF, 0xFF, 0x00}, {0xFF, 0x77, 0x00} },  // pushq $imm(%rdi)
    { 0x2, {0xFF, 0xFF}, {0x31, 0xC0} },						// xor %eax, %eax
    { 0x2, {0xFF, 0xFF}, {0x89, 0xF8} },			// mov %edi, %eax
    { 0x0 }
};
#endif

static Boolean codeMatchesInstruction(unsigned char *code, AsmInstructionMatch* instruction)
{
    Boolean match = true;
    
    size_t i;
    for (i=0; i<instruction->length; i++) {
        unsigned char mask = instruction->mask[i];
        unsigned char constraint = instruction->constraint[i];
        unsigned char codeValue = code[i];
        
        match = ((codeValue & mask) == constraint);
        if (!match) break;
    }
    
    return match;
}

#if defined(__i386__) || defined(__x86_64__)
static Boolean
eatKnownInstructions(
                     unsigned char	*code,
                     uint64_t		*newInstruction,
                     int				*howManyEaten,
                     char			*originalInstructions,
                     int				*originalInstructionCount,
                     uint8_t			*originalInstructionSizes )
{
    Boolean allInstructionsKnown = true;
    int totalEaten = 0;
    unsigned char* ptr = code;
    int remainsToEat = 5; // a JMP instruction takes 5 bytes
    int instructionIndex = 0;
    
    if (howManyEaten) *howManyEaten = 0;
    if (originalInstructionCount) *originalInstructionCount = 0;
    while (remainsToEat > 0) {
        Boolean curInstructionKnown = false;
        
        // See if instruction matches one  we know
        AsmInstructionMatch* curInstr = possibleInstructions;
        do {
            if ((curInstructionKnown = codeMatchesInstruction(ptr, curInstr))) break;
            curInstr++;
        } while (curInstr->length > 0);
        
        // if all instruction matches failed, we don't know current instruction then, stop here
        if (!curInstructionKnown) {
            fprintf(stderr," ADDRESS PROBLEM mask \n");
            printf("%s\n", curInstr->mask);
            fprintf(stderr," ADDRESS PROBLEM constraint \n");
            printf("%s\n", curInstr->constraint);
            fprintf(stderr," ADDRESS PROBLEM ptr \n");
            printf("%x\n", (int)newInstruction);
            printf("%s\n", originalInstructions);
            
            allInstructionsKnown = false;
            fprintf(stderr, "mach_override: some instructions unknown! Need to update override\n");
            break;
        }
        
        // At this point, we've matched curInstr
        int eaten = curInstr->length;
        ptr += eaten;
        remainsToEat -= eaten;
        totalEaten += eaten;
        
        if (originalInstructionSizes) originalInstructionSizes[instructionIndex] = eaten;
        instructionIndex += 1;
        if (originalInstructionCount) *originalInstructionCount = instructionIndex;
    }
    
    
    if (howManyEaten) *howManyEaten = totalEaten;
    
    if (originalInstructions) {
        Boolean enoughSpaceForOriginalInstructions = (totalEaten < kOriginalInstructionsSize);
        
        if (enoughSpaceForOriginalInstructions) {
            memset(originalInstructions, 0x90 /* NOP */, kOriginalInstructionsSize); // fill instructions with NOP
            bcopy(code, originalInstructions, totalEaten);
        } else {
            // printf ("Not enough space in island to store original instructions. Adapt the island definition and kOriginalInstructionsSize\n");
            return false;
        }
    }
    
    if (allInstructionsKnown) {
        // save last 3 bytes of first 64bits of codre we'll replace
        uint64_t currentFirst64BitsOfCode = *((uint64_t *)code);
        currentFirst64BitsOfCode = OSSwapInt64(currentFirst64BitsOfCode); // back to memory representation
        currentFirst64BitsOfCode &= 0x0000000000FFFFFFLL;
        
        // keep only last 3 instructions bytes, first 5 will be replaced by JMP instr
        *newInstruction &= 0xFFFFFFFFFF000000LL; // clear last 3 bytes
        *newInstruction |= (currentFirst64BitsOfCode & 0x0000000000FFFFFFLL); // set last 3 bytes
    }
    
    return allInstructionsKnown;
}

static void
fixupInstructions(
                  void		*originalFunction,
                  void		*escapeIsland,
                  void		*instructionsToFix,
                  int			instructionCount,
                  uint8_t		*instructionSizes )
{
    int	index;
    for (index = 0;index < instructionCount;index += 1)
    {
        if (*(uint8_t*)instructionsToFix == 0xE9) // 32-bit jump relative
        {
            uint32_t offset = (uintptr_t)originalFunction - (uintptr_t)escapeIsland;
            uint32_t *jumpOffsetPtr = (uint32_t*)((uintptr_t)instructionsToFix + 1);
            *jumpOffsetPtr += offset;
        }
        
        originalFunction = (void*)((uintptr_t)originalFunction + instructionSizes[index]);
        escapeIsland = (void*)((uintptr_t)escapeIsland + instructionSizes[index]);
        instructionsToFix = (void*)((uintptr_t)instructionsToFix + instructionSizes[index]);
    }
}
#endif

#if defined(__i386__)
__asm(
      ".text;"
      ".align 2, 0x90;"
      "_atomic_mov64:;"
      "	pushl %ebp;"
      "	movl %esp, %ebp;"
      "	pushl %esi;"
      "	pushl %ebx;"
      "	pushl %ecx;"
      "	pushl %eax;"
      "	pushl %edx;"
      
      // atomic push of value to an address
      // we use cmpxchg8b, which compares content of an address with 
      // edx:eax. If they are equal, it atomically puts 64bit value 
      // ecx:ebx in address. 
      // We thus put contents of address in edx:eax to force ecx:ebx
      // in address
      "	mov		8(%ebp), %esi;"  // esi contains target address
      "	mov		12(%ebp), %ebx;"
      "	mov		16(%ebp), %ecx;" // ecx:ebx now contains value to put in target address
      "	mov		(%esi), %eax;"
      "	mov		4(%esi), %edx;"  // edx:eax now contains value currently contained in target address
      "	lock; cmpxchg8b	(%esi);" // atomic move.
      
      // restore registers
      "	popl %edx;"
      "	popl %eax;"
      "	popl %ecx;"
      "	popl %ebx;"
      "	popl %esi;"
      "	popl %ebp;"
      "	ret"
      );
#elif defined(__x86_64__)
void atomic_mov64(
                  uint64_t *targetAddress,
                  uint64_t value )
{
    *targetAddress = value;
}
#endif
#endif
