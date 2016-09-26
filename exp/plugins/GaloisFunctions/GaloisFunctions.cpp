/*
 * Author : Gurbinder Gill (gurbinder533@gmail.com)
 */
#include <sstream>
#include <climits>
#include <vector>
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

/*
 *  * Matchers
 *   */

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

/*
 * Rewriter
 */

#include "clang/Rewrite/Core/Rewriter.h"


using namespace clang;
using namespace clang::ast_matchers;
using namespace llvm;
using namespace std;

#define __GALOIS_PREPROCESS_GLOBAL_VARIABLE_PREFIX__ "local_"
#define __GALOIS_ACCUMULATOR_TYPE__ "Galois::DGAccumulator"

namespace {


  class GaloisFunctionsVisitor : public RecursiveASTVisitor<GaloisFunctionsVisitor> {
  private:
    ASTContext* astContext;

  public:
    explicit GaloisFunctionsVisitor(CompilerInstance *CI) : astContext(&(CI->getASTContext())){}

    virtual bool VisitCXXRecordDecl(CXXRecordDecl* Dec){
      Dec->dump();
      return true;
    }
    virtual bool VisitFunctionDecl(FunctionDecl* func){
      //std::string funcName = func->getNameInfo().getName().getAsString();
      std::string funcName = func->getNameAsString();
      if (funcName == "foo"){
        llvm::errs() << "Found" << "\n";
      }
      return true;
    }
  };


  class NameSpaceHandler : public MatchFinder::MatchCallback {
    //CompilerInstance &Instance;
    clang::LangOptions& langOptions; //Instance.getLangOpts();
  public:
    NameSpaceHandler(clang::LangOptions &langOptions) : langOptions(langOptions){}
    virtual void run(const MatchFinder::MatchResult &Results){
      llvm::errs() << "It is coming here\n"
                   << Results.Nodes.getNodeAs<clang::NestedNameSpecifier>("galoisLoop")->getAsNamespace()->getIdentifier()->getName();
      if (const NestedNameSpecifier* NSDecl = Results.Nodes.getNodeAs<clang::NestedNameSpecifier>("galoisLoop")) {
        llvm::errs() << "Found Galois Loop\n";
        //NSDecl->dump(langOptions);
      }
    }
  };

  class ForStmtHandler : public MatchFinder::MatchCallback {
    public:
      virtual void run(const MatchFinder::MatchResult &Results) {
        if (const ForStmt* FS = Results.Nodes.getNodeAs<clang::ForStmt>("forLoop")) {
          llvm::errs() << "for loop found\n";
          FS->dump();
        }
      }
  };

  struct Write_set {
    string GRAPH_NAME;
    string NODE_TYPE;
    string FIELD_TYPE;
    string FIELD_NAME;
    string REDUCE_OP_EXPR;
    string VAL_TYPE;
    string RESET_VAL_EXPR;
    string SYNC_TYPE;
  };

  struct Write_set_PULL {
    string GRAPH_NAME;
    string NODE_TYPE;
    string FIELD_TYPE;
    string FIELD_NAME;
    string VAL_TYPE;
  };

