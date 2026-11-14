//===--- CGWasmLegacyCallbacks.cpp - Legacy C callback addresses ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WebAssembly indirect calls require an exact signature. For an internal C
// definition with an empty parameter list, infer a single callback prototype
// from ALL its address uses while the source types are still available. Only
// ignored scalar arguments and an ignored scalar result can be adapted. Unknown
// escapes and conflicting prototypes keep the ordinary, strict call behavior.
//
// One canonical wrapper owns every address; direct calls still name the private
// original body. This is deliberately not a per-use cast thunk: two addresses of
// the same source function must compare equal, including across library calls.
// Firebox #5XP. Retire when upstream provides equivalent identity-preserving
// legacy callback compatibility, including thin-main/provider calls.
//
//===----------------------------------------------------------------------===//

#include "CGCall.h"
#include "CodeGenModule.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/IRBuilder.h"

using namespace clang;
using namespace CodeGen;

namespace {
class LegacyCallbackUses : public RecursiveASTVisitor<LegacyCallbackUses> {
  ASTContext &Context;

public:
  struct Candidate {
    const FunctionDecl *Definition = nullptr;
    QualType Prototype;
    bool Reject = false;
    bool Direct = false;
  };

  // AST visitation order, rather than pointer hash order, determines IR order.
  llvm::MapVector<const FunctionDecl *, Candidate> Candidates;
  bool HasAsm = false;

  explicit LegacyCallbackUses(ASTContext &Context) : Context(Context) {}

