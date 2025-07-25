// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Add temporaries, such as for inst nodes
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2003-2025 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
// V3Inst's Transformations:
//
// Each module:
//      Pins:
//          Create a wire assign to interconnect to submodule
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3Inst.h"

#include "V3Const.h"

VL_DEFINE_DEBUG_FUNCTIONS;

//######################################################################
// Inst state, as a visitor of each AstNode

class InstVisitor final : public VNVisitor {
    // NODE STATE
    // Cleared each Cell:
    //  AstPin::user1p()        -> bool.  True if created assignment already
    const VNUser1InUse m_inuser1;

    // STATE
    AstCell* m_cellp = nullptr;  // Current cell

    // VISITORS
    void visit(AstCell* nodep) override {
        UINFO(4, "  CELL   " << nodep);
        VL_RESTORER(m_cellp);
        m_cellp = nodep;
        // VV*****  We reset user1p() on each cell!!!
        AstNode::user1ClearTree();
        iterateChildren(nodep);
    }
    void visit(AstPin* nodep) override {
        // PIN(p,expr) -> ASSIGNW(VARXREF(p),expr)    (if sub's input)
        //            or  ASSIGNW(expr,VARXREF(p))    (if sub's output)
        UINFO(4, "   PIN  " << nodep);
        if (!nodep->user1()) {
            // Simplify it
            V3Inst::pinReconnectSimple(nodep, m_cellp, false);
        }
        if (!nodep->exprp()) return;  // No-connect
        if (debug() >= 9) nodep->dumpTree("-  Pin_oldb: ");
        V3Inst::checkOutputShort(nodep);
        // Use user1p on the PIN to indicate we created an assign for this pin
        if (!nodep->user1SetOnce()) {
            // Make an ASSIGNW (expr, pin)
            AstNodeExpr* const exprp = VN_AS(nodep->exprp(), NodeExpr)->cloneTree(false);
            UASSERT_OBJ(exprp->width() == nodep->modVarp()->width(), nodep,
                        "Width mismatch, should have been handled in pinReconnectSimple");
            if (nodep->modVarp()->isInout()) {
                nodep->v3fatalSrc("Unsupported: Verilator is a 2-state simulator");
            } else if (nodep->modVarp()->isWritable()) {
                AstNodeExpr* const rhsp = new AstVarXRef{exprp->fileline(), nodep->modVarp(),
                                                         m_cellp->name(), VAccess::READ};
                AstAssignW* const assp = new AstAssignW{exprp->fileline(), exprp, rhsp};
                m_cellp->addNextHere(assp);
            } else if (nodep->modVarp()->isNonOutput()) {
                // Don't bother moving constants now,
                // we'll be pushing the const down to the cell soon enough.
                AstNode* const assp
                    = new AstAssignW{exprp->fileline(),
                                     new AstVarXRef{exprp->fileline(), nodep->modVarp(),
                                                    m_cellp->name(), VAccess::WRITE},
                                     exprp};
                m_cellp->addNextHere(assp);
                if (debug() >= 9) assp->dumpTree("-     _new: ");
            } else if (nodep->modVarp()->isIfaceRef()
                       || (VN_IS(nodep->modVarp()->dtypep()->skipRefp(), UnpackArrayDType)
                           && VN_IS(VN_AS(nodep->modVarp()->dtypep()->skipRefp(), UnpackArrayDType)
                                        ->subDTypep()
                                        ->skipRefp(),
                                    IfaceRefDType))) {
                // Create an AstAssignVarScope for Vars to Cells so we can
                // link with their scope later
                AstNodeExpr* const lhsp = new AstVarXRef{exprp->fileline(), nodep->modVarp(),
                                                         m_cellp->name(), VAccess::READ};
                const AstVarRef* const refp = VN_CAST(exprp, VarRef);
                const AstVarXRef* const xrefp = VN_CAST(exprp, VarXRef);
                UASSERT_OBJ(refp || xrefp, exprp,
                            "Interfaces: Pin is not connected to a VarRef or VarXRef");
                AstAssignVarScope* const assp
                    = new AstAssignVarScope{exprp->fileline(), lhsp, exprp};
                m_cellp->addNextHere(assp);
            } else {
                nodep->v3error("Assigned pin is neither input nor output");
            }
        }

        // We're done with the pin
        VL_DO_DANGLING(nodep->unlinkFrBack()->deleteTree(), nodep);
    }

