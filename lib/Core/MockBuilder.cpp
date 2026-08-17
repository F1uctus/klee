//===-- MockBuilder.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//

#include "klee/Core/MockBuilder.h"

#include "klee/Config/Version.h"
#include "klee/Support/ErrorHandling.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <map>
#include <utility>

using namespace llvm;

namespace klee {

namespace {

/// Symbols that appear as externals in every C++ module but that must not be
/// mocked: they are static-initialisation bookkeeping with side effects on
/// state KLEE models itself, and a symbolic return value for them is
/// meaningless.
const std::set<std::string> alwaysIgnored = {
    "_ZNSt8ios_base4InitC1Ev",
    "_ZNSt8ios_base4InitD1Ev",
};

/// An alias is declared but resolves to a definition, so it is not external in
/// the sense that matters here.
template <typename T>
void removeAliases(const Module *userModule, std::map<std::string, T> &decls) {
  for (const auto &alias : userModule->aliases()) {
    decls.erase(alias.getName().str());
  }
}

} // namespace

MockBuilder::MockBuilder(const Module *initModule,
                         const Interpreter::ModuleOptions &opts,
                         const Interpreter::InterpreterOptions &interpreterOptions,
                         const std::set<std::string> &ignoredExternals)
    : userModule(initModule), ctx(initModule->getContext()), opts(opts),
      interpreterOptions(interpreterOptions),
      ignoredExternals(ignoredExternals) {
  this->ignoredExternals.insert(alwaysIgnored.begin(), alwaysIgnored.end());
}

MockBuilder::~MockBuilder() = default;

std::map<std::string, FunctionType *> MockBuilder::externalFunctions() const {
  std::map<std::string, FunctionType *> externals;
  for (const auto &f : userModule->functions()) {
    // use_empty() matters: a module routinely declares far more than it calls,
    // and synthesising bodies for the unused ones only slows the solver down
    // with dead symbolic objects.
    if (!f.isDeclaration() || f.use_empty())
      continue;
    // Intrinsics are lowered by LLVM or handled by KLEE directly; a synthesised
    // body would shadow that and produce nonsense.
    if (f.isIntrinsic())
      continue;
    if (ignoredExternals.count(f.getName().str()))
      continue;
    externals.emplace(f.getName().str(), f.getFunctionType());
  }
  removeAliases(userModule, externals);
  return externals;
}

std::map<std::string, const GlobalVariable *>
MockBuilder::externalGlobals() const {
  std::map<std::string, const GlobalVariable *> externals;
  for (const auto &global : userModule->globals()) {
    if (!global.isDeclaration())
      continue;
    if (ignoredExternals.count(global.getName().str()))
      continue;
    externals.emplace(global.getName().str(), &global);
  }
  removeAliases(userModule, externals);
  return externals;
}

void MockBuilder::callMakeMock(Value *source, Type *type,
                               const std::string &name, Value *args,
                               uint64_t argsBytes) {
  // klee_make_mock(void *addr, size_t nbytes, const char *name,
  //                void *args, size_t argsBytes)
  auto *voidPtrTy = PointerType::getUnqual(ctx);
  auto *sizeTy = Type::getInt64Ty(ctx);
  auto *signature = FunctionType::get(
      Type::getVoidTy(ctx), {voidPtrTy, sizeTy, voidPtrTy, voidPtrTy, sizeTy},
      /*isVarArg=*/false);
  auto callee = mockModule->getOrInsertFunction("klee_make_mock", signature);

  auto *nameValue = builder->CreateGlobalString(name);
  auto size = mockModule->getDataLayout().getTypeStoreSize(type);
  builder->CreateCall(callee, {source, ConstantInt::get(sizeTy, size), nameValue,
                               args ? args : Constant::getNullValue(voidPtrTy),
                               ConstantInt::get(sizeTy, argsBytes)});
}

Value *MockBuilder::spillArguments(Function *func, const std::string &name,
                                   uint64_t &argsBytes) {
  argsBytes = 0;
  if (interpreterOptions.MockStrategy != MockStrategyKind::Deterministic)
    return nullptr;

  // A varargs callee is the one case where nothing can be said. The unnamed
  // arguments are the ones that distinguish two calls, and a synthesised body
  // cannot reach them, so spilling the declared ones would judge calls equal
  // that differ in everything that mattered. Handing over no buffer at all is
  // how this tells the handler to leave such calls alone.
  if (func->isVarArg())
    return nullptr;

  if (func->arg_empty()) {
    // Nothing distinguishes two calls, which is what an empty argument list
    // should mean: a byte of zero compares equal to itself, so every call is
    // tied to the first. The buffer exists only to say so.
    auto *slot = builder->CreateAlloca(Type::getInt8Ty(ctx), nullptr,
                                       name + "_args");
    builder->CreateStore(ConstantInt::get(Type::getInt8Ty(ctx), 0), slot);
    argsBytes = 1;
    return slot;
  }

  // Packed, so that no padding byte lands between two arguments. The handler
  // compares the buffer byte by byte and has no layout to consult, so a padding
  // byte there would be compared as though it carried meaning.
  SmallVector<Type *, 8> argTypes;
  for (const auto &arg : func->args())
    argTypes.push_back(arg.getType());
  auto *packed = StructType::get(ctx, argTypes, /*isPacked=*/true);

  auto *slot = builder->CreateAlloca(packed, nullptr, name + "_args");
  unsigned index = 0;
  for (auto &arg : func->args()) {
    builder->CreateStore(&arg, builder->CreateStructGEP(packed, slot, index));
    ++index;
  }

  argsBytes = mockModule->getDataLayout().getTypeStoreSize(packed);
  return slot;
}

void MockBuilder::buildFunctionBodies() {
  for (const auto &[name, type] : externalFunctions()) {
    auto callee = mockModule->getOrInsertFunction(name, type);
    auto *func = dyn_cast<Function>(callee.getCallee());
    if (!func) {
      // getOrInsertFunction returns a bitcast when the module already holds a
      // symbol of that name with a different type. Nothing sound can be
      // synthesised in that case.
      klee_warning("Mock: skipping '%s', it is already declared with a "
                   "conflicting type",
                   name.c_str());
      continue;
    }
    if (!func->empty())
      continue;

    auto *entry = BasicBlock::Create(ctx, "entry", func);
    builder->SetInsertPoint(entry);

    auto *returnType = func->getReturnType();
    if (returnType->isVoidTy()) {
      // Nothing to be symbolic about: the call's only effect would have been
      // through its arguments, which mocking does not model.
      builder->CreateRetVoid();
    } else if (!returnType->isSized()) {
      // An opaque return type has no size to make symbolic, so there is no
      // honest value to invent.
      klee_warning("Mock: skipping '%s', its return type has no size",
                   name.c_str());
      func->deleteBody();
      continue;
    } else {
      auto *slot = builder->CreateAlloca(returnType, nullptr, name + "_result");
      uint64_t argsBytes = 0;
      auto *args = spillArguments(func, name, argsBytes);
      callMakeMock(slot, returnType, name, args, argsBytes);
      builder->CreateRet(builder->CreateLoad(returnType, slot));
    }

    klee_message("Mocking external function %s", name.c_str());
  }
}

void MockBuilder::buildGlobalInitialiser() {
  auto globals = externalGlobals();
  if (globals.empty())
    return;

  // Globals cannot be made symbolic where they are declared, so the work goes
  // into a constructor. KLEE injects llvm.global_ctors into the entry function,
  // so this runs before anything reads them.
  auto *ctorType = FunctionType::get(Type::getVoidTy(ctx), /*isVarArg=*/false);
  auto *ctor = Function::Create(ctorType, GlobalValue::InternalLinkage,
                                "klee.mock_globals_ctor", mockModule.get());
  auto *entry = BasicBlock::Create(ctx, "entry", ctor);
  builder->SetInsertPoint(entry);

  bool any = false;
  for (const auto &[name, global] : globals) {
    auto *elementType = global->getValueType();
    if (!elementType->isSized()) {
      klee_warning("Mock: skipping global '%s', its type has no size",
                   name.c_str());
      continue;
    }

    auto *definition = dyn_cast_or_null<GlobalVariable>(
        mockModule->getOrInsertGlobal(name, elementType));
    if (!definition) {
      klee_warning("Mock: unable to define global '%s'", name.c_str());
      continue;
    }
    // A declaration becomes a definition only once it has an initialiser; the
    // symbolic value is then written over it by the constructor.
    definition->setInitializer(Constant::getNullValue(elementType));
    definition->setLinkage(GlobalValue::ExternalLinkage);

    callMakeMock(definition, elementType, "external_" + name);
    klee_message("Mocking external variable %s", name.c_str());
    any = true;
  }

  builder->CreateRetVoid();

  if (!any) {
    ctor->eraseFromParent();
    return;
  }
  appendToGlobalCtors(*mockModule, ctor, /*Priority=*/65535);
}

std::unique_ptr<Module> MockBuilder::build() {
  mockModule = std::make_unique<Module>(
      userModule->getName().str() + "__klee_mocks", ctx);
  // The mock module is linked into the module under test, so it has to agree
  // with it about pointer width and endianness -- which for firmware bitcode is
  // never the host's.
  mockModule->setTargetTriple(userModule->getTargetTriple());
  mockModule->setDataLayout(userModule->getDataLayout());
  builder = std::make_unique<Builder>(ctx);

  buildFunctionBodies();
  if (interpreterOptions.Mock == MockPolicy::All)
    buildGlobalInitialiser();

  builder.reset();

  if (mockModule->empty() && mockModule->global_empty())
    return nullptr;
  return std::move(mockModule);
}

} // namespace klee
