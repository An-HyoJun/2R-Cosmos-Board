//////////////////////////////////////////////////////////////////////////////////
// garbage_collection.c for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Garbage Collector
// File Name: garbage_collection.c
//
// Version: v1.0.0
//
// Description:
//   - select a victim block
//   - collect valid pages to a free block
//   - erase a victim block to make a free block
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include <assert.h>
#include "memory_map.h"

P_GC_VICTIM_MAP gcVictimMapPtr;
unsigned int gcount, additional_write;
extern unsigned int request_write;

void InitGcVictimMap()
{
	int dieNo;

	gcVictimMapPtr = (P_GC_VICTIM_MAP) GC_VICTIM_MAP_ADDR;

	additional_write = 0;
	request_write = 0;

	for(dieNo=0 ; dieNo<USER_DIES; dieNo++)
	{
		gcVictimMapPtr->gcVictimList[dieNo].headBlock = BLOCK_NONE;
		gcVictimMapPtr->gcVictimList[dieNo].tailBlock = BLOCK_NONE;
		gcVictimMapPtr->gcVictimList[dieNo].fifo_head = BLOCK_NONE;
		gcVictimMapPtr->gcVictimList[dieNo].fifo_tail = BLOCK_NONE;
		gcVictimMapPtr->gcVictimList[dieNo].normal_threshold = 30;
		gcVictimMapPtr->gcVictimList[dieNo].cold_threshold = 40;
	}

	gcount = 0;
}


void GarbageCollection(unsigned int dieNo)
{
	unsigned int victimBlockNo, eraseBlockNo, pageNo, virtualSliceAddr, logicalSliceAddr, dieNoForGcCopy, reqSlotTag;

	gcount++;

	victimBlockNo = GetFromGcVictimList(dieNo);
	if(victimBlockNo == BLOCK_FAIL){
		assert(!"no victim block");
	}
	dieNoForGcCopy = dieNo;

	while(victimBlockNo != BLOCK_NONE){
		if(virtualBlockMapPtr->block[dieNo][victimBlockNo].invalidSliceCnt != SLICES_PER_BLOCK){
			for(pageNo=0 ; pageNo<USER_PAGES_PER_BLOCK ; pageNo++){
				virtualSliceAddr = Vorg2VsaTranslation(dieNo, victimBlockNo, pageNo);
				logicalSliceAddr = virtualSliceMapPtr->virtualSlice[virtualSliceAddr].logicalSliceAddr;

				if(logicalSliceAddr != LSA_NONE){
					if(logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr ==  virtualSliceAddr){ //valid data
						//read
						reqSlotTag = GetFromFreeReqQ();

						reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
						reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
						reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = logicalSliceAddr;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_TEMP_ENTRY;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
						reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = AllocateTempDataBuf(dieNo);
						UpdateTempDataBufEntryInfoBlockingReq(reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry, reqSlotTag);
						reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

						SelectLowLevelReqQ(reqSlotTag);

						//write
						reqSlotTag = GetFromFreeReqQ();

						reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
						reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
						reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = logicalSliceAddr;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_TEMP_ENTRY;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
						reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
						reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = AllocateTempDataBuf(dieNo);
						UpdateTempDataBufEntryInfoBlockingReq(reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry, reqSlotTag);
						reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = FindFreeVirtualSliceForGc(dieNoForGcCopy, victimBlockNo);

						logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr = reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr;
						virtualSliceMapPtr->virtualSlice[reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr].logicalSliceAddr = logicalSliceAddr;

						SelectLowLevelReqQ(reqSlotTag);
						additional_write++;
					}
				}
			}
		}

		eraseBlockNo = victimBlockNo;
		victimBlockNo = virtualBlockMapPtr->block[dieNo][victimBlockNo].nextBlock;
		EraseBlock(dieNo, eraseBlockNo);
	}
	/*if(gcount == 1){
		xil_printf("%d %d %d\n", gcount, additional_write, request_write);
	}*/
}


