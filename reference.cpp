#include "tree.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
//#include "main.h"
//#include <time.h>
#include <vector>
//#define PRINTF printf
#define PRINTF(...) 
using namespace std;
bool ShowTreeNodeRanges(tree<unsigned int> *derevo);
int EncodeIndex(int index);
int DecodeIndex(int codedIndex);

int cmp(unsigned int left, unsigned int right)
{
	//PRINTF("compare: left %08x, right %08x\n", left, right);
	if (left < right) return -1;
	if (left > right) return 1;
	return 0;
}
tree<unsigned int> *g_tree = NULL;

struct RangeSection
{
	void *LowAddress;
	void *HighAddress;
	int data;
	void *more_data;
	long array_of_data[12];
};

struct RangeSectionHandle
{
	void *LowAddress;
	RangeSection *pRS;

	RangeSectionHandle()
	{
		LowAddress = 0;
		pRS = nullptr;
	}
};

struct Range
{
	void *LowAddress;
	void *HighAddress;
};

RangeSectionHandle *pRangeSectionHandleArray = nullptr;
size_t RangeSectionSize = 0;
size_t RangeSectionArrayCapacity = 0;

int FindRangeSectionHandleHelper(void *pCode)
{
	PRINTF("FindRangeSection 1: search for %08x\n", pCode);
	if (pRangeSectionHandleArray == nullptr)	
	{
		printf("error\n");
		exit(1);
	}
	PRINTF("FindRangeSection 2\n");
	if (RangeSectionSize == 0)
	{
		return EncodeIndex(0); //add before 1 i.e. to 0
		//return -1;
	}

	PRINTF("FindRangeSection 3\n");
	int iLow = 0;
	int iHigh = RangeSectionSize - 1;
	int iMid = (iHigh + iLow)/2;

	if (pCode < pRangeSectionHandleArray[iLow].LowAddress)
	{
		return EncodeIndex(0);
	}
	if (pCode >= pRangeSectionHandleArray[iHigh].pRS->HighAddress)
	{
		return EncodeIndex(iHigh+1);
	}

	do
	{
		PRINTF("FindRangeSection 4: iMid=%d\n", iMid);
		if (pCode < pRangeSectionHandleArray[iMid].LowAddress)
		{
			PRINTF("FindRangeSection 5\n");
			iHigh = iMid;
			iMid = (iHigh + iLow)/2;
		}
		else if (pCode >= pRangeSectionHandleArray[iMid+1].LowAddress)//pCode >= iMid.LowAddress
		{
			PRINTF("FindRangeSection 8\n");
			iLow = iMid + 1;
			iMid = (iHigh + iLow)/2;
		}
		else
		{
			iLow=iHigh=iMid;
		}
		PRINTF("FindRangeSection 9: iLow=%d, iMid=%d, iHigh=%d\n", iLow, iMid, iHigh);

	} while(iHigh > iLow);
	PRINTF("FindRangeSection 10\n");
	//here iHigh == iLow
	//here iHigh >= 0
	if ((pCode >= pRangeSectionHandleArray[iHigh].LowAddress)
		&& (pCode < pRangeSectionHandleArray[iHigh].pRS->HighAddress))
	{
		return iHigh;
	}
	else
	{
		return EncodeIndex(iHigh+1);
	}
}

int EncodeIndex(int index)
{
	return -(index + 1);
}
int DecodeIndex(int codedIndex)
{
	return (-codedIndex) - 1;
}

RangeSection *FindRangeSection(void *pCode)
{
	if (pRangeSectionHandleArray == nullptr)
		return nullptr;
	int RSIndex = FindRangeSectionHandleHelper(pCode);
	if (RSIndex < 0)
		return nullptr;
	return pRangeSectionHandleArray[RSIndex].pRS;
}