    // Save some time
    void visit(AstNodeExpr*) override {}
    void visit(AstNodeAssign*) override {}
    void visit(AstAlways*) override {}

    //--------------------
    void visit(AstNode* nodep) override { iterateChildren(nodep); }

public:
    // CONSTRUCTORS
    explicit InstVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~InstVisitor() override = default;
};

//######################################################################

class InstDeModVarVisitor final : public VNVisitorConst {
    // Expand all module variables, and save names for later reference
private:
    // STATE
    std::map<const std::string, AstVar*> m_modVarNameMap;  // Per module, name of cloned variables

    // VISITORS
    void visit(AstVar* nodep) override {
        if (VN_IS(nodep->dtypep()->skipRefp(), IfaceRefDType)) {
            UINFO(8, "   dm-1-VAR    " << nodep);
            insert(nodep);
        }
        iterateChildrenConst(nodep);
    }
    void visit(AstNodeExpr*) override {}  // Accelerate
    void visit(AstNode* nodep) override { iterateChildrenConst(nodep); }

public:
    // METHODS
    void insert(AstVar* nodep) {
        UINFO(8, "    dmINSERT    " << nodep);
        m_modVarNameMap.emplace(nodep->name(), nodep);
    }
    AstVar* find(const string& name) {
        const auto it = m_modVarNameMap.find(name);
        if (it != m_modVarNameMap.end()) {
            return it->second;
        } else {
            return nullptr;
        }
    }
    void dump() {
        for (const auto& itr : m_modVarNameMap) {
            cout << "-namemap: " << itr.first << " -> " << itr.second << endl;
        }
    }

    // CONSTRUCTORS
    InstDeModVarVisitor() = default;
    ~InstDeModVarVisitor() override = default;
    void main(AstNodeModule* nodep) {
        UINFO(8, "  dmMODULE    " << nodep);
        m_modVarNameMap.clear();
        iterateConst(nodep);
    }
};

//######################################################################

class InstDeVisitor final : public VNVisitor {
    // Find all cells with arrays, and convert to non-arrayed
private:
    // STATE
    // Range for arrayed instantiations, nullptr for normal instantiations
    const AstRange* m_cellRangep = nullptr;
    int m_instSelNum = 0;  // Current instantiation count 0..N-1
    InstDeModVarVisitor m_deModVars;  // State of variables for current cell module

    // VISITORS
    void visit(AstVar* nodep) override {
        AstNode* const dtp = nodep->dtypep()->skipRefp();
        if (VN_IS(dtp, UnpackArrayDType)
            && VN_IS(VN_AS(dtp, UnpackArrayDType)->subDTypep()->skipRefp(), IfaceRefDType)) {
            if (VN_AS(VN_AS(dtp, UnpackArrayDType)->subDTypep()->skipRefp(), IfaceRefDType)
                    ->isVirtual())
                return;
            UINFO(8, "   dv-vec-VAR    " << nodep);
            AstUnpackArrayDType* const arrdtype = VN_AS(dtp, UnpackArrayDType);
            AstNode* prevp = nullptr;
            for (int i = arrdtype->lo(); i <= arrdtype->hi(); ++i) {
                const string varNewName = nodep->name() + "__BRA__" + cvtToStr(i) + "__KET__";
                UINFO(8, "VAR name insert " << varNewName << "  " << nodep);
                if (!m_deModVars.find(varNewName)) {
                    AstIfaceRefDType* const ifaceRefp
                        = VN_AS(arrdtype->subDTypep()->skipRefp(), IfaceRefDType)
                              ->cloneTree(false);
                    arrdtype->addNextHere(ifaceRefp);
                    ifaceRefp->cellp(nullptr);

                    AstVar* const varNewp = nodep->cloneTree(false);
                    varNewp->name(varNewName);
                    varNewp->origName(varNewp->origName() + "__BRA__" + cvtToStr(i) + "__KET__");
                    varNewp->dtypep(ifaceRefp);
                    m_deModVars.insert(varNewp);
                    if (!prevp) {
                        prevp = varNewp;
                    } else {
                        prevp->addNextHere(varNewp);
                    }
                }
            }
            if (prevp) nodep->addNextHere(prevp);
            if (prevp && debug() == 9) {
                prevp->dumpTree("-  newintf: ");
                cout << endl;
            }
        }
        iterateChildren(nodep);
    }