void PutToGcVictimList(unsigned int dieNo, unsigned int blockNo)
{
	if(gcVictimMapPtr->gcVictimList[dieNo].tailBlock != BLOCK_NONE)
	{
		virtualBlockMapPtr->block[dieNo][blockNo].prevBlock = gcVictimMapPtr->gcVictimList[dieNo].tailBlock;
		virtualBlockMapPtr->block[dieNo][blockNo].nextBlock = BLOCK_NONE;
		virtualBlockMapPtr->block[dieNo][gcVictimMapPtr->gcVictimList[dieNo].tailBlock].nextBlock = blockNo;
		gcVictimMapPtr->gcVictimList[dieNo].tailBlock = blockNo;
	}
	else
	{
		virtualBlockMapPtr->block[dieNo][blockNo].prevBlock = BLOCK_NONE;
		virtualBlockMapPtr->block[dieNo][blockNo].nextBlock = BLOCK_NONE;
		gcVictimMapPtr->gcVictimList[dieNo].headBlock = blockNo;
		gcVictimMapPtr->gcVictimList[dieNo].tailBlock = blockNo;
		gcVictimMapPtr->gcVictimList[dieNo].fifo_head = blockNo;
	}
}

unsigned int GetFromGcVictimList(unsigned int dieNo)//2R++
{
	unsigned int i, j, first_victim_i, nextBlock, prevBlock, evictedBlockNo, fifo_end;
	unsigned int vi_list[USER_PAGES_PER_BLOCK];

	int used_normal = 0, used_cold = 0;
	int is_cold_merge = 0;
	int list_end = (int)((double)(USER_BLOCKS_PER_DIE - 2) * 0.8) - 1;//gc를 위한 free block과 이전 gc에서 생긴 cold block을 제외하기 위해 2를 뺌

	double normal_block_valid_ratio = (double)gcVictimMapPtr->gcVictimList[dieNo].normal_threshold * 0.01;
	double cold_block_valid_ratio = (double)gcVictimMapPtr->gcVictimList[dieNo].cold_threshold * 0.01;

	unsigned int head_loc = 0;
	i = gcVictimMapPtr->gcVictimList[dieNo].headBlock;
	for(int k = 0; k < list_end; k++){
		if(i == gcVictimMapPtr->gcVictimList[dieNo].fifo_head){
			head_loc = 1;
		}
		i = virtualBlockMapPtr->block[dieNo][i].nextBlock;
	}
	gcVictimMapPtr->gcVictimList[dieNo].fifo_tail = i;

	if(head_loc == 0){
		gcVictimMapPtr->gcVictimList[dieNo].fifo_head = gcVictimMapPtr->gcVictimList[dieNo].headBlock;
	}

	i = gcVictimMapPtr->gcVictimList[dieNo].fifo_head;

	if(gcVictimMapPtr->gcVictimList[dieNo].fifo_head == gcVictimMapPtr->gcVictimList[dieNo].headBlock){
		fifo_end = gcVictimMapPtr->gcVictimList[dieNo].fifo_tail;
	}
	else{
		fifo_end = virtualBlockMapPtr->block[dieNo][i].prevBlock;
	}

	while(1){
		unsigned int out = 0, sum = 0;
		i = gcVictimMapPtr->gcVictimList[dieNo].fifo_head;

		if(normal_block_valid_ratio < 0.1){
			normal_block_valid_ratio = 0.3;
		}
		if (cold_block_valid_ratio < 0.06) {
			cold_block_valid_ratio = 0.4;
		}

		while(1){//first vicitm
			double invalid_ratio = (double) virtualBlockMapPtr->block[dieNo][i].invalidSliceCnt / (double) USER_PAGES_PER_BLOCK;
			char cold_condition = invalid_ratio >= cold_block_valid_ratio;
			char normal_condition = invalid_ratio >= normal_block_valid_ratio;

			if(used_normal == 1){
				normal_condition = 0;
			}
			else if(used_cold == 1){
				cold_condition = 0;
			}

			if ((virtualBlockMapPtr->block[dieNo][i].cold == 2 && normal_condition) || (virtualBlockMapPtr->block[dieNo][i].cold == 1 && cold_condition) || (virtualBlockMapPtr->block[dieNo][i].cold == 0 && normal_condition)){
				first_victim_i = i;
				if(i != gcVictimMapPtr->gcVictimList[dieNo].fifo_tail){
					i = virtualBlockMapPtr->block[dieNo][i].nextBlock;
					if(i == BLOCK_NONE){
						assert(!"range over 1\n");
					}
				}
				else{
					i = gcVictimMapPtr->gcVictimList[dieNo].headBlock;
				}
				break;
			}

			if(i == fifo_end){
				if(normal_block_valid_ratio <= 0.02 && cold_block_valid_ratio <= 0.02){
					assert(!"no victim!\n");
				}

				if(cold_block_valid_ratio > 0.02){
					cold_block_valid_ratio -= 0.02;
				}
				if(normal_block_valid_ratio > 0.02){
					normal_block_valid_ratio -= 0.02;
				}
			}

			if(i == gcVictimMapPtr->gcVictimList[dieNo].fifo_tail){
				i = gcVictimMapPtr->gcVictimList[dieNo].headBlock;
			}
			else{
				i = virtualBlockMapPtr->block[dieNo][i].nextBlock;
				if(i == BLOCK_NONE){
					assert(!"range over 2\n");
				}
			}
		}

		//MERGE
		vi_list[0] = first_victim_i;
		sum = virtualBlockMapPtr->block[dieNo][first_victim_i].invalidSliceCnt;
		is_cold_merge = virtualBlockMapPtr->block[dieNo][first_victim_i].cold; // 1: cold merge block , 0: normal block
		j = 1;

		char mn = 0, mw = 0;

		if(is_cold_merge == 2){
			mw = 1;
			is_cold_merge = 0;
		}
		else if(is_cold_merge == 0){
			mn = 1;
		}

		if(is_cold_merge == 1){
			if(virtualDieMapPtr->die[dieNo].coldBlock != BLOCK_NONE){
				sum += (USER_PAGES_PER_BLOCK - virtualBlockMapPtr->block[dieNo][virtualDieMapPtr->die[dieNo].coldBlock].currentPage);
			}
		}
		else{
			if(virtualDieMapPtr->die[dieNo].warmBlock != BLOCK_NONE){
				sum += (USER_PAGES_PER_BLOCK - virtualBlockMapPtr->block[dieNo][virtualDieMapPtr->die[dieNo].warmBlock].currentPage);
			}
		}



		while(1){
			char is_exist_already = 0;
			for(int k = 0; k < j; k++){
				if(vi_list[k] == i){
					is_exist_already = 1;
					break;
				}
			}

			int tmp_cold;

			if(!is_exist_already){
				tmp_cold = virtualBlockMapPtr->block[dieNo][i].cold;
				if(tmp_cold == 2){
					mw = 1;
					tmp_cold = 0;
				}
				else if(tmp_cold == 0){
					mn = 1;
				}

				if(tmp_cold == is_cold_merge){
					double invalid_ratio = (double) virtualBlockMapPtr->block[dieNo][i].invalidSliceCnt / (double) USER_PAGES_PER_BLOCK;
					char cold_condition = invalid_ratio >= cold_block_valid_ratio;
					char normal_condition = invalid_ratio >= normal_block_valid_ratio;

					if ((is_cold_merge == 1 && cold_condition) || (is_cold_merge == 0 && normal_condition)){
						sum += virtualBlockMapPtr->block[dieNo][i].invalidSliceCnt;

						vi_list[j++] = i;

						if(mw == 1 && mn == 1){
							if(sum >= USER_PAGES_PER_BLOCK * 2){
								out = 1;
								break;
							}
						}
						else{
							if(sum >= USER_PAGES_PER_BLOCK * 2){
								out = 1;
								break;
							}
						}
					}
				}
			}

			if(i == fifo_end){
				if((is_cold_merge == 0 && normal_block_valid_ratio <= 0.02) || (is_cold_merge == 1 && cold_block_valid_ratio <= 0.02)){
					if(used_normal + used_cold == 1){
						assert(!"no victim! 2\n");
					}

					if(is_cold_merge == 0 && normal_block_valid_ratio <= 0.02){
						used_normal = 1;
					}
					else if(is_cold_merge == 1 && cold_block_valid_ratio <= 0.02){
						used_cold = 1;
					}

					break;
				}

				if (is_cold_merge == 1) {
					cold_block_valid_ratio -= 0.02;
				}
				else if(is_cold_merge == 0){
					normal_block_valid_ratio -= 0.02;
				}
			}

			if(i == gcVictimMapPtr->gcVictimList[dieNo].fifo_tail){
				i = gcVictimMapPtr->gcVictimList[dieNo].headBlock;
			}
			else{
				i = virtualBlockMapPtr->block[dieNo][i].nextBlock;
				if(i == BLOCK_NONE){
					assert(!"range over 1\n");
				}
			}
		}//multi victim while

		if(out == 0){
			continue;
		}

		break;
	}

	gcVictimMapPtr->gcVictimList[dieNo].normal_threshold = (int)(normal_block_valid_ratio * 100);
	gcVictimMapPtr->gcVictimList[dieNo].cold_threshold = (int)(cold_block_valid_ratio * 100);

	gcVictimMapPtr->gcVictimList[dieNo].fifo_head = i;
	evictedBlockNo = vi_list[0];

	for(int k = 0; k < j; k++){
		nextBlock = virtualBlockMapPtr->block[dieNo][vi_list[k]].nextBlock;
		prevBlock = virtualBlockMapPtr->block[dieNo][vi_list[k]].prevBlock;

		if((nextBlock != BLOCK_NONE) && (prevBlock != BLOCK_NONE)){
			virtualBlockMapPtr->block[dieNo][prevBlock].nextBlock = nextBlock;
			virtualBlockMapPtr->block[dieNo][nextBlock].prevBlock = prevBlock;
			if(gcVictimMapPtr->gcVictimList[dieNo].fifo_head == vi_list[k]){
				gcVictimMapPtr->gcVictimList[dieNo].fifo_head = nextBlock;
			}
		}
		else if((nextBlock == BLOCK_NONE) && (prevBlock != BLOCK_NONE)){
			virtualBlockMapPtr->block[dieNo][prevBlock].nextBlock = BLOCK_NONE;
			gcVictimMapPtr->gcVictimList[dieNo].tailBlock = prevBlock;
			if(gcVictimMapPtr->gcVictimList[dieNo].fifo_head == vi_list[k]){
				gcVictimMapPtr->gcVictimList[dieNo].fifo_head = gcVictimMapPtr->gcVictimList[dieNo].headBlock;
			}
		}
		else if((nextBlock != BLOCK_NONE) && (prevBlock == BLOCK_NONE)){
			virtualBlockMapPtr->block[dieNo][nextBlock].prevBlock = BLOCK_NONE;
			gcVictimMapPtr->gcVictimList[dieNo].headBlock = nextBlock;
			if(gcVictimMapPtr->gcVictimList[dieNo].fifo_head == vi_list[k]){
				gcVictimMapPtr->gcVictimList[dieNo].fifo_head = nextBlock;
			}
		}

		if(k == 0){
			virtualBlockMapPtr->block[dieNo][vi_list[k]].prevBlock = BLOCK_NONE;
			virtualBlockMapPtr->block[dieNo][vi_list[k]].nextBlock = vi_list[k + 1];
		}
		else if(k < j - 1){
			virtualBlockMapPtr->block[dieNo][vi_list[k]].nextBlock = vi_list[k + 1];
			virtualBlockMapPtr->block[dieNo][vi_list[k]].prevBlock = vi_list[k - 1];
		}
		else{
			virtualBlockMapPtr->block[dieNo][vi_list[k]].nextBlock = BLOCK_NONE;
		}
	}

	/*tmpshow = gcVictimMapPtr->gcVictimList[dieNo].headBlock;
	xil_printf("gc list\n");
	xil_printf("headBlock : %d tailBlock : %d\n", gcVictimMapPtr->gcVictimList[dieNo].headBlock, gcVictimMapPtr->gcVictimList[dieNo].tailBlock);
	xil_printf("fifo_head : %d fifo_tail : %d\n", gcVictimMapPtr->gcVictimList[dieNo].fifo_head, gcVictimMapPtr->gcVictimList[dieNo].fifo_tail);

	while(tmpshow != BLOCK_NONE){
		xil_printf("block number : %d invalid count : %d\n", tmpshow, virtualBlockMapPtr->block[dieNo][tmpshow].invalidSliceCnt);
		tmpshow = virtualBlockMapPtr->block[dieNo][tmpshow].nextBlock;
	}
	xil_printf("\n victim list\n");

	tmpshow = evictedBlockNo;
	while(tmpshow != BLOCK_NONE){
		xil_printf("block number : %d invalid count : %d\n", tmpshow, virtualBlockMapPtr->block[dieNo][tmpshow].invalidSliceCnt);
		tmpshow = virtualBlockMapPtr->block[dieNo][tmpshow].nextBlock;
	}
	xil_printf("\n free list\n");
	xil_printf("free head : %d free tail : %d\n", virtualDieMapPtr->die[dieNo].headFreeBlock, virtualDieMapPtr->die[dieNo].tailFreeBlock);

	tmpshow = virtualDieMapPtr->die[dieNo].headFreeBlock;
	while(tmpshow != BLOCK_NONE){
		xil_printf("%d ", tmpshow);
		tmpshow = virtualBlockMapPtr->block[dieNo][tmpshow].nextBlock;
	}

	xil_printf("\n\n current block : %d cold block : %d cold invalid : %d\n\n", virtualDieMapPtr->die[dieNo].currentBlock, virtualDieMapPtr->die[dieNo].coldBlock, virtualBlockMapPtr->block[dieNo][virtualDieMapPtr->die[dieNo].coldBlock].invalidSliceCnt);*/

	return evictedBlockNo;
}


