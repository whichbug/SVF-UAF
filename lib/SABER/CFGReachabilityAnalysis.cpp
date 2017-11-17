/*
 * CFGReachabilityAnalysis.cpp
 *
 *  Created on: Jun 19, 2015
 *      Author: Fan Gang
 *
 *  Modified on 02/04/2016
 *  	Author: Qingkai
 *  	Description: make it thread-safe by changing
 *  	the type of reachable from map<unsigned, ReachableSet>
 *  	to ReachableSet[].
 */

#include <cassert>
#include <iostream>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Instruction.h>

#include "SABER/CFGReachabilityAnalysis.h"

#define DEBUG_TYPE "cfg-reachability"

using namespace llvm;

char CFGReachabilityAnalysis::ID = 0;
static RegisterPass<CFGReachabilityAnalysis> X("x", "x");

CFGReachabilityAnalysis::~CFGReachabilityAnalysis() {
	for (auto pair : funcReachMap) {
		delete pair.second;
	}
}

bool CFGReachabilityAnalysis::runOnModule(Module& M) {
	return false;
}

CFGReachability* CFGReachabilityAnalysis::getReachabilityResultFor(const Function* func)
{
    CFGReachability* result;
    auto It = funcReachMap.find(func);

    if (It == funcReachMap.end()) {
        // We build the reachability results on demand
        result = new CFGReachability(*func);
        funcReachMap[func] = result;
    }
    else {
        result = It->second;
    }

    return result;
}

bool CFGReachabilityAnalysis::isBBReachable(const BasicBlock *Src, const BasicBlock *Dst) {
    assert(
            Src->getParent() == Dst->getParent()
                    && "Cannot query two basic blocks in different functions");

    CFGReachability* BB_reach_set = getReachabilityResultFor(Src->getParent());
    return BB_reach_set->isReachable(Src, Dst);
}

bool CFGReachabilityAnalysis::isReachableInBlock(const Instruction* Src,
        const Instruction* Dst) const {
    assert(Src->getParent() == Dst->getParent()
            && "isReachableInBlock is called on two instructions that belongs to different Basic Blocks");

    auto *srcBB = Src->getParent();
    for (auto &I : *srcBB) {
        if (&I == Src) {
            return true;
        } else if (&I == Dst) {
            return false;
        }
    }

    return false;
}

bool CFGReachabilityAnalysis::isReachable(const Instruction* Src, const Instruction* Dst) {

    auto *srcBB = Src->getParent();
    auto *dstBB = Dst->getParent();

    if (srcBB != dstBB) {
        // If the two instruction are located in different BB
        return isBBReachable(srcBB, dstBB);
    } else {
        // If this two BB are in the same BB.
        return isReachableInBlock(Src, Dst);
    }
}

CFGReachability::CFGReachability(const Function& F) :
        analyzed(F.size(), false) {

    reachable = new ReachableSet[F.size()];
    unsigned int i = 0;

    for (auto &BB : F) {
        IDtoBB.push_back(&BB);
        BBID[&BB] = i;
        reachable[i].resize(F.size(), false);
        ++i;
    }
}

CFGReachability::~CFGReachability() {
    delete [] reachable;
}

// Maps reachability to a common node by walking the predecessors of the
// destination node.
void CFGReachability::mapReachability(const BasicBlock *Dst) {
	llvm::BitVector visited(analyzed.size());

	ReachableSet &DstReachability = reachable[BBID[Dst]];

	// Search all the reachable nodes from \p Dst
	std::vector<const BasicBlock *> worklist;
	worklist.push_back(Dst);
	bool firstRun = true;

	while (!worklist.empty()) {
		const BasicBlock *block = worklist.back();
		worklist.pop_back();
		unsigned blockID = BBID[block];
		if (visited[blockID])
			continue;
		visited[blockID] = true;

		// Update reachability information for this node -> Dst
		if (!firstRun) {
			// Don't insert Dst -> Dst unless it was a predecessor of itself
			DstReachability[blockID] = true;
		} else
			firstRun = false;

		// Add the predecessors to the worklist.
		for (auto I = pred_begin(block), E = pred_end(block); I != E;
				++I) {
			const BasicBlock* pred_bb = *I;
			worklist.push_back(pred_bb);
		}
	}
}

bool CFGReachability::isReachable(const BasicBlock* Src, const BasicBlock* Dst) {

	assert(BBID.count(Dst) && BBID.count(Src) && "Never labeled the queried BB");

	const unsigned DstBlockID = BBID.at(Dst);
	// If we haven't analyzed the destination node, run the analysis now
	if (!analyzed[DstBlockID]) {
		mapReachability(Dst);
		analyzed[DstBlockID] = true;
	}

	// Return the cached result
	return reachable[DstBlockID][BBID.at(Src)];
}