void  AddRangeSection(RangeSection *pRS)
{
			PRINTF("AddRangeSection 1: adding %08x\n", pRS->LowAddress);
	//int c = getchar();
	if (pRangeSectionHandleArray == nullptr)
	{
		int cap = 8;
		pRangeSectionHandleArray = new RangeSectionHandle[cap];
		RangeSectionArrayCapacity = cap;
		RangeSectionSize = 0;
	}

			PRINTF("AddRangeSection 2\n");
	if (RangeSectionSize == RangeSectionArrayCapacity)
	{
		//reallocate array
		RangeSectionArrayCapacity = (size_t)RangeSectionArrayCapacity*2;
		RangeSectionHandle *tmp = new RangeSectionHandle[RangeSectionArrayCapacity];
		memcpy(tmp, pRangeSectionHandleArray, RangeSectionSize*sizeof(RangeSectionHandle));
		delete[] pRangeSectionHandleArray;
		pRangeSectionHandleArray = tmp;
	}

			PRINTF("AddRangeSection 3\n");
	//where to add?
    size_t size = RangeSectionSize;
    if ((size == 0) || (pRS->LowAddress >= pRangeSectionHandleArray[size - 1].pRS->HighAddress))
    {
        pRangeSectionHandleArray[size].LowAddress = pRS->LowAddress;
        //pRangeSectionHandleArray[size].HighAddress = pRS->HighAddress;
        pRangeSectionHandleArray[size].pRS = pRS;
        RangeSectionSize = size + 1;
	return;
    }
	int index = FindRangeSectionHandleHelper(pRS->LowAddress);
			PRINTF("AddRangeSection 4\n");
	if (index < 0)
	{
		index = DecodeIndex(index);
			PRINTF("AddRangeSection 5\n");
		//push after index and shift remainings right
			PRINTF("AddRangeSection pRangeSectionHandleArray=%08x,index=%d, RangeSectionSize=%d\n", pRangeSectionHandleArray,index,RangeSectionSize);
		memmove(pRangeSectionHandleArray+index+1, pRangeSectionHandleArray+index, (RangeSectionSize-index)*sizeof(RangeSectionHandle));
			PRINTF("AddRangeSection 6\n");
		pRangeSectionHandleArray[index].LowAddress = pRS->LowAddress;
		//pRangeSectionHandleArray[index].HighAddress = pRS->HighAddress;
		pRangeSectionHandleArray[index].pRS = pRS;
		RangeSectionSize++;
		return;
	}
			PRINTF("AddRangeSection 7\n");
	return;
}

int /*retcode*/  DeleteRangeSection(RangeSection *pRS)
{
	if (pRangeSectionHandleArray == nullptr)
		return -1;
	
	//from where to delete?
	int index = FindRangeSectionHandleHelper(pRS->LowAddress);
	PRINTF("DeleteRangeSection: index = %d\n", index);
	if (index < 0)
		return -2;
	memmove(pRangeSectionHandleArray+index, pRangeSectionHandleArray+index+1, (RangeSectionSize-index-1)*sizeof(RangeSectionHandle));
	RangeSectionSize--;
	delete pRS;
}


