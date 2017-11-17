//===- UseAfterFreeChecker.cpp -- Use After Free detector ------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2016>  <Yulei Sui>
// Copyright (C) <2013-2016>  <Jingling Xue>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * UseAfterFreeChecker.cpp
 *
 * Qingkai
 */

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Debug.h>

#include "SABER/Profiler.h"
#include "SABER/UseAfterFreeChecker.h"
#include "Util/AnalysisUtil.h"

#define DEBUG_TYPE "uaf"

using namespace llvm;
using namespace analysisUtil;

char UseAfterFreeChecker::ID = 0;

static RegisterPass<UseAfterFreeChecker> UAFCHECKER("uaf-checker", "Use After Free Checker");

static cl::opt<bool> ReportNumOnly("report-num-only", cl::init(true),
                                   cl::desc("Validate memory leak tests"));

static cl::opt<bool> Nocheck("no-check", cl::init(false),
                                   cl::desc("Validate memory leak tests"));

static cl::opt<bool> IgnoreGlobal("no-global", cl::init(false),
                                   cl::desc("Validate memory leak tests"));

unsigned Index = 0;

extern Profiler* globalprofiler;

/*!
 * Initialize sources
 */
void UseAfterFreeChecker::initSrcs() {
    PAG* G = getPAG();

    for(PAG::CSToArgsListMap::iterator It = G->getCallSiteArgsMap().begin(),
            E = G->getCallSiteArgsMap().end(); It != E; ++It) {
        const Function* F = getCallee(It->first);
        if(isSinkLikeFun(F) && F->empty()) {
            PAG::PAGNodeList& Arglist = It->second;
            assert(!Arglist.empty() && "no actual parameter at deallocation site?");
            ActualParmSVFGNode* Src = getSVFG()->getActualParmSVFGNode(Arglist.front(),It->first);


            outs() << "Finding src: " << *Src->getCallSite().getInstruction() << "\n";

            addToSources(Src); // e.g., add p to sources if free(p)
            addSrcToEdge(Src, new CallDirSVFGEdge(Src, nullptr, getSVFG()->getCallSiteID(Src->getCallSite(), F)));
        }
    }
}

/*!
 * Initialize sinks
 */
void UseAfterFreeChecker::initSnks() {
    // do nothing
}

void UseAfterFreeChecker::reportBug(ProgSlice* Slice) {
    // do nothing
}

bool UseAfterFreeChecker::runOnModule(llvm::Module& M) {
    CFGR = &this->getAnalysis<CFGReachabilityAnalysis>();
    initialize(M);

    for (SVFGNodeSetIter It = sourcesBegin(), E = sourcesEnd(); It != E; ++It) {
        const ActualParmSVFGNode* Src = dyn_cast<ActualParmSVFGNode>(*It);
        assert(Src);

        std::vector<const SVFGEdge*> Ctx;
        Ctx.push_back(SrcToCallEdgeMap[Src]);


        DEBUG(errs() << "Start.... " << Src->getId() << "\n");

        push();
        searchBackward(Src, nullptr, nullptr, Ctx);
        pop();
    }

    finalize();
    std::string filename("svfg.dot");
    const_cast<SVFG*>(getSVFG())->dump(filename);

    outs() << "Total: " << Index << "\n";
    return false;
}

void UseAfterFreeChecker::searchBackward(const SVFGNode* CurrNode, const SVFGNode* PrevNode, const SVFGEdge* E,
        std::vector<const SVFGEdge*> Ctx) {
    if (Ctx.size() > ContextCond::getMaxCxtLen() + 1) {
        return;
    }

    DEBUG(errs() << "Visiting[b] " << getSVFGNodeMsg(CurrNode)<< "\n");

    SVFGPath.add(CurrNode);

    bool AllCalls = true;
    for (size_t I = 0; I < Ctx.size(); ++I) {
        if (!Ctx[I]->isCallVFGEdge()) {
            AllCalls = false;
            break;
        }
    }
    if (AllCalls) {
        push();
        std::vector<const SVFGEdge*> FCtx;

        assert(!Ctx.empty());
        //assert(isa<CallDirSVFGEdge>(Ctx.back()));
        CallSiteID XCSID;
        if (auto* X = dyn_cast<CallDirSVFGEdge>(Ctx.back())) {
            XCSID = X->getCallSiteId();
        } else if (auto* Y = dyn_cast<CallIndSVFGEdge>(Ctx.back())) {
            XCSID = Y->getCallSiteId();
        } else {
            assert(false);
        }
        searchForward(CurrNode, PrevNode, E, FCtx,
                getSVFG()->getCallSite(XCSID).getInstruction(), true);
        pop();
    }

    auto& InEdges = CurrNode->getInEdges();
    for(auto It = InEdges.begin(), E = InEdges.end(); It != E; ++It) {
        auto InEdge = *It;
        SVFGNode* Ancestor = InEdge->getSrcNode();
        //assert(Ancestor != CurrNode);
        if (Ancestor == CurrNode || !Ancestor->getBB())
            continue;

        if (InEdge->isCallVFGEdge() || InEdge->isRetVFGEdge()) {
            bool match = matchContextB(Ctx, InEdge);
            DEBUG_WITH_TYPE("bctx", printContextStack(Ctx));
            if (!match) {
                continue;
            }
        } else if (IgnoreGlobal.getValue() && CurrNode->getBB()->getParent() != Ancestor->getBB()->getParent()) {
            continue;
        }

        push();
        searchBackward(Ancestor, CurrNode, InEdge, Ctx);
        pop();
    }
}