    void visit(AstCell* nodep) override {
        UINFO(4, "  CELL   " << nodep);
        // Find submodule vars
        UASSERT_OBJ(nodep->modp(), nodep, "Unlinked");
        m_deModVars.main(nodep->modp());
        //
        if (nodep->rangep()) {
            m_cellRangep = nodep->rangep();

            AstVar* const ifaceVarp = VN_CAST(nodep->nextp(), Var);
            AstNodeDType* const ifaceVarDtp
                = ifaceVarp ? ifaceVarp->dtypep()->skipRefp() : nullptr;
            const bool isIface
                = ifaceVarp && VN_IS(ifaceVarDtp, UnpackArrayDType)
                  && VN_IS(VN_AS(ifaceVarDtp, UnpackArrayDType)->subDTypep()->skipRefp(),
                           IfaceRefDType)
                  && !VN_AS(VN_AS(ifaceVarDtp, UnpackArrayDType)->subDTypep()->skipRefp(),
                            IfaceRefDType)
                          ->isVirtual();

            // Make all of the required clones
            for (int i = 0; i < m_cellRangep->elementsConst(); i++) {
                m_instSelNum
                    = m_cellRangep->ascending() ? (m_cellRangep->elementsConst() - 1 - i) : i;
                const int instNum = m_cellRangep->loConst() + i;

                AstCell* const newp = nodep->cloneTree(false);
                nodep->addNextHere(newp);
                // Remove ranging and fix name
                newp->rangep()->unlinkFrBack()->deleteTree();
                // Somewhat illogically, we need to rename the original name of the cell too.
                // as that is the name users expect for dotting
                // The spec says we add [x], but that won't work in C...
                newp->name(newp->name() + "__BRA__" + cvtToStr(instNum) + "__KET__");
                newp->origName(newp->origName() + "__BRA__" + cvtToStr(instNum) + "__KET__");
                UINFO(8, "    CELL loop  " << newp);

                // If this AstCell is actually an interface instantiation, also clone the IfaceRef
                // within the same parent module as the cell
                if (isIface) {
                    AstUnpackArrayDType* const arrdtype = VN_AS(ifaceVarDtp, UnpackArrayDType);
                    AstIfaceRefDType* const origIfaceRefp
                        = VN_AS(arrdtype->subDTypep()->skipRefp(), IfaceRefDType);
                    origIfaceRefp->cellp(nullptr);
                    AstVar* const varNewp = ifaceVarp->cloneTree(false);
                    AstIfaceRefDType* const ifaceRefp = origIfaceRefp->cloneTree(false);
                    arrdtype->addNextHere(ifaceRefp);
                    ifaceRefp->cellp(newp);
                    ifaceRefp->cellName(newp->name());
                    varNewp->name(varNewp->name() + "__BRA__" + cvtToStr(instNum) + "__KET__");
                    varNewp->origName(varNewp->origName() + "__BRA__" + cvtToStr(instNum)
                                      + "__KET__");
                    varNewp->dtypep(ifaceRefp);
                    newp->addNextHere(varNewp);
                    if (debug() == 9) {
                        varNewp->dumpTree("-  newintf: ");
                        cout << endl;
                    }
                }
                // Fixup pins
                iterateAndNextNull(newp->pinsp());
                if (debug() == 9) {
                    newp->dumpTree("-  newcell: ");
                    cout << endl;
                }
            }

            // Done.  Delete original
            m_cellRangep = nullptr;
            if (isIface) {
                ifaceVarp->unlinkFrBack();
                VL_DO_DANGLING(pushDeletep(ifaceVarp), ifaceVarp);
            }
            nodep->unlinkFrBack();
            VL_DO_DANGLING(pushDeletep(nodep), nodep);
        } else {
            m_cellRangep = nullptr;
            iterateChildren(nodep);
        }
    }

