//===-- Interpreter.h - Abstract Execution Engine Interface -----*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//

#ifndef KLEE_INTERPRETER_H
#define KLEE_INTERPRETER_H

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct KTest;

namespace llvm {
class Function;
class LLVMContext;
class Module;
class raw_ostream;
class raw_fd_ostream;
}

namespace klee {
class ExecutionState;
class Interpreter;
class TreeStreamWriter;

class InterpreterHandler {
public:
  InterpreterHandler() {}
  virtual ~InterpreterHandler() {}

  virtual llvm::raw_ostream &getInfoStream() const = 0;

  virtual std::string getOutputFilename(const std::string &filename) = 0;
  virtual std::unique_ptr<llvm::raw_fd_ostream> openOutputFile(const std::string &filename) = 0;

  virtual void incPathsCompleted() = 0;
  virtual void incPathsExplored(std::uint32_t num = 1) = 0;

  virtual void processTestCase(const ExecutionState &state,
                               const char *err,
                               const char *suffix) = 0;
};

/// Which external functions get a synthesised body.
///
/// An external call is one KLEE has no bitcode for. By default it is dispatched
/// to the real function on the host, which is impossible when the module is for
/// another architecture -- firmware bitcode always is -- and undesirable when
/// the callee is a hardware register access or a driver entry point. Mocking
/// replaces the call with a fresh symbolic value of the return type instead.
enum class MockPolicy {
  None,   // Do not synthesise anything.
  Failed, // Only for calls that could not be dispatched, so a mock is the
          // alternative to terminating the state.
  All     // For every external, decided up front while building the module.
};

/// How a mocked call decides what to return.
enum class MockStrategyKind {
  Naive,        // A fresh symbolic value per call.
  Deterministic // The function is an uninterpreted function in the solver, so
                // equal arguments give equal results.
};

class Interpreter {
public:
  /// ModuleOptions - Module level options which can be set when
  /// registering a module with the interpreter.
  struct ModuleOptions {
    std::string LibraryDir;
    std::string EntryPoint;
    std::string OptSuffix;
    bool Optimize;
    bool CheckDivZero;
    bool CheckOvershift;

    ModuleOptions(const std::string &_LibraryDir,
                  const std::string &_EntryPoint, const std::string &_OptSuffix,
                  bool _Optimize, bool _CheckDivZero, bool _CheckOvershift)
        : LibraryDir(_LibraryDir), EntryPoint(_EntryPoint),
          OptSuffix(_OptSuffix), Optimize(_Optimize),
          CheckDivZero(_CheckDivZero), CheckOvershift(_CheckOvershift) {}
  };

  enum LogType
  {
	  STP, //.CVC (STP's native language)
	  KQUERY, //.KQUERY files (kQuery native language)
	  SMTLIB2 //.SMT2 files (SMTLIB version 2 files)
  };

  /// InterpreterOptions - Options varying the runtime behavior during
  /// interpretation.
  struct InterpreterOptions {
    /// A frequency at which to make concrete reads return constrained
    /// symbolic values. This is used to test the correctness of the
    /// symbolic execution on concrete programs.
    unsigned MakeConcreteSymbolic;

    /// Which external functions to answer with a symbolic value instead of
    /// calling for real.
    MockPolicy Mock;

    /// What a mocked call returns.
    MockStrategyKind MockStrategy;

    InterpreterOptions()
        : MakeConcreteSymbolic(false), Mock(MockPolicy::None),
          MockStrategy(MockStrategyKind::Naive) {}
  };

protected:
  const InterpreterOptions interpreterOpts;

  Interpreter(const InterpreterOptions &_interpreterOpts)
    : interpreterOpts(_interpreterOpts)
  {}

public:
  virtual ~Interpreter() {}

  static Interpreter *create(llvm::LLVMContext &ctx,
                             const InterpreterOptions &_interpreterOpts,
                             InterpreterHandler *ih);

  /// Register the module to be executed.
  /// \param modules A list of modules that should form the final
  ///                module
  /// \return The final module after it has been optimized, checks
  /// inserted, and modified for interpretation.
  virtual llvm::Module *
  setModule(std::vector<std::unique_ptr<llvm::Module>> &modules,
            const ModuleOptions &opts) = 0;

  // supply a tree stream writer which the interpreter will use
  // to record the concrete path (as a stream of '0' and '1' bytes).
  virtual void setPathWriter(TreeStreamWriter *tsw) = 0;

  // supply a tree stream writer which the interpreter will use
  // to record the symbolic path (as a stream of '0' and '1' bytes).
  virtual void setSymbolicPathWriter(TreeStreamWriter *tsw) = 0;

  // supply a test case to replay from. this can be used to drive the
  // interpretation down a user specified path. use null to reset.
  virtual void setReplayKTest(const struct KTest *out) = 0;

  // supply a list of branch decisions specifying which direction to
  // take on forks. this can be used to drive the interpretation down
  // a user specified path. use null to reset.
  virtual void setReplayPath(const std::vector<bool> *path) = 0;

  // supply a set of symbolic bindings that will be used as "seeds"
  // for the search. use null to reset.
  virtual void useSeeds(const std::vector<struct KTest *> *seeds) = 0;

  virtual void runFunctionAsMain(llvm::Function *f,
                                 int argc,
                                 char **argv,
                                 char **envp) = 0;

  /// Start execution at \p f with every parameter symbolic, rather than
  /// passing argc/argv. This is what lets a function be analysed without a
  /// hand-written harness that calls klee_make_symbolic for each argument.
  virtual void runFunctionSymbolically(llvm::Function *f) = 0;

  /*** Runtime options ***/

  virtual void setHaltExecution(bool value) = 0;

  virtual void setInhibitForking(bool value) = 0;

  virtual void prepareForEarlyExit() = 0;

  /*** State accessor methods ***/

  virtual unsigned getPathStreamID(const ExecutionState &state) = 0;

  virtual unsigned getSymbolicPathStreamID(const ExecutionState &state) = 0;

  virtual void getConstraintLog(const ExecutionState &state,
                                std::string &res,
                                LogType logFormat = STP) = 0;

  virtual bool getSymbolicSolution(const ExecutionState &state,
                                   std::vector<
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res) = 0;

  virtual void getCoveredLines(const ExecutionState &state,
                               std::map<const std::string*, std::set<unsigned> > &res) = 0;
};

} // End klee namespace

#endif /* KLEE_INTERPRETER_H */
