/*
 * CFGReachabilityAnalysis.h
 *
 * Basic CFG reachabilityAnalysis
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

#ifndef ANALYSIS_CFG_CFGREACHABILITYANALYSIS_H
#define ANALYSIS_CFG_CFGREACHABILITYANALYSIS_H

#include <llvm/ADT/BitVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <utility>
#include <unordered_map>

using namespace llvm;

class CFGReachability {
	typedef BitVector ReachableSet;
	ReachableSet analyzed;
	ReachableSet* reachable;

	// ID mapping
	std::vector<const BasicBlock *> IDtoBB;
	std::unordered_map<const BasicBlock *, int> BBID;

public:
	CFGReachability(const Function &F);
	~CFGReachability();

	/// Returns true if the block 'Dst' can be reached from block 'Src'.
	bool isReachable(const BasicBlock *Src, const BasicBlock *Dst);

private:
	void mapReachability(const BasicBlock *Dst);
};

class CFGReachabilityAnalysis: public ModulePass {
private:
	std::unordered_map<const Function *, CFGReachability *> funcReachMap;

public:
	static char ID;
	CFGReachabilityAnalysis() :
		ModulePass(ID) {
	}

	virtual ~CFGReachabilityAnalysis();

	bool runOnModule(Module& M);

	void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.setPreservesAll();
	}

public:
	/// Returns true if the block 'Dst' can be reached from block 'Src'.
	bool isBBReachable(const BasicBlock *Src, const BasicBlock *Dst);

	/// Determine reachability within one basic block.
	bool isReachableInBlock(const Instruction *Src, const Instruction *Dst) const;

	// Check the reachability from any two instructions in the same function
	bool isReachable(const Instruction *Src, const Instruction *Dst);

private:
	CFGReachability* getReachabilityResultFor(const Function*);
};

#endif /* ANALYSIS_CFG_CFGREACHABILITYANALYSIS_H */