void UseAfterFreeChecker::searchForward(const SVFGNode* CurrNode, const SVFGNode* PrevNode, const SVFGEdge* E,
        std::vector<const SVFGEdge*> Ctx, Instruction* CS, bool TagX) {
    if (Ctx.size() > ContextCond::getMaxCxtLen()) {
        return;
    }

    DEBUG(errs() << "Visiting[f] " << getSVFGNodeMsg(CurrNode)<< "\n");

    SVFGPath.add(CurrNode);

    if (auto* StmtNode = dyn_cast<StmtSVFGNode>(CurrNode)) {
        auto* PAGE = StmtNode->getPAGEdge();
        auto* Inst = PAGE->getInst();
        if(TagX && Inst && !Inst->getType()->isVoidTy())
            for (auto It = Inst->user_begin(), E = Inst->user_end(); It != E; ++It) {
                auto User = dyn_cast<Instruction>(*It);
                if (!User)
                    continue;

                bool report = false;
                if (auto* X = dyn_cast<LoadInst>(User)) {
                    report = X->getPointerOperand() == Inst;
                } else if (auto* X = dyn_cast<StoreInst>(User)) {
                    report = X->getPointerOperand() == Inst;
                } else if (auto* X = dyn_cast<CallInst>(User)) {
                    if (X->getCalledFunction() && isSinkLikeFun(X->getCalledFunction())) {
                        report = X->getArgOperand(0) == Inst;
                    }
                }

                if (report && reachable(CS, Inst)) {
                    push();
                    if(check(User)) {
                       // outs() << "+";
                        reportBug(User);
                    } else {
                        // outs() << "-";
                    }
                    pop();
                }
            }
    }

    auto& OutEdges = CurrNode->getOutEdges();
    for(auto It = OutEdges.begin(), E = OutEdges.end(); It != E; ++It) {
        auto OutEdge = *It;
        SVFGNode* Child = OutEdge->getDstNode();
        if (Child == PrevNode)
            continue;

        if (!Child->getBB())
            continue;

        bool Tag = true && TagX;
        if (OutEdge->isRetVFGEdge() || OutEdge->isCallVFGEdge()) {
            CallSiteID CSID = getCSID(OutEdge);
            CallSite CS2 = getSVFG()->getCallSite(CSID);

            if (CS2.getInstruction() == CS) {
                continue;
            }

            // match ctx
            if (!matchContextF(Ctx, OutEdge)) {
                continue;
            }

            if (!reachable(CS, CS2.getInstruction())) {
                Tag = false;
            }
        } else if (IgnoreGlobal.getValue() && CurrNode->getBB()->getParent() != Child->getBB()->getParent()) {
            continue;
        }

        push();
        searchForward(Child, CurrNode, OutEdge, Ctx, CS, Tag);
        pop();
    }
}

bool UseAfterFreeChecker::matchContextB(std::vector<const SVFGEdge*>& Ctx, SVFGEdge* Edge) {
    if (!Ctx.empty()) {
        CallSiteID ID = getCSID(Edge);

        auto* Top = Ctx.back();
        CallSiteID TopID = getCSID(Top);

        if (ID == TopID) {
            if (Edge->isCallVFGEdge() != Top->isCallVFGEdge()) {
                DEBUG_WITH_TYPE("bctx", errs() << "Pop back visiting " <<
                        getSourceLoc(getSVFG()->getCallSite(ID).getInstruction()));
                Ctx.pop_back();
                return true;
            }
        } else {
            // if it is call and all call in Ctx
            if (Edge->isCallVFGEdge()) {
                bool AllCalls = true;
                for (size_t I = 0; I < Ctx.size(); ++I) {
                    if (!Ctx[I]->isCallVFGEdge()) {
                        AllCalls = false;
                        break;
                    }
                }

                if (AllCalls && Edge->getDstNode()->getBB()->getParent()
                        == Top->getSrcNode()->getBB()->getParent()) {
                    Ctx.push_back(Edge);
                    return true;
                }
            } else {
                assert(Edge->isRetVFGEdge());
                Ctx.push_back(Edge);
                return true;
            }
        }
    } else {
        Ctx.push_back(Edge);
        return true;
    }

    return false;
}