    void visit(AstPin* nodep) override {
        // Any non-direct pins need reconnection with a part-select
        if (!nodep->exprp()) return;  // No-connect
        const AstNodeDType* expDtp = nodep->exprp()->dtypep()->skipRefp();
        if (m_cellRangep) {
            UINFO(4, "   PIN  " << nodep);
            const int modwidth = nodep->modVarp()->width();
            const int expwidth = nodep->exprp()->width();
            const std::pair<uint32_t, uint32_t> pinDim
                = nodep->modVarp()->dtypep()->skipRefp()->dimensions(false);
            const std::pair<uint32_t, uint32_t> expDim = expDtp->dimensions(false);
            UINFO(4, "   PINVAR  " << nodep->modVarp());
            UINFO(4, "   EXP     " << nodep->exprp());
            UINFO(4, "   expwidth=" << expwidth << " modwidth=" << modwidth
                                    << "  expDim(p,u)=" << expDim.first << "," << expDim.second
                                    << "  pinDim(p,u)=" << pinDim.first << "," << pinDim.second);
            if (expDim.second == pinDim.second + 1) {
                // Connection to array, where array dimensions match the instant dimension
                const AstRange* const rangep = VN_AS(expDtp, UnpackArrayDType)->rangep();
                const int arraySelNum = rangep->ascending()
                                            ? (rangep->elementsConst() - 1 - m_instSelNum)
                                            : m_instSelNum;
                AstNodeExpr* exprp = VN_AS(nodep->exprp(), NodeExpr)->unlinkFrBack();
                exprp = new AstArraySel{exprp->fileline(), exprp, arraySelNum};
                nodep->exprp(exprp);
            } else if (expwidth == modwidth) {
                // NOP: Arrayed instants: widths match so connect to each instance
            } else if (expwidth == modwidth * m_cellRangep->elementsConst()) {
                // Arrayed instants: one bit for each of the instants (each
                // assign is 1 modwidth wide)
                if (m_cellRangep->ascending()) {
                    nodep->exprp()->v3warn(ASCRANGE, "Ascending instance range connecting to "
                                                     "vector: left < right of instance range: ["
                                                         << m_cellRangep->leftConst() << ":"
                                                         << m_cellRangep->rightConst() << "]");
                }
                AstNodeExpr* exprp = VN_AS(nodep->exprp(), NodeExpr)->unlinkFrBack();
                const bool inputPin = nodep->modVarp()->isNonOutput();
                if (!inputPin
                    && !VN_IS(exprp, VarRef)
                    // V3Const will collapse the SEL with the one we're about to make
                    && !VN_IS(exprp, Concat) && !VN_IS(exprp, Replicate) && !VN_IS(exprp, Sel)) {
                    nodep->v3warn(E_UNSUPPORTED, "Unsupported: Per-bit array instantiations "
                                                 "with output connections to non-wires.");
                    // Note spec allows more complicated matches such as slices and such
                }
                exprp = new AstSel{exprp->fileline(), exprp, modwidth * m_instSelNum, modwidth};
                nodep->exprp(exprp);
            } else {
                nodep->v3fatalSrc("Width mismatch; V3Width should have errored out.");
            }
        }  // end expanding ranged cell
        else if (AstArraySel* const arrselp = VN_CAST(nodep->exprp(), ArraySel)) {
            if (const AstUnpackArrayDType* const arrp
                = VN_CAST(arrselp->fromp()->dtypep()->skipRefp(), UnpackArrayDType)) {
                if (!VN_IS(arrp->subDTypep()->skipRefp(), IfaceRefDType)) return;
                if (VN_AS(arrp->subDTypep()->skipRefp(), IfaceRefDType)->isVirtual()) return;
                // Interface pin attaches to one element of arrayed interface
                V3Const::constifyParamsEdit(arrselp->bitp());
                const AstConst* const constp = VN_CAST(arrselp->bitp(), Const);
                if (!constp) {
                    nodep->v3warn(
                        E_UNSUPPORTED,
                        "Unsupported: Non-constant index when passing interface to module");
                    return;
                }
                const string index = AstNode::encodeNumber(constp->toSInt() + arrp->lo());
                if (VN_IS(arrselp->fromp(), SliceSel))
                    arrselp->fromp()->v3error("Unsupported: interface slices");
                const AstVarRef* const varrefp = VN_CAST(arrselp->fromp(), VarRef);
                UASSERT_OBJ(varrefp, arrselp, "No interface varref under array");
                AstVarXRef* const newp = new AstVarXRef{
                    nodep->fileline(), varrefp->name() + "__BRA__" + index + "__KET__", "",
                    VAccess::WRITE};
                newp->dtypep(nodep->modVarp()->dtypep());
                newp->classOrPackagep(varrefp->classOrPackagep());
                arrselp->addNextHere(newp);
                VL_DO_DANGLING(arrselp->unlinkFrBack()->deleteTree(), arrselp);
            }
        } else {
            AstVar* const pinVarp = nodep->modVarp();
            const AstUnpackArrayDType* const pinArrp
                = VN_CAST(pinVarp->dtypep()->skipRefp(), UnpackArrayDType);
            if (!pinArrp || !VN_IS(pinArrp->subDTypep()->skipRefp(), IfaceRefDType)) return;
            if (VN_AS(pinArrp->subDTypep()->skipRefp(), IfaceRefDType)->isVirtual()) return;
            // Arrayed pin/var attaches to arrayed submodule lower port/var, expand it
            AstNode* prevp = nullptr;
            AstNode* prevPinp = nullptr;
            // Clone the var referenced by the pin, and clone each var referenced by the varref
            // Clone pin varp:
            for (int in = 0; in < pinArrp->elementsConst(); ++in) {  // 0 = leftmost
                const int i = pinArrp->left() + in * pinArrp->declRange().leftToRightInc();
                const string varNewName = pinVarp->name() + "__BRA__" + cvtToStr(i) + "__KET__";
                AstVar* varNewp = nullptr;

                // Only clone the var once for all usages of a given child module
                if (!pinVarp->backp()) {
                    varNewp = m_deModVars.find(varNewName);
                } else {
                    AstIfaceRefDType* const ifaceRefp
                        = VN_AS(pinArrp->subDTypep()->skipRefp(), IfaceRefDType);
                    ifaceRefp->cellp(nullptr);
                    varNewp = pinVarp->cloneTree(false);
                    varNewp->name(varNewName);
                    varNewp->origName(varNewp->origName() + "__BRA__" + cvtToStr(i) + "__KET__");
                    varNewp->dtypep(ifaceRefp);
                    m_deModVars.insert(varNewp);
                    if (!prevp) {
                        prevp = varNewp;
                    } else {
                        prevp->addNextHere(varNewp);
                    }
                }
                if (!varNewp) {
                    if (debug() >= 9) m_deModVars.dump();  // LCOV_EXCL_LINE
                    nodep->v3fatalSrc("Module dearray failed for "
                                      << AstNode::prettyNameQ(varNewName));
                }

                // But clone the pin for each module instance
                // Now also clone the pin itself and update its varref
                AstPin* const newp = nodep->cloneTree(false);
                newp->modVarp(varNewp);
                newp->name(newp->name() + "__BRA__" + cvtToStr(i) + "__KET__");
                // And replace exprp with a new varxref
                const AstVarRef* varrefp = VN_CAST(newp->exprp(), VarRef);  // Maybe null
                int expr_i = i;
                if (const AstSliceSel* const slicep = VN_CAST(newp->exprp(), SliceSel)) {
                    varrefp = VN_AS(slicep->fromp(), VarRef);
                    UASSERT_OBJ(VN_IS(slicep->rhsp(), Const), slicep, "Slices should be constant");
                    const int slice_index
                        = slicep->declRange().left() + in * slicep->declRange().leftToRightInc();
                    const auto* const exprArrp
                        = VN_AS(varrefp->dtypep()->skipRefp(), UnpackArrayDType);
                    UASSERT_OBJ(exprArrp, slicep, "Slice of non-array");
                    expr_i = slice_index + exprArrp->lo();
                } else if (!varrefp) {
                    newp->exprp()->v3error("Unexpected connection to arrayed port");
                } else if (const auto* const exprArrp
                           = VN_CAST(varrefp->dtypep()->skipRefp(), UnpackArrayDType)) {
                    expr_i = exprArrp->left() + in * exprArrp->declRange().leftToRightInc();
                }

                const string newname = varrefp->name() + "__BRA__" + cvtToStr(expr_i) + "__KET__";
                AstVarXRef* const newVarXRefp
                    = new AstVarXRef{nodep->fileline(), newname, "", VAccess::WRITE};
                newVarXRefp->varp(newp->modVarp());
                newp->exprp()->unlinkFrBack()->deleteTree();
                newp->exprp(newVarXRefp);
                if (!prevPinp) {
                    prevPinp = newp;
                } else {
                    prevPinp->addNextHere(newp);
                }
            }
            if (prevp) {
                pinVarp->replaceWith(prevp);
                pushDeletep(pinVarp);
            }  // else pinVarp already unlinked when another instance did this step
            nodep->replaceWith(prevPinp);
            VL_DO_DANGLING(pushDeletep(nodep), nodep);
        }
    }
    void visit(AstArraySel* nodep) override {
        if (const AstUnpackArrayDType* const arrp
            = VN_CAST(nodep->fromp()->dtypep()->skipRefp(), UnpackArrayDType)) {
            if (!VN_IS(arrp->subDTypep()->skipRefp(), IfaceRefDType)) return;
            if (VN_AS(arrp->subDTypep()->skipRefp(), IfaceRefDType)->isVirtual()) return;
            V3Const::constifyParamsEdit(nodep->bitp());
            const AstConst* const constp = VN_CAST(nodep->bitp(), Const);
            if (!constp) {
                nodep->bitp()->v3warn(E_UNSUPPORTED,
                                      "Non-constant index in RHS interface array selection");
                return;
            }
            const string index = AstNode::encodeNumber(constp->toSInt() + arrp->lo());
            const AstVarRef* const varrefp = VN_CAST(nodep->fromp(), VarRef);
            UASSERT_OBJ(varrefp, nodep, "No interface varref under array");
            AstVarXRef* const newp = new AstVarXRef{
                nodep->fileline(), varrefp->name() + "__BRA__" + index + "__KET__", "",
                VAccess::READ};
            newp->dtypep(arrp->subDTypep());
            newp->classOrPackagep(varrefp->classOrPackagep());
            nodep->addNextHere(newp);
            VL_DO_DANGLING(pushDeletep(nodep->unlinkFrBack()), nodep);
        }
    }
    void visit(AstNodeAssign* nodep) override {
        if (AstSliceSel* const arrslicep = VN_CAST(nodep->rhsp(), SliceSel)) {
            if (const AstUnpackArrayDType* const arrp
                = VN_CAST(arrslicep->fromp()->dtypep()->skipRefp(), UnpackArrayDType)) {
                if (!VN_IS(arrp->subDTypep()->skipRefp(), IfaceRefDType)) return;
                if (VN_AS(arrp->subDTypep()->skipRefp(), IfaceRefDType)->isVirtual()) return;
                arrslicep->v3warn(E_UNSUPPORTED, "Interface slices unsupported");
                return;
            }
        } else {
            if (const AstUnpackArrayDType* const rhsarrp
                = VN_CAST(nodep->rhsp()->dtypep()->skipRefp(), UnpackArrayDType)) {
                if (const AstUnpackArrayDType* const lhsarrp
                    = VN_CAST(nodep->lhsp()->dtypep()->skipRefp(), UnpackArrayDType)) {
                    // copy between arrays
                    if (!VN_IS(lhsarrp->subDTypep()->skipRefp(), IfaceRefDType)) return;
                    if (!VN_IS(rhsarrp->subDTypep()->skipRefp(), IfaceRefDType)) return;
                    if (VN_AS(rhsarrp->subDTypep()->skipRefp(), IfaceRefDType)->isVirtual())
                        return;
                    if (!VN_AS(lhsarrp->subDTypep()->skipRefp(), IfaceRefDType)->isVirtual()) {
                        nodep->v3warn(E_UNSUPPORTED, "Unexpected target of interface assignment ["
                                                         << rhsarrp->prettyDTypeNameQ() << "]");
                        return;
                    }
                    if (lhsarrp->elementsConst() != rhsarrp->elementsConst()) {
                        nodep->v3warn(E_UNSUPPORTED,
                                      "Array size mismatch in interface assignment");
                        return;
                    }
                    for (int i = 0; i < lhsarrp->elementsConst(); ++i) {
                        const string index = AstNode::encodeNumber(i);
                        AstNodeExpr* lhsp = nullptr;
                        if (AstVarRef* const varrefp = VN_CAST(nodep->lhsp(), VarRef)) {
                            AstVarRef* const newvarp = varrefp->cloneTree(false);
                            AstArraySel* newarrselp = new AstArraySel{
                                nodep->fileline(), newvarp,
                                new AstConst{nodep->fileline(), static_cast<uint32_t>(i)}};
                            lhsp = newarrselp;
                        } else if (AstMemberSel* const prevselp
                                   = VN_CAST(nodep->lhsp(), MemberSel)) {
                            AstMemberSel* membselp = prevselp->cloneTree(false);
                            AstArraySel* newarrselp = new AstArraySel{
                                nodep->fileline(), membselp,
                                new AstConst{nodep->fileline(), static_cast<uint32_t>(i)}};
                            lhsp = newarrselp;
                        } else {
                            nodep->v3warn(E_UNSUPPORTED,
                                          "Unsupported lhs node type in array assignment");
                            return;
                        }
                        const AstVarRef* const rhsrefp = VN_CAST(nodep->rhsp(), VarRef);
                        AstVarXRef* const rhsp = new AstVarXRef{
                            nodep->fileline(), rhsrefp->name() + "__BRA__" + index + "__KET__", "",
                            VAccess::READ};
                        rhsp->dtypep(rhsarrp->subDTypep()->skipRefp());
                        rhsp->classOrPackagep(rhsrefp->classOrPackagep());
                        AstAssign* const assignp = new AstAssign(nodep->fileline(), lhsp, rhsp);
                        nodep->addNextHere(assignp);
                    }
                    VL_DO_DANGLING(pushDeletep(nodep->unlinkFrBack()), nodep);
                    return;
                }
            }
        }
        iterateChildren(nodep);
    }

