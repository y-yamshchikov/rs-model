#include "common.h"
#include <pthread.h>
#include <mutex>

typedef std::mutex CrstStatic;

class CrstHolder
{
public:
	CrstHolder(CrstStatic* st)
	{
		m_pMutex = st;
		m_pMutex->lock();
	}

	~CrstHolder()
	{
		m_pMutex->unlock();
	}
private:
	std::mutex *m_pMutex;
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
    volatile int count;
    int last_used_index;
    //RangeSectionHandle array[1];
    RangeSectionHandle *array;

    RangeSectionHandleHeader():
        size(0),
        capacity(0),
        count(0),
        last_used_index(-1),
        array(nullptr)
    {}
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

    DWORD               flags;

    // union
    // {
    //    PTR_CodeHeap    pCodeHeap;    // valid if RANGE_SECTION_HEAP is set
    //    PTR_Module      pZapModule;   // valid if RANGE_SECTION_HEAP is not set
    // };
    TADDR           pHeapListOrZapModule;
#if defined(HOST_64BIT)
    PTR_UnwindInfoTable pUnwindInfoTable; // Points to unwind information for this memory range.
#endif // defined(HOST_64BIT)
    PTR_RangeSection pNextPendingDeletion;
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
//#define _DEBUG
#ifndef _DEBUG
        RangeSectionHandleArrayInitialSize = 100,
        RangeSectionHandleArrayExpansionFactor = 2
#else
        RangeSectionHandleArrayInitialSize = 8,
	RangeSectionHandleArrayIncrement = 1
#endif //(_DEBUG)
//#undef _DEBUG
    };

    static int FindRangeSectionHandleHelper(RangeSectionHandleHeader *h, TADDR addr);

    static int EncodeRangeSectionIndex(int index)
    {
        return -(index+1);
    }

    static void check_writer_array();
    static int DecodeRangeSectionIndex(int codedIndex)
    {
        return -codedIndex - 1;
    }

public:
    static void Init();
    static void Reinit();
    static void DumpReaderArray(void); //NOT THREADSAFE
    static void DumpWriterArray(void); //NOT THREADSAFE

    class ReaderLockHolder
    {
    public:
        ReaderLockHolder(bool allowHostCalls);
        ~ReaderLockHolder();
	RangeSectionHandleHeader *h;
	bool m_allowHostCalls;
    };

    class ForbidDeletionHolder
    {
    public:
        ForbidDeletionHolder();
        ~ForbidDeletionHolder();
    };


    static void           AddCodeRange(TADDR StartRange, TADDR EndRange,
                                       IJitManager* pJit,
                                       RangeSection::RangeSectionFlags flags,
                                       void * pHp);

    static void            AddRangeSection(RangeSection *pRS);

    static void           DeleteRangeSection(RangeSectionHandleHeader *wh, RangeSectionHandleHeader *rh, int index);

    static void           DeleteRange(TADDR StartRange);


    static RangeSection* GetRangeSection(TADDR addr);
private:

    static CrstStatic       m_RangeCrst;        // Aquire before writing into m_CodeRangeList and m_DataRangeList

    //positive values block a deleter from substitution rh <- wh
    //deleter can store -1 if there is 0, signalling FDH constructor
    //to wait a few cycles while deleter substitutes rh <- wh
    static volatile LONG m_dwForbidDeletionCounter;

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
        WriterLockHolder(int purpose = 0); //0 for add, 1 for delete
        ~WriterLockHolder();
        RangeSectionHandleHeader *h;
	int m_purpose;
    };


    static void AddRangeHelper(TADDR StartRange,
                               TADDR EndRange,
                               IJitManager* pJit,
                               RangeSection::RangeSectionFlags flags,
                               TADDR pHeapListOrZapModule);
};