bool UseAfterFreeChecker::matchContextF(std::vector<const SVFGEdge*>& Ctx, SVFGEdge* Edge) {
    if (!Ctx.empty()) {
        CallSiteID ID = getCSID(Edge);

        auto* Top = Ctx.back();
        CallSiteID TopID = getCSID(Top);

        if (ID == TopID) {
            if (Edge->isCallVFGEdge() != Top->isCallVFGEdge()) {
                Ctx.pop_back();
                return true;
            }
        } else {
            // if it is ret and all ret in Ctx
            if (Edge->isRetVFGEdge()) {
                bool AllRets = true;
                for (size_t I = 0; I < Ctx.size(); ++I) {
                    if (!Ctx[I]->isRetVFGEdge()) {
                        AllRets = false;
                        break;
                    }
                }

                if (AllRets && Top->getDstNode()->getBB()->getParent()
                        == Edge->getSrcNode()->getBB()->getParent()) {
                    Ctx.push_back(Edge);
                    return true;
                }
            } else {
                assert(Edge->isCallVFGEdge());
                Ctx.push_back(Edge);
                return true;
            }
        }
    } else {
        Ctx.push_back(Edge);
        return true;
    }

    return false;
}

CallSiteID UseAfterFreeChecker::getCSID(const SVFGEdge* Edge) {
    CallSiteID ID;
    if (auto* X = dyn_cast<CallDirSVFGEdge>(Edge)) {
        ID = X->getCallSiteId();
    } else if (auto* Y = dyn_cast<CallIndSVFGEdge>(Edge)) {
        ID = Y->getCallSiteId();
    } else if (auto* Z = dyn_cast<RetIndSVFGEdge>(Edge)) {
        ID = Z->getCallSiteId();
    } else if (auto* W = dyn_cast<RetDirSVFGEdge>(Edge)) {
        ID = W->getCallSiteId();
    } else {
        assert(false);
    }
    return ID;
}

void UseAfterFreeChecker::reportBug(const Instruction* TailInst) {
    if (ReportNumOnly.getValue()) {
        Index++;
//        if (Index >= 100) {
//            outs() << "\n[!!!] Exit with more than 100 reports!\n";
//            globalprofiler->create_snapshot();
//            outs() << "\n\n";
//            globalprofiler->print_snapshot_result("Total");
//            globalprofiler->print_peak_memory();
//            exit(0);
//        }
        return;
    }

    outs() << "+++++" << ++Index << "+++++\n";
    for(unsigned I = 0; I < SVFGPath.size(); ++I) {
        auto* N = SVFGPath[I];
        outs() << "[" << I << "] ";
        outs() << getSVFGNodeMsg(N);
        outs() << "\n";
    }
    outs() << "[" << SVFGPath.size() << "] ";
    outs() << "XX (" << TailInst->getParent()->getParent()->getName() << ") \t" << *TailInst;
    outs() << "\n\n";

//    if (Index > 2000)
//        exit(0);
}

std::string UseAfterFreeChecker::getSVFGNodeMsg(const SVFGNode* N) {
    const Instruction* Inst = nullptr;
    if (auto* X = dyn_cast<StmtSVFGNode>(N)) {
        Inst = (X->getInst());
    } if (auto* X = dyn_cast<ActualParmSVFGNode>(N)) {
        Inst = X->getCallSite().getInstruction();
    } else if (auto* X = dyn_cast<ActualINSVFGNode>(N)) {
        Inst = X->getCallSite().getInstruction();
    } else if (auto* X = dyn_cast<ActualRetSVFGNode>(N)) {
        Inst = X->getCallSite().getInstruction();
    } else if (auto* X = dyn_cast<ActualOUTSVFGNode>(N)) {
        Inst = X->getCallSite().getInstruction();
    }

    std::string Ret;
    llvm::raw_string_ostream O(Ret);
    if (Inst) {
        O << N->getId() << " (" << Inst->getParent()->getParent()->getName() << ") \t" << " " << *Inst;
    } else if (N->getBB()) {
        O << N->getId() << " (" << N->getBB()->getParent()->getName() << ") ";
    } else {
        O << N->getId() << " (unknown function) ";
    }
    return std::move(O.str());
}

bool UseAfterFreeChecker::reachable(const llvm::Instruction* From, const llvm::Instruction* To) {
    if (From->getParent()->getParent() != To->getParent()->getParent()) {
        return true;
    } else {
        return CFGR->isReachable(From, To) && From != To;
    }
}