int main(int argc, char* argv[])
{
/*	
	tree<unsigned int> derevo(0xf6704000);
	g_tree = &derevo;
	
	derevo.Add(0xf0918000, cmp);
	PRINTF("added 0xf0918000\n"); 

        derevo.Add(0xf5dbf000, cmp);
        PRINTF("added 0xf5dbf000\n"); 

        derevo.Add(0xf5d22000, cmp);
        PRINTF("added 0xf5d22000\n"); 

	unsigned int *found = derevo.Find(0xf5d22000, cmp);
	PRINTF("Found: %08x\n", *found);

        derevo.Add(0xf53c4000, cmp);
        PRINTF("added 0xf53c4000\n"); 



	PRINTF("\n");
	PRINTF("root: %08x\n", derevo.name);
	if (derevo.lChild)
	{
		PRINTF("root.left: %08x\n", derevo.lChild->name);
		if (derevo.lChild->lChild)
		{
	                PRINTF("root.left.left: %08x\n", derevo.lChild->lChild->name);
		}
		if (derevo.lChild->rChild)
		{
	                PRINTF("root.left.right: %08x\n", derevo.lChild->rChild->name);
		}
	}
	if (derevo.rChild)
	{
		PRINTF("root.right: %08x\n", derevo.rChild->name);
		if (derevo.rChild->lChild)
		{
	                PRINTF("root.right.left: %08x\n", derevo.rChild->lChild->name);
		}
		if (derevo.rChild->rChild)
		{
	                PRINTF("root.right.right: %08x\n", derevo.rChild->rChild->name);
		}
	}
*/
	const int epochs = 1000;	
	const int elems = 1000;	
	
	time_t t;
	srand((unsigned int) time(&t));

	for (int i = 0; i < epochs; ++i)
	{
		printf("i=%d\n", i);
		fflush(stdout);
		vector<unsigned int>  nums;
		vector<Range> ranges;
		vector<int> deleted_ranges;

		for (int n = 0; n < RangeSectionSize; ++n)
		{
			delete pRangeSectionHandleArray[n].pRS;
		}
		delete[] pRangeSectionHandleArray;
		RangeSectionSize = 0;
		RangeSectionArrayCapacity = 0;
		pRangeSectionHandleArray = nullptr;

		for (int n = 0; n < elems*2; n++)
		{
			unsigned int rnd;
			bool f = false;
			do
			{
				f = false;
				rnd = (rand()<<1) + rand()%2;
				for (auto x : nums)
				{
					if( rnd == x)
						f = true;
				} 
			}while ( f );
			nums.push_back(rnd);
		}

		sort(nums.begin(), nums.end());

		for (int n = 0; n < elems; n++)
		{
			Range elem;
			elem.LowAddress = (void*)nums[n*2];
			elem.HighAddress = (void*)nums[n*2+1];
			int index = rand()%(ranges.size() + 1);			
			vector<Range>::iterator it = (ranges.begin() + index);
			ranges.insert(it, elem);
		}

		for (int j = 0; j < elems; ++j)
		{
			PRINTF("j=%d\n", j);
			RangeSection *pRS = new RangeSection();
			pRS->LowAddress = ranges[j].LowAddress;
			PRINTF("3\n");
			pRS->HighAddress = ranges[j].HighAddress;
			PRINTF("4\n");
			PRINTF("adding low=%08x, high=%08x\n", pRS->LowAddress, pRS->HighAddress);
			AddRangeSection(pRS);

			if ((rand()%10 == 0))
			{
				DeleteRangeSection(pRS);
				PRINTF("deleting range, j == %d, low = %08x\n", j, ranges[j].LowAddress);
				fflush(stdout);
				deleted_ranges.push_back(j);
			}

			PRINTF("5\n");
			for (int k = 0; k <= j; ++k)
			{
				bool skip = false;
				for(auto in: deleted_ranges)
				{
					if (k == in)
					{
						skip = true;
						PRINTF("skipping k == %d\n", k);
						fflush(stdout);
						break;
					}
				}
				if (skip)
					continue;
				int random_delta = rand()%((unsigned long long)ranges[k].HighAddress - (unsigned long long)ranges[k].LowAddress - 1);
				//int noise = rand()%100 - 50;
				int noise = 0;
				void* pCode = (void*)((unsigned long long)ranges[k].LowAddress + random_delta + noise);
				RangeSection *pRS2 = FindRangeSection(pCode);
				if (!pRS2) 
				{
					printf("element not found, i = %d, j = %d, k = %d, elem = %08x, search for %08x\n", i, j, k, ranges[k].LowAddress, pCode);
					for (int n = 0; n < RangeSectionSize; ++n)
					{
						printf("%08x:%08x ", pRangeSectionHandleArray[n].LowAddress, pRangeSectionHandleArray[n].pRS->HighAddress);
					}
					printf("RangeSectionSize=%d", RangeSectionSize);
					printf("\n\n");
					fflush(stdout);
					return -1;
				}
				if(ranges[k].LowAddress != pRS2->LowAddress)
				{
					printf("section broken\n");
				}
				
				//if(ranges[k].HighAddress != pRS2->HighAddress)
				//{
				//	printf("section broken\n");
				//}
			}

		}
	}
	

	//PRINTF("RAND_MAX = %08x\n" ,RAND_MAX);
	printf("test succeeded\n");
	return 0;
}

bool ShowTreeNodeRanges(tree<unsigned int> *derevo)
{
	PRINTF("Show: Node: %08x\n", derevo->name);
	tree<unsigned int> *lc = derevo->lChild;
	tree<unsigned int > *rc = derevo->rChild;

	if (lc != NULL)
		PRINTF("    Show: LEFT: %08x\n", lc->name);
	if (rc != NULL)
		PRINTF("    Show: RIGHT: %08x\n", rc->name);

	if (lc != NULL)
	{
		PRINTF("Show: go left:\n");
		if (!ShowTreeNodeRanges(lc))
		{
			PRINTF("Show: go up\n");
			return false;
		}
	}

	if (rc != NULL)
	{
		PRINTF("Show: go right:\n");
		if (!ShowTreeNodeRanges(rc))
		{
			PRINTF("Show: go up\n");
			return false;
		}
	}

	PRINTF("Show: go up\n");
	return true;
}

void ShowTreeNodeRangesHelper()
{
	PRINTF("ShowHelper Start\n");
	ShowTreeNodeRanges(g_tree);
        PRINTF("ShowHelper End\n");
}