  std::string getSyncer(unsigned counter, const Write_set& i, std::string struct_type="") {
    std::stringstream s;
    s << "\tstruct Syncer_" << struct_type << counter << " {\n";
    s << "\t\tstatic " << i.VAL_TYPE <<" extract(uint32_t node_id, const " << i.NODE_TYPE << " node) {\n" 
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) return " << "get_node_" << i.FIELD_NAME <<  "_cuda(cuda_ctx, node_id);\n"
      << "\t\t\tassert (personality == CPU);\n"
      << "\t\t#endif\n"
      << "\t\t\treturn " << "node." << i.FIELD_NAME <<  ";\n"
      << "\t\t}\n";
    s << "\t\tstatic bool extract_reset_batch(unsigned from_id, " << i.VAL_TYPE << " *y) {\n" 
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) { " << "batch_get_reset_node_" << i.FIELD_NAME 
      <<  "_cuda(cuda_ctx, from_id, y, " << i.RESET_VAL_EXPR << "); return true; }\n"
      << "\t\t\tassert (personality == CPU);\n"
      << "\t\t#endif\n"
      << "\t\t\treturn false;\n"
      << "\t\t}\n";
    s << "\t\tstatic void reduce (uint32_t node_id, " << i.NODE_TYPE << " node, " << i.VAL_TYPE << " y) {\n" 
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) " << i.REDUCE_OP_EXPR << "_node_" << i.FIELD_NAME <<  "_cuda(cuda_ctx, node_id, y);\n"
      << "\t\t\telse if (personality == CPU)\n"
      << "\t\t#endif\n"
      << "\t\t\t\t{ Galois::" << i.REDUCE_OP_EXPR << "(node." << i.FIELD_NAME  << ", y); }\n"
      << "\t\t}\n";
    s << "\t\tstatic bool reduce_batch(unsigned from_id, " << i.VAL_TYPE << " *y) {\n" 
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) { " << "batch_" << i.REDUCE_OP_EXPR << "_node_" << i.FIELD_NAME 
      <<  "_cuda(cuda_ctx, from_id, y); return true; }\n"
      << "\t\t\tassert (personality == CPU);\n"
      << "\t\t#endif\n"
      << "\t\t\treturn false;\n"
      << "\t\t}\n";
    s << "\t\tstatic void reset (uint32_t node_id, " << i.NODE_TYPE << " node ) {\n" 
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) " << "set_node_" << i.FIELD_NAME <<  "_cuda(cuda_ctx, node_id, " << i.RESET_VAL_EXPR << ");\n"
      << "\t\t\telse if (personality == CPU)\n"
      << "\t\t#endif\n"
      << "\t\t\t\t{ node." << i.FIELD_NAME << " = " << i.RESET_VAL_EXPR << "; }\n"
      << "\t\t}\n";
    s << "\t\ttypedef " << i.VAL_TYPE << " ValTy;\n"
      << "\t};\n";
    return s.str();
  }

  std::string getSyncerPull(unsigned counter, const Write_set& i, std::string struct_type="") {
    std::stringstream s;
    s << "\tstruct SyncerPull_" << struct_type << counter << " {\n";
    s << "\t\tstatic " << i.VAL_TYPE <<" extract(uint32_t node_id, const " << i.NODE_TYPE << " node) {\n"
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) return " << "get_node_" << i.FIELD_NAME <<  "_cuda(cuda_ctx, node_id);\n"
      << "\t\t\tassert (personality == CPU);\n"
      << "\t\t#endif\n"
      << "\t\t\treturn " << "node." << i.FIELD_NAME <<  ";\n"
      << "\t\t}\n";
    s << "\t\tstatic bool extract_batch(unsigned from_id, " << i.VAL_TYPE << " *y) {\n" 
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) { " << "batch_get_node_" << i.FIELD_NAME 
      <<  "_cuda(cuda_ctx, from_id, y); return true; }\n"
      << "\t\t\tassert (personality == CPU);\n"
      << "\t\t#endif\n"
      << "\t\t\treturn false;\n"
      << "\t\t}\n";
    s << "\t\tstatic void setVal (uint32_t node_id, " << i.NODE_TYPE << " node, " << i.VAL_TYPE << " y) " << "{\n"
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) " << "set_node_" << i.FIELD_NAME <<  "_cuda(cuda_ctx, node_id, y);\n"
      << "\t\t\telse if (personality == CPU)\n"
      << "\t\t#endif\n"
      << "\t\t\t\tnode." << i.FIELD_NAME << " = y;\n"
      << "\t\t}\n";
    s << "\t\tstatic bool setVal_batch(unsigned from_id, " << i.VAL_TYPE << " *y) {\n" 
      << "\t\t#ifdef __GALOIS_HET_CUDA__\n"
      << "\t\t\tif (personality == GPU_CUDA) { " << "batch_set_node_" << i.FIELD_NAME 
      <<  "_cuda(cuda_ctx, from_id, y); return true; }\n"
      << "\t\t\tassert (personality == CPU);\n"
      << "\t\t#endif\n"
      << "\t\t\treturn false;\n"
      << "\t\t}\n";
    s << "\t\ttypedef " << i.VAL_TYPE << " ValTy;\n"
      << "\t};\n";
    return s.str();
  }

  class FunctionForEachHandler : public MatchFinder::MatchCallback {
    private:
      Rewriter &rewriter;

    public:
      FunctionForEachHandler(Rewriter &rewriter) : rewriter(rewriter){}
      virtual void run(const MatchFinder::MatchResult &Results) {
        llvm::errs() << "for_each found\n";
        const CallExpr* callFS = Results.Nodes.getNodeAs<clang::CallExpr>("galoisLoop_forEach");
        auto callerFS = Results.Nodes.getNodeAs<clang::CXXRecordDecl>("class");

        //callerFS->dump();
        if(callFS){

          std::string OperatorStructName = callerFS->getNameAsString();
          llvm::outs() << " NAME OF THE STRUCT : " << OperatorStructName << "\n";
          llvm::errs() << "It is coming here for_Each \n" << callFS->getNumArgs() << "\n";

          SourceLocation ST_main = callerFS->getSourceRange().getBegin();

          stringstream SSHelperStructFunctions;
          SSHelperStructFunctions << "template <typename GraphTy>\n"
                                  << "struct Get_info_functor : public Galois::op_tag {\n"
                                  << "\tGraphTy &graph;\n";
          rewriter.InsertText(ST_main, SSHelperStructFunctions.str(), true, true);
          SSHelperStructFunctions.str(string());
          SSHelperStructFunctions.clear();


          //Get the arguments:
          clang::LangOptions LangOpts;
          LangOpts.CPlusPlus = true;
          clang::PrintingPolicy Policy(LangOpts);

          // Vector to store read and write set.
          vector<pair<string, string>>read_set_vec;
          vector<pair<string,string>>write_set_vec;
          vector<string>write_setGNode_vec;
          string GraphNode;

          vector<Write_set> write_set_vec_PUSH;
          vector<Write_set> write_set_vec_PULL;


          for(int i = 0, j = callFS->getNumArgs(); i < j; ++i){
            string str_arg;
            llvm::raw_string_ostream s(str_arg);
            callFS->getArg(i)->printPretty(s, 0, Policy);

            //callFS->getArg(i)->IgnoreParenImpCasts()->dump();
            const CallExpr* callExpr = dyn_cast<CallExpr>(callFS->getArg(i)->IgnoreParenImpCasts());
            if (callExpr) {
              const FunctionDecl* func = callExpr->getDirectCallee();
              //func->dump();
              if(func) {
                if (func->getNameInfo().getName().getAsString() == "read_set"){
                  llvm::errs() << "Inside arg read_set: " << i <<  s.str() << "\n\n";
                }
                if (func->getNameInfo().getName().getAsString() == "write_set"){
                  llvm::errs() << "Inside arg write_set: " << i <<  s.str() << "\n\n";

                  // Take out first argument to see if it is push or pull
                  string str_sub_arg;
                  llvm::raw_string_ostream s_sub(str_sub_arg);
                  callExpr->getArg(0)->printPretty(s_sub, 0, Policy);

                  string str_sync_type = s_sub.str();
                  if(str_sync_type.front() == '"'){
                    str_sync_type.erase(0,1);
                    str_sync_type.erase(str_sync_type.size()-1);
                  }

                  //SYNC_PUSH
                  //if(callExpr->getNumArgs() == 8) {
                  if(str_sync_type == "sync_push") {
                    vector<string> Temp_vec_PUSH;
                    for(unsigned i = 0; i < callExpr->getNumArgs(); ++i){
                      string str_sub_arg;
                      llvm::raw_string_ostream s_sub(str_sub_arg);
                      callExpr->getArg(i)->printPretty(s_sub, 0, Policy);

                      string temp_str = s_sub.str();
                      if(temp_str.front() == '"'){
                        temp_str.erase(0,1);
                        temp_str.erase(temp_str.size()-1);
                      }
                      if(temp_str.front() == '&'){
                        temp_str.erase(0,1);
                      }

                      llvm::errs() << " ------- > " << temp_str << "\n";
                      Temp_vec_PUSH.push_back(temp_str);

                    }
                    Write_set WR_entry;
                    WR_entry.GRAPH_NAME = Temp_vec_PUSH[1];
                    WR_entry.NODE_TYPE = Temp_vec_PUSH[2];
                    WR_entry.FIELD_TYPE = Temp_vec_PUSH[3];
                    WR_entry.FIELD_NAME = Temp_vec_PUSH[4];
                    WR_entry.VAL_TYPE = Temp_vec_PUSH[5];
                    WR_entry.REDUCE_OP_EXPR = Temp_vec_PUSH[6];
                    WR_entry.RESET_VAL_EXPR = Temp_vec_PUSH[7];
                    WR_entry.SYNC_TYPE = "sync_push";

                    write_set_vec_PUSH.push_back(WR_entry);
                  }

                  //SYNC_PULL
                  else if(str_sync_type == "sync_pull") {
                    vector<string> Temp_vec_PULL;
                    for(unsigned i = 0; i < callExpr->getNumArgs(); ++i){
                      string str_sub_arg;
                      llvm::raw_string_ostream s_sub(str_sub_arg);
                      callExpr->getArg(i)->printPretty(s_sub, 0, Policy);

                      string temp_str = s_sub.str();
                      if(temp_str.front() == '"'){
                        temp_str.erase(0,1);
                        temp_str.erase(temp_str.size()-1);
                      }
                      if(temp_str.front() == '&'){
                        temp_str.erase(0,1);
                      }

                      Temp_vec_PULL.push_back(temp_str);

                    }
                    Write_set WR_entry;
                    WR_entry.GRAPH_NAME = Temp_vec_PULL[1];
                    WR_entry.NODE_TYPE = Temp_vec_PULL[2];
                    WR_entry.FIELD_TYPE = Temp_vec_PULL[3];
                    WR_entry.FIELD_NAME = Temp_vec_PULL[4];
                    WR_entry.VAL_TYPE = Temp_vec_PULL[5];
                    WR_entry.REDUCE_OP_EXPR = Temp_vec_PULL[6];
                    WR_entry.RESET_VAL_EXPR = Temp_vec_PULL[7];
                    WR_entry.SYNC_TYPE = "sync_pull";

                    write_set_vec_PULL.push_back(WR_entry);
                  }
                 }
                }
              }
            }

            /************************************
             * Find unique one from push and pull
             * sets to generate for vertex cut
             * *********************************/

            vector<Write_set> write_set_vec_PUSH_PULL;
            for(auto i : write_set_vec_PUSH){
              bool found = false;
              for(auto j : write_set_vec_PULL){
                if(i.GRAPH_NAME == j.GRAPH_NAME && i.NODE_TYPE == j.NODE_TYPE && i.FIELD_TYPE == j.FIELD_TYPE && i.FIELD_NAME == j.FIELD_NAME){
                  found = true;
                }
              }
              if(!found)
                write_set_vec_PUSH_PULL.push_back(i);
            }
            for(auto i : write_set_vec_PULL){
              bool found = false;
              for(auto j : write_set_vec_PUSH){
                if(i.GRAPH_NAME == j.GRAPH_NAME && i.NODE_TYPE == j.NODE_TYPE && i.FIELD_TYPE == j.FIELD_TYPE && i.FIELD_NAME == j.FIELD_NAME){
                  found = true;
                }
              }
              if(!found)
                write_set_vec_PUSH_PULL.push_back(i);
            }


            stringstream SSSyncer;
            unsigned counter = 0;
            for(auto i : write_set_vec_PUSH) {
              SSSyncer << getSyncer(counter, i);
              rewriter.InsertText(ST_main, SSSyncer.str(), true, true);
              SSSyncer.str(string());
              SSSyncer.clear();
              ++counter;
            }

            // Addding structure for pull sync
            stringstream SSSyncer_pull;
            counter = 0;
            for(auto i : write_set_vec_PULL) {
              SSSyncer_pull << getSyncerPull(counter, i);
              rewriter.InsertText(ST_main, SSSyncer_pull.str(), true, true);
              SSSyncer_pull.str(string());
              SSSyncer_pull.clear();
              ++counter;
            }

            // Addding additional structs for vertex cut
            stringstream SSSyncer_vertexCut;
            counter = 0;
            for(auto i : write_set_vec_PUSH_PULL) {
              if(i.SYNC_TYPE == "sync_push"){
                SSSyncer_vertexCut << getSyncerPull(counter, i, "vertexCut_");
                rewriter.InsertText(ST_main, SSSyncer_vertexCut.str(), true, true);
                SSSyncer_vertexCut.str(string());
                SSSyncer_vertexCut.clear();
                ++counter;
              }
              else if(i.SYNC_TYPE == "sync_pull"){
                SSSyncer_vertexCut << getSyncer(counter, i, "vertexCut_");
                rewriter.InsertText(ST_main, SSSyncer_vertexCut.str(), true, true);
                SSSyncer_vertexCut.str(string());
                SSSyncer_vertexCut.clear();
                ++counter;
              }
            }

            // Adding helper function
            SSHelperStructFunctions <<"\tGet_info_functor(GraphTy& _g): graph(_g){}\n"
                                    <<"\tunsigned operator()(GNode n) const {\n"
                                    <<"\t\treturn graph.getHostID(n);\n\t}\n"
                                    <<"\tGNode getGNode(uint32_t local_id) const {\n"
                                    <<"\t\treturn GNode(graph.getGID(local_id));\n\t}\n"
                                    <<"\tuint32_t getLocalID(GNode n) const {\n"
                                    <<"\t\treturn graph.getLID(n);\n\t}\n"
                                    <<"\tvoid sync_graph(){\n"
                                    <<"\t\tsync_graph_static(graph);\n\t}\n"
                                    <<"\tuint32_t get_run_num() const {\n"
                                    <<"\t\treturn graph.get_run_num();\n\t}\n"
                                    <<"\tvoid static sync_graph_static(Graph& _graph) {\n";

            rewriter.InsertText(ST_main, SSHelperStructFunctions.str(), true, true);
            SSHelperStructFunctions.str(string());
            SSHelperStructFunctions.clear();

            // Adding sync calls for write set
            stringstream SSAfter;
            for (unsigned i = 0; i < write_set_vec_PUSH.size(); i++) {
              SSAfter <<"\n" << "\t\t_graph.sync_push<Syncer_" << i << ">" <<"(\"" << OperatorStructName << "\");\n";
              rewriter.InsertText(ST_main, SSAfter.str(), true, true);
              SSAfter.str(string());
              SSAfter.clear();
            }

            //For sync Pull and push for vertex cut
            for (unsigned i = 0; i < write_set_vec_PUSH_PULL.size(); i++) {
              if(write_set_vec_PUSH_PULL[i].SYNC_TYPE == "sync_pull"){
                SSAfter << "\n" << "if(_graph.is_vertex_cut()) {";
                SSAfter <<"\n\t" << "_graph.sync_push<Syncer_vertexCut_" << i << ">" <<"(\"" << OperatorStructName << "\");";
                SSAfter << "\n}\n";
                rewriter.InsertText(ST_main, SSAfter.str(), true, true);
              }
              SSAfter.str(string());
              SSAfter.clear();
            }

            //For sync Pull
            for (unsigned i = 0; i < write_set_vec_PULL.size(); i++) {
              SSAfter <<"\n" << "\t\t_graph.sync_pull<SyncerPull_" << i << ">" <<"(\"" << OperatorStructName << "\");\n";
              rewriter.InsertText(ST_main, SSAfter.str(), true, true);
              SSAfter.str(string());
              SSAfter.clear();
            }

            //For sync Pull and push for vertex cut
            for (unsigned i = 0; i < write_set_vec_PUSH_PULL.size(); i++) {
              if(write_set_vec_PUSH_PULL[i].SYNC_TYPE == "sync_push"){
                SSAfter << "\n" << "if(_graph.is_vertex_cut()) {";
                SSAfter <<"\n\t" << "_graph.sync_pull<SyncerPull_vertexCut_" << i << ">" <<"(\"" << OperatorStructName << "\");";
                SSAfter << "\n}\n";
                rewriter.InsertText(ST_main, SSAfter.str(), true, true);
              }
              SSAfter.str(string());
              SSAfter.clear();
            }

            // closing helpFunction struct
            SSHelperStructFunctions << "\t}\n};\n\n";
            rewriter.InsertText(ST_main, SSHelperStructFunctions.str(), true, true);
            SSHelperStructFunctions.str(string());
            SSHelperStructFunctions.clear();

            auto recordDecl = Results.Nodes.getNodeAs<clang::CXXRecordDecl>("class");
            std::string className = recordDecl->getNameAsString();
            
            std::string accumulator;
            for (auto decl : recordDecl->decls()) {
              auto var = dyn_cast<VarDecl>(decl);
              if (var && var->isStaticDataMember()) {
                std::string type = var->getType().getAsString();
                if (type.find(__GALOIS_ACCUMULATOR_TYPE__) == 0) {
                  assert(accumulator.empty());
                  accumulator = var->getNameAsString();
                }
              }
            }
            stringstream kernelBefore;
            kernelBefore << "#ifdef __GALOIS_HET_CUDA__\n";
            kernelBefore << "\tif (personality == GPU_CUDA) {\n";
            kernelBefore << "\t\tunsigned num_iter = 0;\n";
            kernelBefore << "\t\tauto __sync_functor = Get_info_functor<Graph>(_graph);\n";
            kernelBefore << "\t\ttypedef Galois::DGBag<GNode, Get_info_functor<Graph> > DBag;\n";
            kernelBefore << "\t\tDBag dbag(__sync_functor);\n";
            kernelBefore << "\t\tauto &local_wl = DBag::get();\n";
            kernelBefore << "\t\tcuda_wl.num_in_items = _graph.getNumOwned();\n";
            kernelBefore << "\t\tfor (int __i = 0; __i < cuda_wl.num_in_items; ++__i) cuda_wl.in_items[__i] = __i;\n";
            stringstream cudaKernelCall;
            if (!accumulator.empty()) {
              cudaKernelCall << "\t\tint __retval = 0;\n";
            }
            cudaKernelCall << "\t\tif (cuda_wl.num_in_items > 0)\n";
            cudaKernelCall << "\t\t\t" << className << "_cuda(";
            if (!accumulator.empty()) {
              cudaKernelCall << "__retval, ";
            }
            for (auto field : recordDecl->fields()) {
              std::string name = field->getNameAsString();
              if (name.find(__GALOIS_PREPROCESS_GLOBAL_VARIABLE_PREFIX__) == 0) {
                cudaKernelCall << name.substr(std::strlen(__GALOIS_PREPROCESS_GLOBAL_VARIABLE_PREFIX__)) << ", ";
              }
            }
            cudaKernelCall << "cuda_ctx);\n";
            if (!accumulator.empty()) {
              cudaKernelCall << "\t\t" << accumulator << " += __retval;\n";
            }
            cudaKernelCall << "\t\t__sync_functor.sync_graph();\n";
            cudaKernelCall << "\t\tdbag.set_local(cuda_wl.out_items, cuda_wl.num_out_items);\n";
            cudaKernelCall << "\t\t#ifdef __GALOIS_DEBUG_WORKLIST__\n";
            cudaKernelCall << "\t\tstd::cout << \"[\" << Galois::Runtime::getSystemNetworkInterface().ID << \"] worklist size : \" << cuda_wl.num_out_items << \" duplication factor : \" << (double)cuda_wl.num_out_items/_graph.size() << \"\\n\";\n";
            cudaKernelCall << "\t\t#endif\n";
            cudaKernelCall << "\t\tdbag.sync();\n";
            cudaKernelCall << "\t\tcuda_wl.num_out_items = 0;\n";
            kernelBefore << cudaKernelCall.str();
            kernelBefore << "\t\twhile (!dbag.canTerminate()) {\n";
            kernelBefore << "\t\t++num_iter;\n";
            kernelBefore << "\t\tcuda_wl.num_in_items = local_wl.size();\n";
            kernelBefore << "\t\tif (cuda_wl.num_in_items > cuda_wl.max_size) {\n";
            kernelBefore << "\t\t\tstd::cout << \"[\" << Galois::Runtime::getSystemNetworkInterface().ID << \"] ERROR - worklist size insufficient; size : \" << cuda_wl.max_size << \" , expected : \" << cuda_wl.num_in_items << \"\\n\";\n";
            kernelBefore << "\t\t\texit(1);\n";
            kernelBefore << "\t\t}\n";
            kernelBefore << "\t\t//std::cout << \"[\" << Galois::Runtime::getSystemNetworkInterface().ID << \"] Iter : \" << num_iter ";
            kernelBefore << "<< \" Total items to work on : \" << cuda_wl.num_in_items << \"\\n\";\n";
            kernelBefore << "\t\tstd::copy(local_wl.begin(), local_wl.end(), cuda_wl.in_items);\n";
            kernelBefore << cudaKernelCall.str();
            kernelBefore << "\t\t}\n";
            kernelBefore << "\t} else if (personality == CPU)\n";
            kernelBefore << "#endif\n";
            SourceLocation ST_before = callFS->getSourceRange().getBegin();
            rewriter.InsertText(ST_before, kernelBefore.str(), true, true);


            //insert helperFunc in for_each call
            SourceLocation ST_forEach_end = callFS->getSourceRange().getEnd().getLocWithOffset(0);
            //SSHelperStructFunctions << ", Get_info_functor<Graph>(_graph), Galois::wl<dChunk>()";

            //Assumption:: User will give worklist Galois::wl in for_each.
            SSHelperStructFunctions << ", Get_info_functor<Graph>(_graph)";
            rewriter.InsertText(ST_forEach_end, SSHelperStructFunctions.str(), true, true);
          }
        }
  };
  class FunctionCallHandler : public MatchFinder::MatchCallback {
    private:
      Rewriter &rewriter;

    public:
      FunctionCallHandler(Rewriter &rewriter) :  rewriter(rewriter){}
      virtual void run(const MatchFinder::MatchResult &Results) {
        const CallExpr* callFS = Results.Nodes.getNodeAs<clang::CallExpr>("galoisLoop");
        llvm::errs() << "It is coming here\n" << callFS->getNumArgs() << "\n";
        auto callerStruct = Results.Nodes.getNodeAs<clang::CXXRecordDecl>("class");

        if(callFS){

          std::string OperatorStructName = callerStruct->getNameAsString();
          llvm::outs() << " NAME OF THE STRUCT : " << OperatorStructName << "\n";

          SourceLocation ST_main = callFS->getSourceRange().getBegin();

          llvm::errs() << "Galois::do_All loop found\n";

          //Get the arguments:
          clang::LangOptions LangOpts;
          LangOpts.CPlusPlus = true;
          clang::PrintingPolicy Policy(LangOpts);

          // Vector to store read and write set.
          vector<pair<string, string>>read_set_vec;
          vector<pair<string,string>>write_set_vec;
          vector<string>write_setGNode_vec;
          string GraphNode;

          vector<Write_set> write_set_vec_PUSH;
          vector<Write_set> write_set_vec_PULL;

          /*
             string str_first;
             llvm::raw_string_ostream s_first(str_first);
             callFS->getArg(0)->printPretty(s_first, 0, Policy);
          //llvm::errs() << " //////// > " << s_first.str() << "\n";
          callFS->getArg(0)->dump();
          */

          for(int i = 0, j = callFS->getNumArgs(); i < j; ++i){
            string str_arg;
            llvm::raw_string_ostream s(str_arg);
            callFS->getArg(i)->printPretty(s, 0, Policy);

            //callFS->getArg(i)->IgnoreParenImpCasts()->dump();
            const CallExpr* callExpr = dyn_cast<CallExpr>(callFS->getArg(i)->IgnoreParenImpCasts());
            //const CallExpr* callExpr1 = callFS->getArg(i);;
            //callExpr->dump();
            if (callExpr) {
              const FunctionDecl* func = callExpr->getDirectCallee();
              //func->dump();
              if(func) {
                if (func->getNameInfo().getName().getAsString() == "read_set"){
                  llvm::errs() << "Inside arg read_set: " << i <<  s.str() << "\n\n";
                }
                if (func->getNameInfo().getName().getAsString() == "write_set"){
                  llvm::errs() << "Inside arg write_set: " << i <<  s.str() << "\n\n";

                  // Take out first argument to see if it is push or pull
                  string str_sub_arg;
                  llvm::raw_string_ostream s_sub(str_sub_arg);
                  callExpr->getArg(0)->printPretty(s_sub, 0, Policy);

                  string str_sync_type = s_sub.str();
                  if(str_sync_type.front() == '"'){
                    str_sync_type.erase(0,1);
                    str_sync_type.erase(str_sync_type.size()-1);
                  }

                  //SYNC_PUSH
                  //if(callExpr->getNumArgs() == 8) {
                  if(str_sync_type == "sync_push") {
                    vector<string> Temp_vec_PUSH;
                    for(unsigned i = 0; i < callExpr->getNumArgs(); ++i){
                      string str_sub_arg;
                      llvm::raw_string_ostream s_sub(str_sub_arg);
                      callExpr->getArg(i)->printPretty(s_sub, 0, Policy);

                      string temp_str = s_sub.str();
                      if(temp_str.front() == '"'){
                        temp_str.erase(0,1);
                        temp_str.erase(temp_str.size()-1);
                      }
                      if(temp_str.front() == '&'){
                        temp_str.erase(0,1);
                      }

                      llvm::errs() << " ------- > " << temp_str << "\n";
                      Temp_vec_PUSH.push_back(temp_str);

                    }
                    Write_set WR_entry;
                    WR_entry.GRAPH_NAME = Temp_vec_PUSH[1];
                    WR_entry.NODE_TYPE = Temp_vec_PUSH[2];
                    WR_entry.FIELD_TYPE = Temp_vec_PUSH[3];
                    WR_entry.FIELD_NAME = Temp_vec_PUSH[4];
                    WR_entry.VAL_TYPE = Temp_vec_PUSH[5];
                    WR_entry.REDUCE_OP_EXPR = Temp_vec_PUSH[6];
                    WR_entry.RESET_VAL_EXPR = Temp_vec_PUSH[7];
                    WR_entry.SYNC_TYPE = "sync_push";

                    write_set_vec_PUSH.push_back(WR_entry);
                  }

                  //SYNC_PULL
                  else if(str_sync_type == "sync_pull") {
                    vector<string> Temp_vec_PULL;
                    for(unsigned i = 0; i < callExpr->getNumArgs(); ++i){
                      string str_sub_arg;
                      llvm::raw_string_ostream s_sub(str_sub_arg);
                      callExpr->getArg(i)->printPretty(s_sub, 0, Policy);

                      string temp_str = s_sub.str();
                      if(temp_str.front() == '"'){
                        temp_str.erase(0,1);
                        temp_str.erase(temp_str.size()-1);
                      }
                      if(temp_str.front() == '&'){
                        temp_str.erase(0,1);
                      }

                      Temp_vec_PULL.push_back(temp_str);

                    }
                    Write_set WR_entry;
                    WR_entry.GRAPH_NAME = Temp_vec_PULL[1];
                    WR_entry.NODE_TYPE = Temp_vec_PULL[2];
                    WR_entry.FIELD_TYPE = Temp_vec_PULL[3];
                    WR_entry.FIELD_NAME = Temp_vec_PULL[4];
                    WR_entry.VAL_TYPE = Temp_vec_PULL[5];
                    WR_entry.SYNC_TYPE = "sync_pull";
                    WR_entry.REDUCE_OP_EXPR = Temp_vec_PULL[6];
                    WR_entry.RESET_VAL_EXPR = Temp_vec_PULL[7];

                    write_set_vec_PULL.push_back(WR_entry);
                  }
                 }
                }
              }
            }

            /************************************
             * Find unique one from push and pull
             * sets to generate for vertex cut
             * *********************************/

            vector<Write_set> write_set_vec_PUSH_PULL;
            for(auto i : write_set_vec_PUSH){
              bool found = false;
              for(auto j : write_set_vec_PULL){
                if(i.GRAPH_NAME == j.GRAPH_NAME && i.NODE_TYPE == j.NODE_TYPE && i.FIELD_TYPE == j.FIELD_TYPE && i.FIELD_NAME == j.FIELD_NAME){
                  found = true;
                }
              }
              if(!found)
                write_set_vec_PUSH_PULL.push_back(i);
            }
            for(auto i : write_set_vec_PULL){
              bool found = false;
              for(auto j : write_set_vec_PUSH){
                if(i.GRAPH_NAME == j.GRAPH_NAME && i.NODE_TYPE == j.NODE_TYPE && i.FIELD_TYPE == j.FIELD_TYPE && i.FIELD_NAME == j.FIELD_NAME){
                  found = true;
                }
              }
              if(!found)
                write_set_vec_PUSH_PULL.push_back(i);
            }

            stringstream SSSyncer;
            unsigned counter = 0;
            for(auto i : write_set_vec_PUSH) {
              SSSyncer << getSyncer(counter, i);
              rewriter.InsertText(ST_main, SSSyncer.str(), true, true);
              SSSyncer.str(string());
              SSSyncer.clear();
              ++counter;
            }

            // Addding structure for pull sync
            stringstream SSSyncer_pull;
            counter = 0;
            for(auto i : write_set_vec_PULL) {
              SSSyncer_pull << getSyncerPull(counter, i);
              rewriter.InsertText(ST_main, SSSyncer_pull.str(), true, true);
              SSSyncer_pull.str(string());
              SSSyncer_pull.clear();
              ++counter;
            }

            // Addding additional structs for vertex cut
            stringstream SSSyncer_vertexCut;
            counter = 0;
            for(auto i : write_set_vec_PUSH_PULL) {
              if(i.SYNC_TYPE == "sync_push"){
                SSSyncer_vertexCut << getSyncerPull(counter, i, "vertexCut_");
                rewriter.InsertText(ST_main, SSSyncer_vertexCut.str(), true, true);
                SSSyncer_vertexCut.str(string());
                SSSyncer_vertexCut.clear();
                ++counter;
              }
              else if(i.SYNC_TYPE == "sync_pull"){
                SSSyncer_vertexCut << getSyncer(counter, i, "vertexCut_");
                rewriter.InsertText(ST_main, SSSyncer_vertexCut.str(), true, true);
                SSSyncer_vertexCut.str(string());
                SSSyncer_vertexCut.clear();
                ++counter;
              }
            }

            auto recordDecl = Results.Nodes.getNodeAs<clang::CXXRecordDecl>("class");
            std::string className = recordDecl->getNameAsString();

            stringstream kernelBefore;
            kernelBefore << "#ifdef __GALOIS_HET_CUDA__\n";
            kernelBefore << "\tif (personality == GPU_CUDA) {\n";
            std::string accumulator;
            for (auto decl : recordDecl->decls()) {
              auto var = dyn_cast<VarDecl>(decl);
              if (var && var->isStaticDataMember()) {
                std::string type = var->getType().getAsString();
                if (type.find(__GALOIS_ACCUMULATOR_TYPE__) == 0) {
                  assert(accumulator.empty());
                  accumulator = var->getNameAsString();
                }
              }
            }
            if (!accumulator.empty()) {
              kernelBefore << "\t\tint __retval = 0;\n";
            }
            kernelBefore << "\t\t" << className << "_cuda(";
            if (!accumulator.empty()) {
              kernelBefore << "__retval, ";
            }
            for (auto field : recordDecl->fields()) {
              std::string name = field->getNameAsString();
              if (name.find(__GALOIS_PREPROCESS_GLOBAL_VARIABLE_PREFIX__) == 0) {
                kernelBefore << name.substr(std::strlen(__GALOIS_PREPROCESS_GLOBAL_VARIABLE_PREFIX__)) << ", ";
              }
            }
            kernelBefore << "cuda_ctx);\n";
            if (!accumulator.empty()) {
              kernelBefore << "\t\t" << accumulator << " += __retval;\n";
            }
            kernelBefore << "\t} else if (personality == CPU)\n";
            kernelBefore << "#endif\n";
            rewriter.InsertText(ST_main, kernelBefore.str(), true, true);

            SourceLocation ST = callFS->getSourceRange().getEnd().getLocWithOffset(2);

            // Adding Syn calls for write set
            stringstream SSAfter;
            for (unsigned i = 0; i < write_set_vec_PUSH.size(); i++) {
              SSAfter.str(string());
              SSAfter.clear();
              //SSAfter <<"\n" <<write_set_vec_PUSH[i].GRAPH_NAME<< ".sync_push<Syncer_" << i << ">" <<"();\n";
              SSAfter <<"\n" << "_graph.sync_push<Syncer_" << i << ">" <<"(\"" << OperatorStructName << "\");\n";
              rewriter.InsertText(ST, SSAfter.str(), true, true);
            }


            //For sync Pull and push for vertex cut
            for (unsigned i = 0; i < write_set_vec_PUSH_PULL.size(); i++) {
              SSAfter.str(string());
              SSAfter.clear();
              if(write_set_vec_PUSH_PULL[i].SYNC_TYPE == "sync_pull"){
                SSAfter << "\n" << "if(_graph.is_vertex_cut()) {";
                SSAfter <<"\n\t" << "_graph.sync_push<Syncer_vertexCut_" << i << ">" <<"(\"" << OperatorStructName << "\");";
                SSAfter << "\n}\n";
                rewriter.InsertText(ST, SSAfter.str(), true, true);
              }
            }

            //For sync Pull
            for (unsigned i = 0; i < write_set_vec_PULL.size(); i++) {
              SSAfter.str(string());
              SSAfter.clear();
              SSAfter <<"\n" << "_graph.sync_pull<SyncerPull_" << i << ">" <<"(\"" << OperatorStructName << "\");\n";
              rewriter.InsertText(ST, SSAfter.str(), true, true);
            }

            //For sync Pull and push for vertex cut
            for (unsigned i = 0; i < write_set_vec_PUSH_PULL.size(); i++) {
              SSAfter.str(string());
              SSAfter.clear();
              if(write_set_vec_PUSH_PULL[i].SYNC_TYPE == "sync_push"){
                SSAfter << "\n" << "if(_graph.is_vertex_cut()) {";
                SSAfter <<"\n\t" << "_graph.sync_pull<SyncerPull_vertexCut_" << i << ">" <<"(\"" << OperatorStructName << "\");";
                SSAfter << "\n}\n";
                rewriter.InsertText(ST, SSAfter.str(), true, true);
              }
            }
          }
        }
      };



  class GaloisFunctionsConsumer : public ASTConsumer {
    private:
    CompilerInstance &Instance;
    std::set<std::string> ParsedTemplates;
    GaloisFunctionsVisitor* Visitor;
    MatchFinder Matchers;
    ForStmtHandler forStmtHandler;
    //NameSpaceHandler namespaceHandler;
    FunctionCallHandler functionCallHandler;
    FunctionForEachHandler forEachHandler;
  public:
    GaloisFunctionsConsumer(CompilerInstance &Instance, std::set<std::string> ParsedTemplates, Rewriter &R): Instance(Instance), ParsedTemplates(ParsedTemplates), Visitor(new GaloisFunctionsVisitor(&Instance)), functionCallHandler(R), forEachHandler(R) {
      Matchers.addMatcher(callExpr(isExpansionInMainFile(), callee(functionDecl(hasName("Galois::do_all"))), hasAncestor(recordDecl().bind("class"))).bind("galoisLoop"), &functionCallHandler);

      /** for Galois::for_each. Needs different treatment. **/
      Matchers.addMatcher(callExpr(isExpansionInMainFile(), callee(functionDecl(hasName("Galois::for_each"))), hasDescendant(declRefExpr(to(functionDecl(hasName("workList_version"))))), hasAncestor(recordDecl().bind("class"))).bind("galoisLoop_forEach"), &forEachHandler);

      Matchers.addMatcher(callExpr(isExpansionInMainFile(), callee(functionDecl(hasName("Galois::for_each"))), unless(hasDescendant(declRefExpr(to(functionDecl(hasName("workList_version")))))), hasAncestor(recordDecl().bind("class"))).bind("galoisLoop"), &functionCallHandler);


    }

    virtual void HandleTranslationUnit(ASTContext &Context){
      Matchers.matchAST(Context);
    }
  };

  class GaloisFunctionsAction : public PluginASTAction {
  private:
    std::set<std::string> ParsedTemplates;
    Rewriter TheRewriter;
  protected:

    void EndSourceFileAction() override {
      TheRewriter.getEditBuffer(TheRewriter.getSourceMgr().getMainFileID()).write(llvm::outs());
      if (!TheRewriter.overwriteChangedFiles())
      {
        llvm::errs() << "Successfully saved changes\n";
      }

    }
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
      TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
      return llvm::make_unique<GaloisFunctionsConsumer>(CI, ParsedTemplates, TheRewriter);

    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
      return true;
    }

  };


}

static FrontendPluginRegistry::Add<GaloisFunctionsAction> X("galois-fns", "find galois function names");
