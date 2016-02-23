/*
 * Analysis.h
 *
 *  Created on: Dec 8, 2015
 *      Author: rashid
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

#include "GaloisAST.h"

#ifndef SRC_PLUGINS_OPENCLCODEGEN_ANALYSIS_H_
#define SRC_PLUGINS_OPENCLCODEGEN_ANALYSIS_H_

using namespace clang;
using namespace clang::ast_matchers;
using namespace llvm;
using namespace std;

namespace {
/////////////////////////////////////////////////////////////////////////
/*********************************************************************************************************
 *
 *********************************************************************************************************/

class GraphTypeParser : public RecursiveASTVisitor<GraphTypeParser>{
public:
   ASTContext * astContext;
   Rewriter & rewriter;
   std::vector<Type * > typeDecls;
   Galois::GAST::GaloisApp * app_data;

public :
      GraphTypeParser (ASTContext * _a , Rewriter & R, Galois::GAST::GaloisApp * ad): astContext(_a), rewriter(R),app_data(ad){

      }
      virtual ~GraphTypeParser(){

      }
      bool _VisitDeclRefExpr (DeclRefExpr * d){
         Type * t = const_cast<Type*>(d->getType().getTypePtr()->getCanonicalTypeUnqualified().getTypePtr());
         if (app_data->find_type(t)){
            llvm::outs() << "< >Found :: " << d->getType().getAsString() << "\n";
         }
         return true;
      }
      bool VisitVarDecl(VarDecl *d){
//         Type * sType = const_cast<Type*>(d->getType().getTypePtr());
         Type * t = const_cast<Type*>(d->getType().getTypePtr()->getCanonicalTypeUnqualified().getTypePtr());
         if(t->isPointerType() || t->isReferenceType())
            t= const_cast<Type*>(t->getPointeeType().getTypePtr());
         std::string s = QualType::getAsString(t, Qualifiers());
         if (app_data->find_type(t)){
            llvm::outs() << "< >Found :: " << s  << ",  ";
            d->dump(llvm::outs());
            llvm::outs()<< "\n";
         }
         else{
//            llvm::outs() << "<!>Found :: " << s << "\n";
         }
         return true;

      }

};
/*
 * The handler class for managing graph declarations. Ideally, we would like to
 * also store all the type-declarations that this class exposes. This would be used
 * to replace the iterator types and other associated types in the opencl implementation.
 * */
class GraphDeclHandler: public MatchFinder::MatchCallback {
public:
   Rewriter &rewriter;
   std::vector<CXXRecordDecl * > graphDecls;
   std::map<CXXRecordDecl * , std::vector<Type *>*> typeDecls;
public:
   GraphDeclHandler(Rewriter &rewriter) :
         rewriter(rewriter) {
   }
   virtual void run(const MatchFinder::MatchResult &Results) {
      CXXRecordDecl* decl = const_cast<CXXRecordDecl*>(Results.Nodes.getNodeAs<clang::CXXRecordDecl>("graphClass"));
      const Type * tyname = decl->getCanonicalDecl()->getTypeForDecl();
      graphDecls.push_back(decl);
      llvm::outs()<<"GraphClass Candidate definition :: " << decl->getNameAsString() <<  ", " << tyname->getTypeClassName() << "\n";
      GraphTypeParser gtp(&decl->getASTContext(), rewriter, nullptr);
      gtp.TraverseDecl(decl);
      typeDecls[decl]= new std::vector<Type*>();
      for(auto t : gtp.typeDecls){
         typeDecls[decl]->push_back(t);
      }
   }

};
class DoAllHandler: public MatchFinder::MatchCallback {
public:
   Rewriter &rewriter;
   Galois::GAST::GaloisApp & app_data;
public:
   DoAllHandler(Rewriter &rewriter, Galois::GAST::GaloisApp & _ad) :
         rewriter(rewriter), app_data(_ad) {
   }
/*
   void add_know_types(const std::set<Type *> & kt){
      app_data.add_known_types(kt);
   }
*/
   virtual void run(const MatchFinder::MatchResult &Results) {
      CallExpr* callFS = const_cast<CallExpr*>(Results.Nodes.getNodeAs<clang::CallExpr>("galoisLoop"));
      VarDecl * decl = const_cast<VarDecl*>(Results.Nodes.getNodeAs<VarDecl>("graphDecl"));
      if(decl)
         rewriter.ReplaceText(decl->getTypeSourceInfo()->getTypeLoc().getSourceRange(), " CLGraph ");

      llvm::outs() << "GaloisLoop found  - #Args :: " << callFS->getNumArgs() << "\n";

      if (callFS) {
         CXXRecordDecl * kernel = const_cast<CXXRecordDecl*>(Results.Nodes.getNodeAs<CXXRecordDecl>("kernelType"));
         assert(kernel!=nullptr && "KernelType cast failed.");
         app_data.add_doAll_call(callFS, kernel);
         llvm::outs() << "Galois::do_All loop found " << callFS->getCalleeDecl()->getCanonicalDecl()->getAsFunction()->getNameAsString() << "\n";
         //Get the arguments:
         clang::LangOptions LangOpts;
         LangOpts.CPlusPlus = true;
         clang::PrintingPolicy Policy(LangOpts);
//         unsigned write_setNum = 0;
//         string GraphNode;
         //Print the important arguments::
         if (false && callFS->getNumArgs() >= 3) {
            llvm::outs() << "Begin iterator :: ";
            callFS->getArg(0)->printPretty(llvm::outs(), nullptr, Policy);
            llvm::outs() << ", Type :: " << QualType::getAsString(callFS->getArg(0)->getType().split()) << "\n";
            llvm::outs() << "End iterator :: ";
            callFS->getArg(1)->printPretty(llvm::outs(), nullptr, Policy);
            llvm::outs() << ", Type :: " << QualType::getAsString(callFS->getArg(1)->getType().split()) << "\n";
            llvm::outs() << "Operator instance :: ";
            callFS->getArg(2)->printPretty(llvm::outs(), nullptr, Policy);
            llvm::outs() << ", Type :: " << QualType::getAsString(callFS->getArg(2)->getType().split()) << "\n";
            llvm::outs()<<"-------->OPERATOR CALLED IN DO_ALL::\n";
            callFS->getArg(2)->getBestDynamicClassType()->dump();
            llvm::outs() << ", Type :: " << callFS->getArg(2)->getBestDynamicClassType()->getName() << "\n";

         }
      }
   }
};
/*********************************************************************************************************
 *
 *********************************************************************************************************/

/*********************************************************************************************************
 *
 *********************************************************************************************************/

}//End anonymous namespace

#endif /* SRC_PLUGINS_OPENCLCODEGEN_ANALYSIS_H_ */