  bool VisitGCCAsmStmt(GCCAsmStmt *) {
    HasAsm = true;
    return true;
  }
  bool VisitMSAsmStmt(MSAsmStmt *) {
    HasAsm = true;
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    const auto *FD = dyn_cast<FunctionDecl>(E->getDecl());
    if (!FD)
      return true;
    const FunctionDecl *Definition = FD->getDefinition();
    if (!Definition || !Definition->getType()->isFunctionNoProtoType() ||
        Definition->getNumParams() ||
        Definition->getFormalLinkage() != Linkage::Internal)
      return true;
    // Later declarations can add a real prototype or an unaudited use without
    // updating the older definition's type or attributes.
    if (llvm::any_of(FD->redecls(), [](const FunctionDecl *R) {
          return R->getType()->isFunctionProtoType() || R->hasAttr<UsedAttr>() ||
                 R->hasAttr<RetainAttr>() || R->hasAttr<AsmLabelAttr>();
        }))
      return true;

    Candidate &C = Candidates[FD->getCanonicalDecl()];
    C.Definition = Definition;
    const Expr *Current = E;
    DynTypedNode Node = DynTypedNode::create(*E);
    for (;;) {
      auto Parents = Context.getParents(Node);
      if (Parents.size() != 1) {
        C.Reject = true;
        return true;
      }
      Node = Parents[0];
      if (const auto *Cast = Node.get<CastExpr>()) {
        // A void-pointer cast may be followed by a typed callback conversion
        // (a common old-C idiom). It is not itself evidence of a prototype.
        QualType T = Cast->getType();
        if (!T->isFunctionPointerType() && !T->isVoidPointerType()) {
          C.Reject = true;
          return true;
        }
        Current = Cast;
        continue;
      }
      if (const auto *P = Node.get<ParenExpr>()) {
        Current = P;
        continue;
      }
      if (const auto *U = Node.get<UnaryOperator>()) {
        if (U->getOpcode() == UO_AddrOf) {
          Current = U;
          continue;
        }
      }
      if (const auto *Conditional = Node.get<ConditionalOperator>()) {
        Current = Conditional;
        continue;
      }
      if (const auto *Generic = Node.get<GenericSelectionExpr>()) {
        if (Generic->getResultExpr() == Current) {
          Current = Generic;
          continue;
        }
      }
      if (const auto *Call = Node.get<CallExpr>()) {
        // A reference passed as an argument to a recursive call is an escape,
        // not a direct call of that reference.
        if (Call->getCallee() == Current && Call->getDirectCallee() &&
            Call->getDirectCallee()->getCanonicalDecl() ==
                FD->getCanonicalDecl()) {
          C.Direct = true;
          return true;
        }
        // A typed cast passed to an unprototyped or variadic consumer is
        // still an unknown escape: there is no destination callback type.
        const auto *Pointer =
            Call->getCallee()->getType()->getAs<PointerType>();
        const auto *Consumer = Pointer
                                   ? Pointer->getPointeeType()
                                         ->getAs<FunctionProtoType>()
                                   : nullptr;
        unsigned I = 0;
        while (I != Call->getNumArgs() && Call->getArg(I) != Current)
          ++I;
        if (!Consumer || I >= Consumer->getNumParams() ||
            !Context.hasSameType(Current->getType(),
                                 Consumer->getParamType(I))) {
          C.Reject = true;
          return true;
        }
      } else if (const auto *Binary = Node.get<BinaryOperator>()) {
        if ((!Binary->isAssignmentOp() || Binary->getRHS() != Current) &&
            Binary->getOpcode() != BO_EQ && Binary->getOpcode() != BO_NE) {
          C.Reject = true;
          return true;
        }
      } else if (!Node.get<VarDecl>() && !Node.get<ReturnStmt>() &&
                 !Node.get<InitListExpr>()) {
        C.Reject = true;
        return true;
      }

      QualType Expected;
      if (const auto *P = Current->getType()->getAs<PointerType>())
        if (P->getPointeeType()->isFunctionProtoType())
          Expected = Context.getCanonicalType(P->getPointeeType());
      // Inspect the FINAL conversion, never an earlier cast that was erased
      // again into void *, an unprototyped pointer, or another unknown escape.
      if (Expected.isNull() ||
          (!C.Prototype.isNull() &&
           !Context.hasSameType(C.Prototype, Expected)))
        C.Reject = true;
      else
        C.Prototype = Expected;
      return true;
    }
  }
};

bool isScalarABIType(QualType T) {
  return T->isPointerType() || T->isRealType();
}

// An alias, annotation, constructor list, or blockaddress can refer to a
// function without a source DeclRefExpr and therefore without the use audit.
bool hasUnauditedIRUse(llvm::Value *V,
                      llvm::SmallPtrSetImpl<llvm::Value *> &Seen) {
  if (!Seen.insert(V).second)
    return false;
  for (llvm::User *U : V->users()) {
    if (isa<llvm::GlobalAlias, llvm::GlobalIFunc, llvm::BlockAddress>(U))
      return true;
    if (auto *G = dyn_cast<llvm::GlobalVariable>(U)) {
      if (G->getName().starts_with("llvm."))
        return true;
    } else if (isa<llvm::Constant>(U) && hasUnauditedIRUse(U, Seen)) {
      return true;
    }
  }
  return false;
}
} // namespace

