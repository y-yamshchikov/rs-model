#include "codeman.h"
#include <sched.h>
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <cstring>
#include <cstdio>

#define DACCESS_COMPILE

#define _ASSERTE(...)
#define NOINLINE
void debug_trap2(void)
{
	sleep(1);
}
void ExecutionManager::check_writer_array()
{
	if(m_RangeSectionHandleWriterHeader&&m_RangeSectionHandleWriterHeader->size&&(!m_RangeSectionHandleWriterHeader->array[0].pRS))
		debug_trap2();
}
template<typename To, typename From>
To dac_cast(From arg)
{
	return reinterpret_cast<To>(arg);
}

//Volatile<RangeSectionHandle *> ExecutionManager::m_RangeSectionHandleArray = nullptr;
volatile RangeSectionHandleHeader*  ExecutionManager::m_RangeSectionHandleReaderHeader = nullptr;
volatile RangeSectionHandleHeader*  ExecutionManager::m_RangeSectionHandleWriterHeader = nullptr;
volatile RangeSection*  ExecutionManager::m_RangeSectionPendingDeletion = nullptr;
volatile LONG ExecutionManager::m_dwForbidDeletionCounter = 0; //CLR_TODO remove plain volatile and perform VolatileLoad/Store
//Volatile<int> ExecutionManager::m_LastUsedRSIndex = -1;
//Volatile<SIZE_T> ExecutionManager::m_RangeSectionArraySize = 0;
//Volatile<SIZE_T> ExecutionManager::m_RangeSectionArrayCapacity = 0;
//Volatile<LONG> ExecutionManager::m_dwReaderCount = 0;
//Volatile<LONG> ExecutionManager::m_dwWriterLock = 0;

CrstStatic ExecutionManager::m_RangeCrst;

//---------------------------------------------------------------------------------------
//
// ReaderLockHolder::ReaderLockHolder takes the reader lock, checks for the writer lock
// and either aborts if the writer lock is held, or yields until the writer lock is released,
// keeping the reader lock.  This is normally called in the constructor for the
// ReaderLockHolder.
//
// The writer cannot be taken if there are any readers. The WriterLockHolder functions take the
// writer lock and check for any readers. If there are any, the WriterLockHolder functions
// release the writer and yield to wait for the readers to be done.

template<typename T>
T InterlockedCompareExchangeT(T* ptr, T newval, T oldval)
{
	return __sync_val_compare_and_swap(ptr, oldval, newval);
}

ExecutionManager::ForbidDeletionHolder::ForbidDeletionHolder()
{
    while(true)
    {
        LONG c = m_dwForbidDeletionCounter; //VolatileLoad
        if (c >= 0)
        {
            if (c == InterlockedCompareExchangeT((LONG*)&m_dwForbidDeletionCounter, c+1, c))
                break; //now we are blocking rh<-wh substitution performing by deleters
                       //deleters are WriterLockHolder destructors for wlhs constructed
                       //with deletion purpose (see WriterLockHolder constructor)
        }
    }
}

ExecutionManager::ForbidDeletionHolder::~ForbidDeletionHolder()
{
    LONG c;
    do
    {
        c = m_dwForbidDeletionCounter; //VolatileLoad
    } while (c != InterlockedCompareExchangeT((LONG*)&m_dwForbidDeletionCounter, c-1, c));
    //once m_ForbidDeletionCounter appears to be 0 a deleter can store here -1
    //just to perform rh<-wh substitution and instantly
    //stores 0 back making FDH constructors wait just for a few cycles
}

ExecutionManager::ReaderLockHolder::ReaderLockHolder(bool allowHostCalls = true): m_allowHostCalls(allowHostCalls)
{
    //IncCantAllocCount();

    //FastInterlockIncrement(&m_dwReaderCount);

    //EE_LOCK_TAKEN(GetPtrForLockContract());

    int count;
BEGIN:
    h = (RangeSectionHandleHeader*)m_RangeSectionHandleReaderHeader;
INCREMENT:
    count = h->count;
    if (count == 0)
        goto BEGIN;
    if (count != InterlockedCompareExchangeT((int*)(&(h->count)), count+1, count))
        goto INCREMENT;
}
//---------------------------------------------------------------------------------------
//
// See code:ExecutionManager::ReaderLockHolder::ReaderLockHolder. This just decrements the reader count.

