//===-- MockBuilder.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//

#ifndef KLEE_MOCKBUILDER_H
#define KLEE_MOCKBUILDER_H

#include "klee/Core/Interpreter.h"

#include "llvm/IR/IRBuilder.h"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace llvm {
class FunctionType;
class GlobalVariable;
class LLVMContext;
class Module;
} // namespace llvm

namespace klee {

/// Synthesises bodies for the functions a module declares but does not define.
///
/// The result is a separate module that KLEE links in alongside the one under
/// test. Each synthesised body produces a fresh symbolic value of the declared
/// return type rather than calling anything, so analysis never leaves the
/// bitcode.
///
/// This is what makes firmware tractable. A driver or HAL entry point is an
/// external call as far as KLEE is concerned, and for a cross-architecture
/// module it cannot be dispatched at all -- an x86-64 host cannot call a
/// function using the ARM ABI. Terminating the state is the only alternative,
/// so without mocking, analysis stops at the first hardware access.
class MockBuilder {
public:
  MockBuilder(const llvm::Module *initModule,
              const Interpreter::ModuleOptions &opts,
              const Interpreter::InterpreterOptions &interpreterOptions,
              const std::set<std::string> &ignoredExternals);
  ~MockBuilder();

  /// Returns the module of synthesised definitions, or nullptr if the user
  /// module declares nothing that needs one.
  std::unique_ptr<llvm::Module> build();

private:
  using Builder = llvm::IRBuilder<>;

  /// Declared, used, and not defined anywhere in the user module.
  std::map<std::string, llvm::FunctionType *> externalFunctions() const;
  std::map<std::string, const llvm::GlobalVariable *> externalGlobals() const;

  void buildFunctionBodies();
  void buildGlobalInitialiser();

  /// Emits a klee_make_mock(&source, sizeof(type), name) call.
  void callMakeMock(llvm::Value *source, llvm::Type *type,
                    const std::string &name);

  const llvm::Module *userModule;
  llvm::LLVMContext &ctx;
  const Interpreter::ModuleOptions &opts;
  const Interpreter::InterpreterOptions &interpreterOptions;
  std::set<std::string> ignoredExternals;

  std::unique_ptr<llvm::Module> mockModule;
  std::unique_ptr<Builder> builder;
};

} // namespace klee

#endif // KLEE_MOCKBUILDER_H
