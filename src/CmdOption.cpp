#include "CmdOption.h"

namespace ATC {
llvm::cl::OptionCategory MyCategory("ATC");

llvm::cl::opt<std::string> SrcPath(llvm::cl::Positional, llvm::cl::desc("file"), llvm::cl::cat(MyCategory));

llvm::cl::opt<bool> EmitLLVM("emit-llvm", llvm::cl::desc("emit llvm code"), llvm::cl::init(false),
                             llvm::cl::cat(MyCategory));

llvm::cl::opt<std::string> OtherSrc("other-src", llvm::cl::desc("other src which need to be compiled by clang"),
                                    llvm::cl::cat(MyCategory));

llvm::cl::opt<bool> RunAfterCompiling("R", llvm::cl::desc("run after compiling"), llvm::cl::init(false),
                                      llvm::cl::cat(MyCategory));

llvm::cl::opt<std::string> RunInput(("R-input",
                                     llvm::cl::desc("input file for program which will run after compiling")),
                                    llvm::cl::cat(MyCategory));

llvm::cl::opt<bool> DumpAst("dump-ast", llvm::cl::desc("dump the ATC Ast"), llvm::cl::cat(MyCategory));

llvm::cl::opt<bool> Check("check", llvm::cl::desc("check after running"), llvm::cl::cat(MyCategory));

llvm::cl::opt<std::string> CompareFile("compare-file",
                                       llvm::cl::desc("right output for program which will run after compiling"),
                                       llvm::cl::cat(MyCategory));

llvm::cl::opt<std::string> CompareResult("compare-result", llvm::cl::desc("file path where compare result dump to"),
                                         llvm::cl::cat(MyCategory));

}  // namespace ATC