ExecutionManager::ReaderLockHolder::~ReaderLockHolder()
{
	static int cumulative = 0;
//    FastInterlockDecrement(&m_dwReaderCount);
    int count;

    do
    {
        count = h->count;
    }
    while (count != InterlockedCompareExchangeT((int*)&(h->count), count-1, count));

    if (count == 1)// h->count == 0
    {
        //we are here in relatively rare case
        //such code executes once per swap of arrays,
        //so we should not worry about enormous amount
        //of m_RangeSectionPendingDeletion accesses
        if (m_allowHostCalls)
        {
            while((RangeSection*)m_RangeSectionPendingDeletion != NULL)
            {
                RangeSection* pNext = ((RangeSection*)m_RangeSectionPendingDeletion)->pNextPendingDeletion;
#if defined(TARGET_AMD64)
                if (((RangeSection*)m_RangeSectionPendingDeletion)->pUnwindInfoTable != 0)
                    delete ((RangeSection*)m_RangeSectionPendingDeletion)->pUnwindInfoTable;
#endif // defined(TARGET_AMD64)
                delete (RangeSection*)m_RangeSectionPendingDeletion;
                m_RangeSectionPendingDeletion = pNext;
            }
        }

        m_RangeSectionHandleWriterHeader = h;
    }

    //DecCantAllocCount();

    //EE_LOCK_RELEASED(GetPtrForLockContract());
}

BOOL SwitchToThread(VOID)
{
	BOOL ret;
	
	/* sched_yield yields to another thread in the current process. This implementation
	 *        won't work well for cross-process synchronization. */
	ret = (sched_yield() == 0);
	return ret;
}

BOOL __SwitchToThread (DWORD dwSleepMSec, DWORD dwSwitchCount)
{
	if (dwSleepMSec > 0)
	{
		usleep(dwSleepMSec);
		return TRUE;
	}
	    
#ifdef TARGET_ARM
#define SLEEP_START_THRESHOLD (5 * 1024)
#else
#define SLEEP_START_THRESHOLD (32 * 1024)
#endif
	if (dwSwitchCount >= SLEEP_START_THRESHOLD)
	{
		usleep(1000); //one millisecond sleep
	}
	return SwitchToThread();
}
//---------------------------------------------------------------------------------------
//

//We should not hold reader lock and then hold writer locks
//in same thread before releasing reader lock:
//it results in a deadlock when writer lock will wait for
//old copy holding by the reader
//which will never be returned on writer's spot
ExecutionManager::WriterLockHolder::WriterLockHolder(int purpose): m_purpose(purpose)
{
    // Signal to a debugger that this thread cannot stop now
    //IncCantStopCount();

    //IncCantAllocCount();

    DWORD dwSwitchCount = 0;

    while (true)
    {
        //Thread::IncForbidSuspendThread();
        h = (RangeSectionHandleHeader*)m_RangeSectionHandleWriterHeader;

        if (!((h == nullptr)||(h->count != 0)))
            break;

        //Thread::DecForbidSuspendThread();
        __SwitchToThread(0, ++dwSwitchCount);
    }

    //EE_LOCK_TAKEN(GetPtrForLockContract());
}

ExecutionManager::WriterLockHolder::~WriterLockHolder()
{
    int count;
    RangeSectionHandleHeader *old_rh = (RangeSectionHandleHeader*)m_RangeSectionHandleReaderHeader;
    h->count = 1; //EM's unit

    if (m_purpose == 1)//deletion 
    {
        DWORD dwSwitchCount = 0;
        LONG fdc;
        while (true)	
        {
            fdc = m_dwForbidDeletionCounter;//VolatileLoad
            if (fdc == 0)
            {
                if (0 == InterlockedCompareExchangeT((LONG*)&(m_dwForbidDeletionCounter), (LONG) -1, (LONG)0))
                {
                    m_RangeSectionHandleReaderHeader = h; //VolatileStore
                    m_dwForbidDeletionCounter = 0; //VolatileStore 
                    break;
                }
	    }
            __SwitchToThread(0, ++dwSwitchCount);
        }
    }
    else
    {
        m_RangeSectionHandleReaderHeader = h; //VolatileStore
    }

    //removing EM's unit from old reader's header, so now latest leaving
    //user will attach the header to writer's slot
    do
    {
        count = old_rh->count; //VolatileLoad
    }
    while (count != InterlockedCompareExchangeT((int*)&(old_rh->count), count-1, count));

    if (count == 1)// h->count == 0, all readers left, we've just removed EM's unit,
                   //now attach the header to writer's slot:
    {
        while((RangeSection*)m_RangeSectionPendingDeletion != NULL)
        {
            RangeSection* pNext = ((RangeSection*)m_RangeSectionPendingDeletion)->pNextPendingDeletion;
#if defined(TARGET_AMD64)
            if (((RangeSection*)m_RangeSectionPendingDeletion)->pUnwindInfoTable != 0)
                delete ((RangeSection*)m_RangeSectionPendingDeletion)->pUnwindInfoTable;
#endif // defined(TARGET_AMD64)
            delete (RangeSection*)m_RangeSectionPendingDeletion;
            m_RangeSectionPendingDeletion = pNext;
        }

        m_RangeSectionHandleWriterHeader = old_rh; //VolatileStore
    }

    // Writer lock released, so it's safe again for this thread to be
    // suspended or have its stack walked by a profiler
    //Thread::DecForbidSuspendThread();

    //DecCantAllocCount();

    // Signal to a debugger that it's again safe to stop this thread
    //DecCantStopCount();

    //EE_LOCK_RELEASED(GetPtrForLockContract());
}


