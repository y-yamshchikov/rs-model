#include "common.h"
#include <pthread.h>
class CrstStatic
{
};

class CrstHolder
{
public:
	CrstHolder(CrstStatic* st)
	{
		//stub
	}
};

//-----------------------------------------------------------------------------
// The ExecutionManager uses RangeSection as the abstraction of a contiguous
// address range to track the code heaps.

#define DPTR(type) type*
typedef DPTR(struct RangeSection) PTR_RangeSection;
typedef DPTR(struct RangeSectionHandle) PTR_RangeSectionHandle;
typedef DPTR(struct RangeSectionHandleHeader) PTR_RangeSectionHandleHeader;

struct RangeSectionHandle
{
    TADDR LowAddress;
    PTR_RangeSection pRS;

    RangeSectionHandle()
    {
        LowAddress = 0;
        pRS = NULL;
    }
};

struct RangeSectionHandleHeader
{
    SIZE_T size;
    SIZE_T capacity;
    int count;
    int last_used_index;
    RangeSectionHandle array[1];
};

typedef void* PTR_IJitManager;
typedef void* PTR_UnwindInfoTable;
struct RangeSection
{
    TADDR               LowAddress;
    TADDR               HighAddress;

    PTR_IJitManager     pjit;           // The owner of this address range

    enum RangeSectionFlags
    {
        RANGE_SECTION_NONE          = 0x0,
        RANGE_SECTION_COLLECTIBLE   = 0x1,
        RANGE_SECTION_CODEHEAP      = 0x2,
#ifdef FEATURE_READYTORUN
        RANGE_SECTION_READYTORUN    = 0x4,
#endif
    };

    union
    {
        DWORD               flags;
	PTR_RangeSection pNextPendingDeletion;
    };

    // union
    // {
    //    PTR_CodeHeap    pCodeHeap;    // valid if RANGE_SECTION_HEAP is set
    //    PTR_Module      pZapModule;   // valid if RANGE_SECTION_HEAP is not set
    // };
    TADDR           pHeapListOrZapModule;
#if defined(HOST_64BIT)
    PTR_UnwindInfoTable pUnwindInfoTable; // Points to unwind information for this memory range.
#endif // defined(HOST_64BIT)
};



//*****************************************************************************
//
// This class manages IJitManagers and ICorJitCompilers.  It has only static
// members.  It should never be constucted.
//
//*****************************************************************************

class ExecutionManager
{

    enum
    {
#ifndef _DEBUG
        RangeSectionHandleArrayInitialSize = 100,
        RangeSectionHandleArrayExpansionFactor = 2
#else
        RangeSectionHandleArrayInitialSize = 8,
	RangeSectionHandleArrayIncrement = 1
#endif //(_DEBUG)
    };

    static int FindRangeSectionHandleHelper(RangeSectionHandleHeader *h, TADDR addr);

    static int EncodeRangeSectionIndex(int index)
    {
        return -(index+1);
    }

    static int DecodeRangeSectionIndex(int codedIndex)
    {
        return -codedIndex - 1;
    }

public:
    static void Init();
    static void Reinit();
    static void DumpReaderArray(void); //NOT THREADSAFE

    class ReaderLockHolder
    {
    public:
        ReaderLockHolder();
        ~ReaderLockHolder();
	RangeSectionHandleHeader *h;
    };


    static void           AddCodeRange(TADDR StartRange, TADDR EndRange,
                                       IJitManager* pJit,
                                       RangeSection::RangeSectionFlags flags,
                                       void * pHp);

    static void            AddRangeSection(RangeSection *pRS);

    static void           DeleteRangeSection(RangeSectionHandleHeader **pwh, RangeSectionHandleHeader *rh, int index);

    static void           DeleteRange(TADDR StartRange);


    static RangeSection* GetRangeSection(TADDR addr);
private:

    static CrstStatic       m_RangeCrst;        // Aquire before writing into m_CodeRangeList and m_DataRangeList

    //static Volatile<RangeSectionHandle *> m_RangeSectionHandleArray;
    //static Volatile<int> m_LastUsedRSIndex;
    //static Volatile<SIZE_T> m_RangeSectionArraySize;
    //static Volatile<SIZE_T> m_RangeSectionArrayCapacity;
    //static Volatile<LONG>   m_dwReaderCount;
    static volatile RangeSectionHandleHeader * m_RangeSectionHandleReaderHeader;
    static volatile RangeSectionHandleHeader * m_RangeSectionHandleWriterHeader;
    static volatile RangeSection*  m_RangeSectionPendingDeletion;
    //static Volatile<LONG>   m_dwWriterLock;

    class WriterLockHolder
    {
    public:
        WriterLockHolder();
        ~WriterLockHolder();
        RangeSectionHandleHeader *h;
    };


    static void AddRangeHelper(TADDR StartRange,
                               TADDR EndRange,
                               IJitManager* pJit,
                               RangeSection::RangeSectionFlags flags,
                               TADDR pHeapListOrZapModule);
};
