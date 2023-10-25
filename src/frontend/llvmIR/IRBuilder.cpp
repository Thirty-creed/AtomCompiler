#include "llvmIR/IRBuilder.h"

#include "AST/CompUnit.h"
#include "AST/Decl.h"
#include "AST/Expression.h"
#include "AST/FunctionDef.h"
#include "AST/Scope.h"
#include "AST/Statement.h"
#include "AST/Variable.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Host.h"

namespace ATC {

IRBuilder::IRBuilder() {
    llvm::LLVMContext *ctx = new llvm::LLVMContext();
    _theIRBuilder = new llvm::IRBuilder<>(*ctx);
    _module = new llvm::Module("module", *ctx);
    _voidTy = llvm::Type::getVoidTy(*ctx);
    _int1Ty = llvm::Type::getInt1Ty(*ctx);
    _int32Ty = llvm::Type::getInt32Ty(*ctx);
    _floatTy = llvm::Type::getFloatTy(*ctx);
    _int32PtrTy = llvm::Type::getInt32PtrTy(*ctx);
    _floatPtrTy = llvm::Type::getFloatPtrTy(*ctx);
    _int1Zero == llvm::ConstantInt::get(_int1Ty, 0);
    _int32Zero = llvm::ConstantInt::get(_int32Ty, 0, true);
    _floatZero = llvm::ConstantFP::get(_floatTy, 0);
    _int1One == llvm::ConstantInt::get(_int1Ty, 1);
    _int32One = llvm::ConstantInt::get(_int32Ty, 1, true);
    _floatOne = llvm::ConstantFP::get(_floatTy, 1);

    _module->setTargetTriple(llvm::sys::getProcessTriple());
}

IRBuilder::~IRBuilder() { _module->print(llvm::outs(), nullptr); }

void IRBuilder::visit(FunctionDef *node) {
    std::vector<llvm::Type *> params;
    for (auto param : node->getParams()) {
        DataType *dataType = param->getDataType();
        params.push_back(convertToLLVMType(dataType));
    }
    llvm::FunctionType *funcTy = llvm::FunctionType::get(convertToLLVMType(node->getRetType()), params, false);
    llvm::Function *func = llvm::Function::Create(funcTy, llvm::GlobalValue::ExternalLinkage, node->getName(), _module);
    node->setFunction(func);
    _currentFunction = func;

    llvm::BasicBlock *allocBB = llvm::BasicBlock::Create(_module->getContext(), "init");
    llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(_module->getContext(), "entry");
    allocBB->insertInto(_currentFunction);
    entryBB->insertInto(_currentFunction);

    _theIRBuilder->SetInsertPoint(allocBB);
    allocForScopeVars(node->getScope());
    int i = 0;
    for (auto param : node->getParams()) {
        // the decl of formal param is the only one
        Variable *var = param->getVariables()[0];
        auto arg = func->getArg(i++);
        _theIRBuilder->CreateStore(arg, var->getAddr());
    }
    _theIRBuilder->CreateBr(entryBB);

    _theIRBuilder->SetInsertPoint(entryBB);
    node->getBlock()->accept(this);

    // if the function didn't execute return before, than return the default value
    if (node->getRetType() == INT) {
        _theIRBuilder->CreateRet(_int32Zero);
    } else {
        _theIRBuilder->CreateRet(_floatZero);
    }
}

void IRBuilder::visit(Variable *node) {
    if (node->isGlobal()) {
        // create global variable
        llvm::Type *type = node->getDataType()->getBaseType() == INT ? _int32Ty : _floatTy;
        _module->getOrInsertGlobal(node->getName(), type);
        auto globalVar = _module->getNamedGlobal(node->getName());

        if (auto initValue = node->getInitValue()) {
            assert(initValue->isConst());
            initValue->accept(this);
            globalVar->setInitializer((llvm::Constant *)_value);
        }
        node->setAddr(globalVar);
    } else {
        if (auto initValue = node->getInitValue()) {
            initValue->accept(this);
            _value = convertToDestTy(_value, node->getAddr()->getType()->getPointerElementType());
            _theIRBuilder->CreateStore(_value, node->getAddr());
        }
    }
}

void IRBuilder::visit(ConstVal *node) {
    if (node->getBaseType() == INT) {
        _value = llvm::ConstantInt::get(_int32Ty, node->getIntValue());
    } else {
        _value = llvm::ConstantFP::get(_floatTy, node->getFloatValue());
    }
}

void IRBuilder::visit(VarRef *node) {
    if (node->getVariable()->getDataType()->getBaseType() == INT) {
        _value = _theIRBuilder->CreateLoad(_int32Ty, node->getVariable()->getAddr());
    } else {
        _value = _theIRBuilder->CreateLoad(_floatTy, node->getVariable()->getAddr());
    }
}

void IRBuilder::visit(ArrayExpression *node) {
    if (node->isConst()) {
        // std::vector<llvm::Constant *> array;
        // array.push_back(_int32One);
        // array.push_back(_int32One);
        // llvm::ArrayRef<llvm::Constant *> arrayRef = array;
        // auto arrayType = llvm::ArrayType::get(_int32Ty, 0);
        // auto arrayType1 = llvm::ArrayType::get(arrayType, 0);
        // auto tmp1 = llvm::ConstantArray::get(arrayType, array);
        // std::vector<llvm::Constant *> array1;
        // array1.push_back(tmp1);
        // array1.push_back(tmp1);
        // _value = llvm::ConstantArray::get(arrayType, array1);
    }
}

void IRBuilder::visit(UnaryExpression *node) {
    node->getOperand()->accept(this);
    if (node->getOperator() == MINUS) {
        if (_value->getType() == _floatTy) {
            _value = _theIRBuilder->CreateFSub(_floatZero, _value);
        } else if (_value->getType() == _int32Ty) {
            _value = _theIRBuilder->CreateSub(_int32Zero, _value);
        }
    } else if (node->getOperator() == NOT) {
        _value = _theIRBuilder->CreateNot(convertToDestTy(_value, _int1Ty));
    }
}

void IRBuilder::visit(BinaryExpression *node) {
    if (node->isShortCircuit()) {
        llvm::BasicBlock *rhsCondBB = llvm::BasicBlock::Create(_module->getContext(), "rhsCondBB");
        rhsCondBB->insertInto(_currentFunction);

        if (node->getOperator() == AND) {
            auto tmpTrueBB = _trueBB;
            _trueBB = rhsCondBB;
            node->getLeft()->accept(this);
            _theIRBuilder->CreateCondBr(convertToDestTy(_value, _int1Ty), rhsCondBB, _falseBB);
            _trueBB = tmpTrueBB;
        } else {
            auto tmpFalseBB = _falseBB;
            _falseBB = rhsCondBB;
            node->getLeft()->accept(this);
            _theIRBuilder->CreateCondBr(convertToDestTy(_value, _int1Ty), _trueBB, rhsCondBB);
            _falseBB = tmpFalseBB;
        }

        _theIRBuilder->SetInsertPoint(rhsCondBB);
        node->getRight()->accept(this);
        return;
    }

    node->getLeft()->accept(this);
    auto left = _value;
    node->getRight()->accept(this);
    auto right = _value;

    if (node->getOperator() == AND) {
        _value = _theIRBuilder->CreateAnd(convertToDestTy(left, _int1Ty), convertToDestTy(right, _int1Ty));
        return;
    } else if (node->getOperator() == OR) {
        _value = _theIRBuilder->CreateOr(convertToDestTy(left, _int1Ty), convertToDestTy(right, _int1Ty));
        return;
    }

    // make the left expr and right expr has the same type
    if (left->getType() == _floatTy || right->getType() == _floatTy) {
        left = convertToDestTy(left, _floatTy);
        right = convertToDestTy(right, _floatTy);
        switch (node->getOperator()) {
            case PLUS:
                _value = _theIRBuilder->CreateFAdd(left, right);
                break;
            case MINUS:
                _value = _theIRBuilder->CreateFSub(left, right);
                break;
            case MUL:
                _value = _theIRBuilder->CreateFMul(left, right);
                break;
            case DIV:
                _value = _theIRBuilder->CreateFDiv(left, right);
                break;
            case MOD:
                assert(false && "should not reach here");
                break;
            case LT:
                _value = _theIRBuilder->CreateFCmpOLT(left, right);
                break;
            case GT:
                _value = _theIRBuilder->CreateFCmpOGT(left, right);
                break;
            case LE:
                _value = _theIRBuilder->CreateFCmpOLE(left, right);
                break;
            case GE:
                _value = _theIRBuilder->CreateFCmpOGE(left, right);
                break;
            case EQ:
                _value = _theIRBuilder->CreateFCmpOEQ(left, right);
                break;
            case NE:
                _value = _theIRBuilder->CreateFCmpONE(left, right);
                break;
            default:
                assert(false && "should not reach here");
                break;
        }
    } else {
        left = convertToDestTy(left, _int32Ty);
        right = convertToDestTy(right, _int32Ty);
        switch (node->getOperator()) {
            case PLUS:
                _value = _theIRBuilder->CreateAdd(left, right);
                break;
            case MINUS:
                _value = _theIRBuilder->CreateSub(left, right);
                break;
            case MUL:
                _value = _theIRBuilder->CreateMul(left, right);
                break;
            case DIV:
                _value = _theIRBuilder->CreateSDiv(left, right);
                break;
            case MOD:
                _value = _theIRBuilder->CreateSRem(left, right);
                break;
            case LT:
                _value = _theIRBuilder->CreateICmpSLT(left, right);
                break;
            case GT:
                _value = _theIRBuilder->CreateICmpSGT(left, right);
                break;
            case LE:
                _value = _theIRBuilder->CreateICmpSLE(left, right);
                break;
            case GE:
                _value = _theIRBuilder->CreateICmpSGE(left, right);
                break;
            case EQ:
                _value = _theIRBuilder->CreateICmpEQ(left, right);
                break;
            case NE:
                _value = _theIRBuilder->CreateICmpNE(left, right);
                break;
            default:
                assert(false && "should not reach here");
                break;
        }
    }
}

void IRBuilder::visit(FunctionCall *node) {
    std::vector<llvm::Value *> params;
    for (auto rParam : node->getParams()) {
        rParam->accept(this);
        params.push_back(_value);
    }
    _value = _theIRBuilder->CreateCall(node->getFunctionDef()->getFunction(), params);
}

void IRBuilder::visit(AssignStatement *node) {
    node->getValue()->accept(this);
    llvm::Value *addr = node->getVar()->getVariable()->getAddr();
    assert(addr->getType()->isPointerTy());
    llvm::Type *destTy = addr->getType()->getPointerElementType();
    _value = convertToDestTy(_value, destTy);
    _theIRBuilder->CreateStore(_value, addr);
}

void IRBuilder::visit(IfStatement *node) {
    llvm::BasicBlock *ifBB = llvm::BasicBlock::Create(_module->getContext(), "ifBB");
    llvm::BasicBlock *afterIfBB = llvm::BasicBlock::Create(_module->getContext(), "afterIfBB");
    ifBB->insertInto(_currentFunction);
    afterIfBB->insertInto(_currentFunction);

    _trueBB = ifBB;
    if (node->getElseStmt()) {
        llvm::BasicBlock *elseBB = llvm::BasicBlock::Create(_module->getContext(), "elseBB");
        elseBB->insertInto(_currentFunction);
        _falseBB = elseBB;
        node->getCond()->accept(this);
        _theIRBuilder->CreateCondBr(convertToDestTy(_value, _int1Ty), _trueBB, _falseBB);

        _theIRBuilder->SetInsertPoint(elseBB);
        node->getElseStmt()->accept(this);
        _theIRBuilder->CreateBr(afterIfBB);
    } else {
        _falseBB = afterIfBB;
        node->getCond()->accept(this);
        _theIRBuilder->CreateCondBr(convertToDestTy(_value, _int1Ty), _trueBB, _falseBB);
    }

    _theIRBuilder->SetInsertPoint(ifBB);
    node->getStmt()->accept(this);
    _theIRBuilder->CreateBr(afterIfBB);

    _theIRBuilder->SetInsertPoint(afterIfBB);
}

void IRBuilder::visit(WhileStatement *node) {
    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(_module->getContext(), "condBB");
    llvm::BasicBlock *whileBB = llvm::BasicBlock::Create(_module->getContext(), "whileBB");
    llvm::BasicBlock *afterWhileBB = llvm::BasicBlock::Create(_module->getContext(), "afterWhileBB");
    condBB->insertInto(_currentFunction);
    whileBB->insertInto(_currentFunction);
    afterWhileBB->insertInto(_currentFunction);

    _theIRBuilder->CreateBr(condBB);
    _theIRBuilder->SetInsertPoint(condBB);
    _trueBB = whileBB;
    _falseBB = afterWhileBB;
    node->getCond()->accept(this);
    _theIRBuilder->CreateCondBr(convertToDestTy(_value, _int1Ty), _trueBB, _falseBB);

    _theIRBuilder->SetInsertPoint(whileBB);
    node->getStmt()->accept(this);
    _theIRBuilder->CreateBr(condBB);

    _theIRBuilder->SetInsertPoint(afterWhileBB);
}

void IRBuilder::visit(ReturnStatement *node) {
    if (node->getExpr()) {
        node->getExpr()->accept(this);
        _theIRBuilder->CreateRet(_value);
    } else {
        _theIRBuilder->CreateRetVoid();
    }
}

llvm::Type *IRBuilder::convertToLLVMType(int type) {
    switch (type) {
        case BOOL:
            return _int1Ty;
        case INT:
            return _int32Ty;
        case FLOAT:
            return _floatTy;
        case VOID:
            return _voidTy;
        default:
            assert(false && "should not reach here");
    }
}

llvm::Type *IRBuilder::convertToLLVMType(DataType *dataType) {
    if (dataType->isPointer()) {
        // TODO:
        return nullptr;
    } else {
        return convertToLLVMType(dataType->getBaseType());
    }
}

void IRBuilder::allocForScopeVars(Scope *currentScope) {
    for (auto [name, var] : currentScope->getVarMap()) {
        auto addr = _theIRBuilder->CreateAlloca(convertToLLVMType(var->getDataType()), nullptr, name);
        var->setAddr(addr);
    }
    for (auto child : currentScope->getChildren()) {
        allocForScopeVars(child);
    }
}

llvm::Value *IRBuilder::convertToDestTy(llvm::Value *value, llvm::Type *destTy) {
    if (destTy == _floatTy) {
        if (value->getType() != _floatTy) {
            return _theIRBuilder->CreateSIToFP(value, _floatTy);
        }
    } else if (destTy == _int32Ty) {
        if (value->getType() == _floatTy) {
            return _theIRBuilder->CreateFPToSI(value, _int32Ty);
        } else if (value->getType() == _int1Ty) {
            return _theIRBuilder->CreateZExt(value, _int32Ty);
        }
    } else {
        if (value->getType() == _floatTy) {
            return _theIRBuilder->CreateFCmpONE(value, _floatZero);
        } else if (value->getType() == _int32Ty) {
            return _theIRBuilder->CreateICmpNE(value, _int32Zero);
        }
    }
    return value;
}

}  // namespace ATC