//**********************************************************************************
//  EEJitManager
//**********************************************************************************


// Init statics
void ExecutionManager::Init()
{
    //m_RangeCrst.Init(CrstExecuteManRangeLock, CRST_UNSAFE_ANYMODE);
}

void ExecutionManager::Reinit()//NOT THREADSAFE !!!
{
    //m_RangeCrst.Init(CrstExecuteManRangeLock, CRST_UNSAFE_ANYMODE);

	if (m_RangeSectionHandleReaderHeader == nullptr)
		return; //inited just now, no need to reinit

	std::vector<TADDR> lows;

	{//rlh's
		ReaderLockHolder rlh;
		RangeSectionHandleHeader *h = rlh.h;
		for (int i = 0; i < h->size; ++i) 
		{
			TADDR pCode = h->array[i].LowAddress;
			lows.push_back(pCode);
		}
		//rlh exists until scope's end
	}
	for(auto pCode: lows)
	{
		DeleteRange(pCode);
		//wlhs terminate on each return
		//and swaps arryas
	}
	//traverses delete list then terminates
	if (m_RangeSectionHandleReaderHeader)
	        delete[] (char*)m_RangeSectionHandleReaderHeader;
	if (m_RangeSectionHandleWriterHeader)
	        delete[] (char*)m_RangeSectionHandleWriterHeader;
	ExecutionManager::m_RangeSectionHandleReaderHeader = nullptr;
	ExecutionManager::m_RangeSectionHandleWriterHeader = nullptr;
	ExecutionManager::m_RangeSectionPendingDeletion = nullptr;
}

RangeSection* ExecutionManager::GetRangeSection(TADDR addr)
{
    if (m_RangeSectionHandleReaderHeader == nullptr)
    {
        return NULL;
    }

    ReaderLockHolder rlh(false); //false here is for test, should be properly wired, TODO
    RangeSectionHandleHeader *rh = rlh.h;
#ifndef DACCESS_COMPILE
    int LastUsedRSIndex = rh->last_used_index;
    if (LastUsedRSIndex != -1)
    {
        RangeSection* pRS = rh->array[LastUsedRSIndex].pRS;
        TADDR LowAddress = pRS->LowAddress;
        TADDR HighAddress = pRS->HighAddress;

        //positive case
        if ((addr >= LowAddress) && (addr < HighAddress))
	{
            return pRS;
	}

        //negative case
        if ((addr < LowAddress) && (LastUsedRSIndex == 0))
	{
            return NULL;
	}
    }
    if ((LastUsedRSIndex > 0)
        && (addr < rh->array[LastUsedRSIndex].LowAddress)
        && (addr >= rh->array[LastUsedRSIndex-1].pRS->HighAddress))
    {
            return NULL;
    }
#endif //DACCESS_COMPILE

    int foundIndex = FindRangeSectionHandleHelper(rh, addr);

#ifndef DACCESS_COMPILE
    if (foundIndex >= 0)
    {
        LastUsedRSIndex = foundIndex;
    }
    else
    {
        LastUsedRSIndex = (int)(rh->size) - 1;
    }

//    if (g_SystemInfo.dwNumberOfProcessors < 4 || !GCHeapUtilities::IsServerHeap() || !GCHeapUtilities::IsGCInProgress())
    rh->last_used_index = LastUsedRSIndex;
#endif //DACCESS_COMPILE

    return (foundIndex>=0)?rh->array[foundIndex].pRS:NULL;
}