    //--------------------
    void visit(AstNode* nodep) override { iterateChildren(nodep); }
    void visit(AstNew* nodep) override { iterateChildren(nodep); }
    void visit(AstMethodCall* nodep) override { iterateChildren(nodep); }
    void visit(AstArg* nodep) override { iterateChildren(nodep); }

public:
    // CONSTRUCTORS
    explicit InstDeVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~InstDeVisitor() override = default;
};

//######################################################################
// Inst static function

class InstStatic final {
    InstStatic() = default;  // Static class

    static AstNodeExpr* extendOrSel(FileLine* fl, AstNodeExpr* rhsp, AstNode* cmpWidthp) {
        if (cmpWidthp->width() > rhsp->width()) {
            rhsp = (rhsp->isSigned() ? static_cast<AstNodeExpr*>(new AstExtendS{fl, rhsp})
                                     : static_cast<AstNodeExpr*>(new AstExtend{fl, rhsp}));
            // Need proper widthMin, which may differ from AstSel created above
            rhsp->dtypeFrom(cmpWidthp);
        } else if (cmpWidthp->width() < rhsp->width()) {
            rhsp = new AstSel{fl, rhsp, 0, cmpWidthp->width()};
            // Need proper widthMin, which may differ from AstSel created above
            rhsp->dtypeFrom(cmpWidthp);
        }
        // else don't change dtype, as might be e.g. array of something
        return rhsp;
    }

public:
    static AstAssignW* pinReconnectSimple(AstPin* pinp, AstCell* cellp, bool forTristate,
                                          bool alwaysCvt) {
        // If a pin connection is "simple" leave it as-is
        // Else create a intermediate wire to perform the interconnect
        // Return the new assignment, if one was made
        // Note this module calls cloneTree() via new AstVar
        AstVar* const pinVarp = pinp->modVarp();
        if (!pinp->exprp()) {
            // No-connect, perhaps promote based on `unconnected_drive,
            // otherwise done
            if (pinVarp->direction() == VDirection::INPUT
                && cellp->modp()->unconnectedDrive().isSetTrue()) {
                pinp->exprp(new AstConst{pinp->fileline(), AstConst::All1{}});
            } else if (pinVarp->direction() == VDirection::INPUT
                       && cellp->modp()->unconnectedDrive().isSetFalse()) {
                pinp->exprp(new AstConst{pinp->fileline(), AstConst::All0{}});
            } else {
                return nullptr;
            }
        }
        const AstVarRef* const connectRefp = VN_CAST(pinp->exprp(), VarRef);
        const AstVarXRef* const connectXRefp = VN_CAST(pinp->exprp(), VarXRef);
        const AstNodeDType* const pinDTypep = pinVarp->dtypep()->skipRefp();
        const AstBasicDType* const pinBasicp = VN_CAST(pinDTypep, BasicDType);
        const AstNodeDType* const connDTypep
            = connectRefp ? connectRefp->varp()->dtypep()->skipRefp() : nullptr;
        const AstBasicDType* const connBasicp = VN_CAST(connDTypep, BasicDType);
        AstAssignW* assignp = nullptr;
        //
        if (!alwaysCvt && connectRefp && connDTypep->sameTree(pinDTypep)
            && !connectRefp->varp()->isSc()) {  // Need the signal as a 'shell' to convert types
            // Done. Same data type
        } else if (!alwaysCvt && connectRefp && connectRefp->varp()->isIfaceRef()) {
            // Done. Interface
        } else if (!alwaysCvt && connectXRefp && connectXRefp->varp()
                   && connectXRefp->varp()->isIfaceRef()) {
        } else if (!alwaysCvt && connBasicp && pinBasicp
                   && connBasicp->width() == pinBasicp->width()
                   && connBasicp->lo() == pinBasicp->lo()
                   && !connectRefp->varp()
                           ->isSc()  // Need the signal as a 'shell' to convert types
                   && connBasicp->width() == pinVarp->width()) {
            // Done. One to one interconnect won't need a temporary variable.
        } else if (!alwaysCvt && !forTristate && VN_IS(pinp->exprp(), Const)) {
            // Done. Constant.
        } else {
            // Make a new temp wire
            // if (1 || debug() >= 9) pinp->dumpTree("-  in_pin: ");
            V3Inst::checkOutputShort(pinp);
            AstNodeExpr* const pinexprp = VN_AS(pinp->exprp(), NodeExpr)->unlinkFrBack();
            const string newvarname
                = (string{pinVarp->isWritable() ? "__Vcellout" : "__Vcellinp"}
                   // Prevent name conflict if both tri & non-tri add signals
                   + (forTristate ? "t" : "") + "__" + cellp->name() + "__" + pinp->name());
            AstVar* const newvarp
                = new AstVar{pinVarp->fileline(), VVarType::MODULETEMP, newvarname, pinVarp};
            // Important to add statement next to cell, in case there is a
            // generate with same named cell
            cellp->addNextHere(newvarp);
            if (pinVarp->isInout()) {
                pinVarp->v3fatalSrc("Unsupported: Inout connections to pins must be"
                                    " direct one-to-one connection (without any expression)");
            } else if (pinVarp->isWritable()) {
                // See also V3Inst
                AstNodeExpr* rhsp = new AstVarRef{pinp->fileline(), newvarp, VAccess::READ};
                UINFO(5, "pinRecon width " << pinVarp->width() << " >? " << rhsp->width() << " >? "
                                           << pinexprp->width());
                rhsp = extendOrSel(pinp->fileline(), rhsp, pinVarp);
                pinp->exprp(new AstVarRef{newvarp->fileline(), newvarp, VAccess::WRITE});
                AstNodeExpr* const rhsSelp = extendOrSel(pinp->fileline(), rhsp, pinexprp);
                assignp = new AstAssignW{pinp->fileline(), pinexprp, rhsSelp};
            } else {
                // V3 width should have range/extended to make the widths correct
                assignp = new AstAssignW{pinp->fileline(),
                                         new AstVarRef{pinp->fileline(), newvarp, VAccess::WRITE},
                                         pinexprp};
                pinp->exprp(new AstVarRef{pinexprp->fileline(), newvarp, VAccess::READ});
            }
            if (assignp) cellp->addNextHere(assignp);
            // if (debug()) pinp->dumpTree("-  out: ");
            // if (debug()) assignp->dumpTree("-  aout: ");
        }
        return assignp;
    }
};