void UseAfterFreeChecker::printContextStack(std::vector<const SVFGEdge*>& Ctx) {
    outs() << "+++++++++++++++++++++++++\n";
    for (auto* Edge : Ctx) {
        bool Call = Edge->isCallVFGEdge();
        CallSiteID Id = getCSID(Edge);
        CallSite CS = getSVFG()->getCallSite(Id);
        if (Call)
            outs() << "+ Call to ";
        else
            outs() << "+ Retn fm ";
        outs() << CS.getCalledFunction()->getName();
        outs() << " at Line " << getSourceLoc(CS.getInstruction()) << "\n";
    }
    outs() << "+++++++++++++++++++++++++\n";
}

bool UseAfterFreeChecker::check(const Instruction* User) {
    if (Nocheck.getValue()) {
        return true;
    }

    auto* PA = this->getPathAllocator();

    svfgNodeToCondMap.clear();
    PA->clearCFCond();

    const SVFGNode* source = nullptr;
    for(size_t I = 0; I < SVFGPath.size() - 1; ++I) {
        if (SVFGPath[I] == SVFGPath[I + 1]) {
            source = SVFGPath[I];
            break;
        }
    }
    assert(source);

    std::map<const SVFGNode*, std::set<const SVFGNode*>> Nodes;
    bool reachSrc = false;
    for(size_t I = 1; I < SVFGPath.size() - 1; ++I) {
        if (!reachSrc) {
            Nodes[SVFGPath[I]].insert(SVFGPath[I - 1]);
        } else {
            Nodes[SVFGPath[I]].insert(SVFGPath[I + 1]);
        }
        if (SVFGPath[I] == source) {
            reachSrc = true;
        }
    }

    typedef FIFOWorkList<const SVFGNode*> VFWorkList;
    VFWorkList worklist;
    worklist.push(source);
    /// mark source node conditions to be true
    setVFCond(source, PA->getTrueCond());

    while(!worklist.empty()) {
        const SVFGNode* node = worklist.pop();

        PA->setCurEvalVal(getLLVMValue(node));

        PathCondAllocator::Condition* cond = getVFCond(node);
        for(SVFGNode::const_iterator it = node->OutEdgeBegin(), eit = node->OutEdgeEnd(); it!=eit; ++it) {
            const SVFGEdge* edge = (*it);
            const SVFGNode* succ = edge->getDstNode();
            if(Nodes[node].count(succ)) {
                PathCondAllocator::Condition* vfCond = NULL;
                const BasicBlock* nodeBB = getSVFGNodeBB(node);
                const BasicBlock* succBB = getSVFGNodeBB(succ);
                /// clean up the control flow conditions for next round guard computation
                PA->clearCFCond();

                if(edge->isCallVFGEdge()) {
                    vfCond = PA->ComputeInterCallVFGGuard(nodeBB,succBB, getCallSite(edge).getInstruction()->getParent());
                }
                else if(edge->isRetVFGEdge()) {
                    vfCond = PA->ComputeInterRetVFGGuard(nodeBB,succBB, getRetSite(edge).getInstruction()->getParent());
                }
                else {
                    vfCond = PA->ComputeIntraVFGGuard(nodeBB,succBB);
                }

                PathCondAllocator::Condition* succPathCond = PA->condAnd(cond, vfCond);
                if(setVFCond(succ,  PA->condOr(getVFCond(succ), succPathCond) ))
                    worklist.push(succ);
            }

            DBOUT(DSaber, outs() << " node (" << node->getId() << ":" << node->getBB()->getName() <<
                    ") --> " << "succ (" << succ->getId() << ":" << succ->getBB()->getName() << ") condition: " << getVFCond(succ) << "\n");
        }
    }

    /*************************/
    PathCondAllocator::Condition* vfCond = nullptr;
    const BasicBlock* nodeBB = SVFGPath.top()->getBB();
    const BasicBlock* succBB = User->getParent();
    assert(nodeBB->getParent() == succBB->getParent());
    /// clean up the control flow conditions for next round guard computation
    PA->clearCFCond();
    vfCond = PA->ComputeIntraVFGGuard(nodeBB,succBB);
    PathCondAllocator::Condition* succPathCond = PA->condAnd(getVFCond(SVFGPath.top()), vfCond);
    /*************************/

    PathCondAllocator::Condition* guard = PA->condAnd(getVFCond(SVFGPath[0]), succPathCond);
    if(guard != PA->getFalseCond()) {
        return true;
    }

    return false;
}

void UseAfterFreeChecker::push() {
    SVFGPath.push();
}

void UseAfterFreeChecker::pop() {
    SVFGPath.pop();
}