/*********************************************************************/
// This function returns coded index: a number to be converted 
// to an actual index of RangeSectionHandleArray. Coded index uses
// non-negative values to store actual index of an element already
// existed in the array or it uses negative values to code an index
// to be used for placing a new element to the array. If value
// returned from FindRangeSectionHandleHelper is not negative, it can
// be immediately used to read an existing element like this:
// pRS = RangeSectionHandleArray[non_negative].pRS;
// If the value becomes negative then there is no element with
// requested TADDR in the RangeSectionHandleArray, and the value
// might be decoded to an actual index of RangeSectionHandleArray cell
// to place new RangeSectionHandle (if intended).
// The function to decode is DecodeRangeSectionIndex that takes
// negative coded index and returns an actual index.
//
int ExecutionManager::FindRangeSectionHandleHelper(RangeSectionHandleHeader *h, TADDR addr)
{
    RangeSectionHandle *array = h->array;

    _ASSERTE(array != nullptr);
    _ASSERTE(h->capacity != (SIZE_T)0);

    if ((int)h->size == 0)
    {
        return EncodeRangeSectionIndex(0);
    }

    int iLow = 0;
    int iHigh = (int)h->size - 1;
    int iMid = (iHigh + iLow)/2;

    if(addr < array[0].LowAddress)
    {
        return EncodeRangeSectionIndex(0);
    }

    if (addr >= array[iHigh].pRS->HighAddress)
    {
        return EncodeRangeSectionIndex(iHigh + 1);
    }

    do
    {
        if (addr < array[iMid].LowAddress)
        {
            iHigh = iMid;
            iMid = (iHigh + iLow)/2;
        }
        else if (addr >= array[iMid+1].LowAddress)
        {
            iLow = iMid + 1;
            iMid = (iHigh + iLow)/2;
        }
        else
        {
            iLow = iHigh = iMid;
        }
    } while(iHigh > iLow);

    if ((addr >= array[iHigh].LowAddress)
            && (addr < array[iHigh].pRS->HighAddress))
    {
        return iHigh;
    }
    else
    {
        return EncodeRangeSectionIndex(iHigh + 1);
    }
}

/* NGenMem depends on this entrypoint */
NOINLINE
void ExecutionManager::AddCodeRange(TADDR          pStartRange,
                                    TADDR          pEndRange,
                                    IJitManager *  pJit,
                                    RangeSection::RangeSectionFlags flags,
                                    void *         pHp)
{
    AddRangeHelper(pStartRange,
                   pEndRange,
                   pJit,
                   flags,
                   dac_cast<TADDR>(pHp));
}