void SelectiveGetFromGcVictimList(unsigned int dieNo, unsigned int blockNo)
{//블록 가득차면
	unsigned int nextBlock, prevBlock;

	nextBlock = virtualBlockMapPtr->block[dieNo][blockNo].nextBlock;
	prevBlock = virtualBlockMapPtr->block[dieNo][blockNo].prevBlock;

	if((nextBlock != BLOCK_NONE) && (prevBlock != BLOCK_NONE))
	{
		virtualBlockMapPtr->block[dieNo][prevBlock].nextBlock = nextBlock;
		virtualBlockMapPtr->block[dieNo][nextBlock].prevBlock = prevBlock;
	}
	else if((nextBlock == BLOCK_NONE) && (prevBlock != BLOCK_NONE))
	{
		virtualBlockMapPtr->block[dieNo][prevBlock].nextBlock = BLOCK_NONE;
	}
	else if((nextBlock != BLOCK_NONE) && (prevBlock == BLOCK_NONE))
	{
		virtualBlockMapPtr->block[dieNo][nextBlock].prevBlock = BLOCK_NONE;
	}

	PutToGcVictimList(dieNo, blockNo);
}

void preentgcstatus(){
	if(gcount > 0){
		xil_printf("%d %d %d\n", gcount, additional_write, request_write);
	}
}