void CodeGenModule::EmitLegacyWasmCallbacks() {
  // Sanitizer type/entry instrumentation is tied to the source signature.
  // Leave it strict until wrappers participate in that instrumentation too.
  if (!getTriple().isWasm() || LangOpts.CPlusPlus || LangOpts.C23 ||
      !LangOpts.Sanitize.empty() ||
      !getModule().getModuleInlineAsm().empty())
    return;

  LegacyCallbackUses Uses(Context);
  Uses.TraverseDecl(Context.getTranslationUnitDecl());
  if (Uses.HasAsm)
    return;
  for (auto &Entry : Uses.Candidates) {
    auto &C = Entry.second;
    if (C.Reject || C.Prototype.isNull())
      continue;
    const auto *Prototype = C.Prototype->castAs<FunctionProtoType>();
    if (Prototype->isVariadic() || Prototype->getCallConv() != CC_C ||
        Prototype->hasExtParameterInfos() ||
        !llvm::all_of(Prototype->param_types(), isScalarABIType))
      continue;

    QualType OldReturn = C.Definition->getReturnType();
    QualType NewReturn = Prototype->getReturnType();
    bool Discard = NewReturn->isVoidType() && isScalarABIType(OldReturn);
    if ((!OldReturn->isVoidType() && !isScalarABIType(OldReturn)) ||
        (!Context.hasSameType(OldReturn, NewReturn) && (!Discard || C.Direct)))
      continue;
    auto *Body = getModule().getFunction(getMangledName(GlobalDecl(C.Definition)));
    if (!Body || Body->isDeclaration() || !Body->hasInternalLinkage() ||
        Body->arg_size() || Body->isVarArg() ||
        Body->getCallingConv() != llvm::CallingConv::C ||
        Body->getAddressSpace() != 0 ||
        Body->hasFnAttribute("wasm-export-name"))
      continue;
    // These lists are materialized by emitLLVMUsed() later in Release().
    // They can contain compiler-generated uses with no DeclRefExpr.
    auto IsBody = [Body](const llvm::WeakTrackingVH &V) {
      return static_cast<llvm::Value *>(V) == Body;
    };
    if (llvm::any_of(LLVMUsed, IsBody) || llvm::any_of(LLVMCompilerUsed, IsBody))
      continue;
    llvm::SmallPtrSet<llvm::Value *, 8> Seen;
    if (hasUnauditedIRUse(Body, Seen))
      continue;

    const CGFunctionInfo &Info = getTypes().arrangeFreeFunctionType(
        Context.getCanonicalType(C.Prototype).castAs<FunctionProtoType>());
    // Source scalars can still require indirect ABI lowering (e.g. wide
    // integers). Do not accidentally manufacture sret/byval/padding adapters.
    if ((!Info.getReturnInfo().isDirect() &&
         !Info.getReturnInfo().isExtend() &&
         !Info.getReturnInfo().isIgnore()) ||
        !llvm::all_of(Info.arguments(), [](const auto &A) {
          return A.info.isDirect() || A.info.isExtend();
        }))
      continue;
    llvm::FunctionType *FT = getTypes().GetFunctionType(Info);
    if (FT->getNumParams() != Prototype->getNumParams() ||
        (!Discard && FT->getReturnType() != Body->getReturnType()) ||
        FT == Body->getFunctionType())
      continue;

    std::string Symbol = Body->getName().str();
    Body->setName(Symbol + ".legacy.body");
    auto *Canonical = llvm::Function::Create(
        FT, llvm::GlobalValue::InternalLinkage, Symbol, getModule());
    llvm::AttributeList ABIAttrs;
    unsigned CallingConv;
    ConstructAttributeList(Symbol, Info, CGCalleeInfo(), ABIAttrs, CallingConv,
                           /*AttrOnCallSite=*/false, /*IsThunk=*/true);
    Canonical->setCallingConv(CallingConv);
    Canonical->setAttributes(ABIAttrs);
    Canonical->addFnAttrs(
        llvm::AttrBuilder(getLLVMContext(), Body->getAttributes().getFnAttrs()));

    Body->replaceUsesWithIf(Canonical, [](llvm::Use &U) {
      const auto *Call = dyn_cast<llvm::CallBase>(U.getUser());
      return !Call || !Call->isCallee(&U);
    });
    auto *BB = llvm::BasicBlock::Create(getLLVMContext(), "entry", Canonical);
    llvm::IRBuilder<> Builder(BB);
    auto *Call = Builder.CreateCall(Body->getFunctionType(), Body);
    Call->setCallingConv(Body->getCallingConv());
    Call->setAttributes(Body->getAttributes());
    if (NewReturn->isVoidType())
      Builder.CreateRetVoid();
    else
      Builder.CreateRet(Call);
  }
}