void ExecutionManager::AddRangeSection(RangeSection *pRS)
{
    CrstHolder ch(&m_RangeCrst);
    if (m_RangeSectionHandleReaderHeader == nullptr)
    {
        //initial call, create array pair and initialize their headers
        m_RangeSectionHandleWriterHeader = new RangeSectionHandleHeader();

        m_RangeSectionHandleWriterHeader->capacity = RangeSectionHandleArrayInitialSize;
        m_RangeSectionHandleWriterHeader->array = new RangeSectionHandle[m_RangeSectionHandleWriterHeader->capacity];

        m_RangeSectionHandleWriterHeader->size = 1; 
        m_RangeSectionHandleWriterHeader->array[0].LowAddress = pRS->LowAddress;
        m_RangeSectionHandleWriterHeader->array[0].pRS = pRS; 
        m_RangeSectionHandleWriterHeader->count = 0;
	//TODO what about GC lastused usage condition?
	m_RangeSectionHandleWriterHeader->last_used_index = 0;

        RangeSectionHandleHeader *rh =  new RangeSectionHandleHeader();
	rh->capacity = m_RangeSectionHandleWriterHeader->capacity; 
	rh->size = m_RangeSectionHandleWriterHeader->size;
        rh->count = 1; //EM's unit
	rh->last_used_index = m_RangeSectionHandleWriterHeader->last_used_index;
	rh->array = new RangeSectionHandle[rh->capacity];
	
        rh->array[0] = m_RangeSectionHandleWriterHeader->array[0];

        m_RangeSectionHandleReaderHeader  = rh;
	return;
    }

    //rh and wh there are not null, so we can and should lock the wh
    WriterLockHolder wlh;
    RangeSectionHandleHeader *rh = (RangeSectionHandleHeader *)m_RangeSectionHandleReaderHeader;
    RangeSectionHandleHeader *wh = wlh.h; 
    if (wh->capacity < (rh->size+1)) //can't add to writer's array, expand
    {
        //reallocate array
        delete[] wh->array;
	if ((rh->size+1) > rh->capacity)
	{
//#define _DEBUG
#ifdef _DEBUG
        	wh->capacity = rh->capacity + RangeSectionHandleArrayIncrement;
#else
        	wh->capacity = rh->capacity * RangeSectionHandleArrayExpansionFactor;
#endif //_DEBUG
//#undef _DEBUG
	}
	else
	{
		wh->capacity = rh->capacity;
	}
        //some of readers can hold non-captured (with no corresponding increment of count)
        //link to this header,
        //can read count and even can try to ILCE if they occasionly
        //read non-zero count before being suspended by OS
	//so we have to maintain count = 0 until wlh destructor
        //thoroughly do his job of interchanging header pointers
        wh->array = new RangeSectionHandle[wh->capacity];
        wh->size = rh->size;
	//TODO what about GC lastused usage condition?
        wh->last_used_index = rh->last_used_index;
    }
    else
    {
        wh->size = rh->size;
        wh->last_used_index = -1;
    }

    //where to add?
    SIZE_T size = wh->size;
    if ((size == 0) || (pRS->LowAddress >= rh->array[size - 1].pRS->HighAddress))
    {
        if (size > 0)
	{
            memcpy((void*)(wh->array), (const void*)(rh->array), wh->size*sizeof(RangeSectionHandle));
	}
        wh->array[size].LowAddress = pRS->LowAddress;
        wh->array[size].pRS = pRS;
        wh->size = size + 1;
        //last used index was not changed by this operation
        return; //assume that ~WriterLockHolder() executes before ~CrstHolder()
    }

    int index = FindRangeSectionHandleHelper(rh, pRS->LowAddress);
    if (index < 0)
    {
        index = DecodeRangeSectionIndex(index);
        //shift and push
        memcpy(wh->array, rh->array, index*sizeof(RangeSectionHandle));
        memcpy(wh->array+index+1, rh->array+index, (size-index)*sizeof(RangeSectionHandle));
        wh->array[index].LowAddress = pRS->LowAddress;
        wh->array[index].pRS = pRS;
        wh->size = size + 1;
	//TODO what about GC lastused usage condition?
        if (rh->last_used_index >= index)
            wh->last_used_index = rh->last_used_index + 1;//aware of readers just running rh
        return;
    }
    return;
}

void ExecutionManager::DeleteRangeSection(RangeSectionHandleHeader *wh, RangeSectionHandleHeader *rh, int index)
{

    if (index < 0)
    {
        return;
    }

    //RangeSection storage guarantees we have reader's and writer's count differ at most 1
    _ASSERTE(wh->size+1 >= rh->size);

    wh->size = rh->size-1;

    //copying header + elements until deleted
    memcpy((void*)wh->array, (const void*)rh->array, index*sizeof(RangeSectionHandle));
    memcpy((void*)(wh->array+index), (const void*)(rh->array+index+1), (rh->size-index-1)*sizeof(RangeSectionHandle));

    int LastUsedRSIndex = rh->last_used_index;
    if (LastUsedRSIndex > index)
        LastUsedRSIndex--;
    else if (LastUsedRSIndex == index)
        LastUsedRSIndex = wh->size - 1;
    
    //TODO what about GC lastused usage condition?
    wh->last_used_index = LastUsedRSIndex;
}

void ExecutionManager::AddRangeHelper(TADDR          pStartRange,
                                      TADDR          pEndRange,
                                      IJitManager *  pJit,
                                      RangeSection::RangeSectionFlags flags,
                                      TADDR          pHeapListOrZapModule)
{
    RangeSection *pnewrange = new RangeSection;

    _ASSERTE(pEndRange > pStartRange);

    pnewrange->LowAddress  = pStartRange;
    pnewrange->HighAddress = pEndRange;
    pnewrange->pjit        = pJit;
    pnewrange->flags       = flags;
    pnewrange->pHeapListOrZapModule = pHeapListOrZapModule;
#if defined(TARGET_AMD64)
    pnewrange->pUnwindInfoTable = NULL;
#endif // defined(TARGET_AMD64)
    AddRangeSection(pnewrange);
}