//######################################################################
// Inst class functions

AstAssignW* V3Inst::pinReconnectSimple(AstPin* pinp, AstCell* cellp, bool forTristate,
                                       bool alwaysCvt) {
    return InstStatic::pinReconnectSimple(pinp, cellp, forTristate, alwaysCvt);
}

void V3Inst::checkOutputShort(AstPin* nodep) {
    if (nodep->modVarp()->direction() == VDirection::OUTPUT) {
        if (VN_IS(nodep->exprp(), Const) || VN_IS(nodep->exprp(), Extend)
            || (VN_IS(nodep->exprp(), Concat)
                && (VN_IS(VN_AS(nodep->exprp(), Concat)->lhsp(), Const)))) {
            // Uses v3warn for error, as might be found multiple times
            nodep->v3warn(E_PORTSHORT, "Output port is connected to a constant pin,"
                                       " electrical short");
        }
    }
}

//######################################################################
// Inst class visitor

void V3Inst::instAll(AstNetlist* nodep) {
    UINFO(2, __FUNCTION__ << ":");
    { InstVisitor{nodep}; }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("inst", 0, dumpTreeEitherLevel() >= 3);
}

void V3Inst::dearrayAll(AstNetlist* nodep) {
    UINFO(2, __FUNCTION__ << ":");
    { InstDeVisitor{nodep}; }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("dearray", 0, dumpTreeEitherLevel() >= 6);
}