// Deletes a single range starting at pStartRange
void ExecutionManager::DeleteRange(TADDR pStartRange)
{
    {
        // Acquire the Crst before unlinking a RangeList.
        // NOTE: The Crst must be acquired BEFORE we grab the writer lock, as the
        // writer lock forces us into a forbid suspend thread region, and it's illegal
        // to enter a Crst after the forbid suspend thread region is entered
        CrstHolder ch(&m_RangeCrst);

        // Acquire the WriterLock and prevent any readers from walking the RangeList.
        // This also forces us to enter a forbid suspend thread region, to prevent
        // hijacking profilers from grabbing this thread and walking it (the walk may
        // require the reader lock, which would cause a deadlock).
        WriterLockHolder wlh(1); //1 signals that purpose of the lock is deletion
        RangeSectionHandleHeader *rh = (RangeSectionHandleHeader*)m_RangeSectionHandleReaderHeader;
	int index = FindRangeSectionHandleHelper(rh, pStartRange);

        if (index >= 0)
	{
            rh->array[index].pRS->pNextPendingDeletion = (RangeSection*)m_RangeSectionPendingDeletion;
            m_RangeSectionPendingDeletion = rh->array[index].pRS;
            DeleteRangeSection(wlh.h, rh, index);
	}
    }
}


void ExecutionManager::DumpReaderArray()
{
	printf("DumpReaderArray: beginning\n");
	RangeSectionHandleHeader *h = (RangeSectionHandleHeader*)m_RangeSectionHandleReaderHeader;
	if (h == nullptr)
	{
		printf("DumpReaderArray: no reader array\n");
		return;
	}

	printf("DumpReaderArray: Reader's header:\n");
	printf("DumpReaderArray: header's address = %08x:%08x\n", ((TADDR)h)>>32, h);
	printf("DumpReaderArray: size = %d\n", h->size);
	printf("DumpReaderArray: capacity = %d\n", h->capacity);
	printf("DumpReaderArray: count = %d\n", h->count);
	printf("DumpReaderArray: last_used_index = %d\n", h->last_used_index);
	printf("DumpReaderArray: elements:\n");

	for (int i = 0; i < h->size; ++i)
	{
		printf("DumpReaderArray: ======= %d =======\n", i);
		TADDR low = h->array[i].LowAddress;
		TADDR high;
		RangeSection* pRS = h->array[i].pRS;

		printf("DumpReaderArray: LowAddress = %08x:%08x\n", ((TADDR)low)>>32, low);
		printf("DumpReaderArray: pRS = %08x:%08x\n", ((TADDR)pRS)>>32, pRS);
		if (pRS != nullptr)
		{
			high = pRS->HighAddress;
			printf("DumpReaderArray: HighAddress = %08x:%08x\n", ((TADDR)high)>>32, high);
		}
	}
	fflush(stdout);
}
void ExecutionManager::DumpWriterArray()
{
	printf("DumpWriter: beginning\n");
	RangeSectionHandleHeader *h = (RangeSectionHandleHeader*)m_RangeSectionHandleWriterHeader;
	if (h == nullptr)
	{
		printf("DumpWriterArray: no writer array\n");
		return;
	}

	printf("DumpWriterArray: Writer's header:\n");
	printf("DumpWriterArray: header's address = %08x:%08x\n", ((TADDR)h)>>32, h);
	printf("DumpWriterArray: size = %d\n", h->size);
	printf("DumpWriterArray: capacity = %d\n", h->capacity);
	printf("DumpWriterArray: count = %d\n", h->count);
	printf("DumpWriterArray: last_used_index = %d\n", h->last_used_index);
	printf("DumpWriterArray: elements:\n");

	for (int i = 0; i < h->size; ++i)
	{
		printf("DumpWriterArray: ======= %d =======\n", i);
		TADDR low = h->array[i].LowAddress;
		TADDR high;
		RangeSection* pRS = h->array[i].pRS;

		printf("DumpWriterArray: LowAddress = %08x:%08x\n", ((TADDR)low)>>32, low);
		printf("DumpWriterArray: pRS = %08x:%08x\n", ((TADDR)pRS)>>32, pRS);
		if (pRS != nullptr)
		{
			high = pRS->HighAddress;
			printf("DumpWriterArray: HighAddress = %08x:%08x\n", ((TADDR)high)>>32, high);
		}
	}
	fflush(stdout);
}
