#include "llvmIR/IRBuilder.h"

#include "../CmdOption.h"
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

namespace LLVMIR {
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
    _int32Zero = llvm::ConstantInt::get(_int32Ty, 0, true);
    _floatZero = llvm::ConstantFP::get(_floatTy, 0);
    _int32One = llvm::ConstantInt::get(_int32Ty, 1, true);
    _floatOne = llvm::ConstantFP::get(_floatTy, 1);

    _module->setTargetTriple(llvm::sys::getProcessTriple());
    _module->setDataLayout(llvm::DataLayout(_module));

    if (Sy) {
        auto funcTy = llvm::FunctionType::get(_int32Ty, {}, false);
        _funcName2funcType["getint"] = funcTy;
        _funcName2funcType["getch"] = funcTy;

        funcTy = llvm::FunctionType::get(_floatTy, {}, false);
        _funcName2funcType["getfloat"] = funcTy;

        funcTy = llvm::FunctionType::get(_int32Ty, {_int32PtrTy}, false);
        _funcName2funcType["getarray"] = funcTy;

        funcTy = llvm::FunctionType::get(_int32Ty, {_floatPtrTy}, false);
        _funcName2funcType["getfarray"] = funcTy;

        funcTy = llvm::FunctionType::get(_voidTy, {_int32Ty, _int32PtrTy}, false);
        _funcName2funcType["putarray"] = funcTy;

        funcTy = llvm::FunctionType::get(_voidTy, {_floatTy}, false);
        _funcName2funcType["putfloat"] = funcTy;

        funcTy = llvm::FunctionType::get(_voidTy, {_int32Ty, _floatPtrTy}, false);
        _funcName2funcType["putfarray"] = funcTy;

        funcTy = llvm::FunctionType::get(_voidTy, {llvm::Type::getInt8PtrTy(_module->getContext())}, true);
        _funcName2funcType["putf"] = funcTy;

        funcTy = llvm::FunctionType::get(_voidTy, {}, false);
        _funcName2funcType["before_main"] = funcTy;
        _funcName2funcType["after_main"] = funcTy;

        funcTy = llvm::FunctionType::get(_voidTy, {_int32Ty}, false);
        _funcName2funcType["putint"] = funcTy;
        _funcName2funcType["putch"] = funcTy;
        _funcName2funcType["_sysy_starttime"] = funcTy;
        _funcName2funcType["_sysy_stoptime"] = funcTy;
    }
}

IRBuilder::~IRBuilder() {
    //_module->print(llvm::outs(), nullptr);
}

void IRBuilder::visit(FunctionDef *node) {
    std::vector<llvm::Type *> params;
    for (auto param : node->getParams()) {
        // every decl of param expression has only one variable
        DataType *dataType = param->getVariables()[0]->getDataType();
        params.push_back(convertToLLVMType(dataType));
    }
    llvm::FunctionType *funcTy = llvm::FunctionType::get(convertToLLVMType(node->getRetType()), params, false);
    llvm::Function *func = llvm::Function::Create(funcTy, llvm::GlobalValue::ExternalLinkage, node->getName(), _module);
    _funcName2funcType.insert({node->getName(), funcTy});
    _currentFunction = func;

    llvm::BasicBlock *allocBB = llvm::BasicBlock::Create(_module->getContext(), "init");
    llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(_module->getContext(), "entry");
    allocBB->insertInto(_currentFunction);
    entryBB->insertInto(_currentFunction);

    _theIRBuilder->SetInsertPoint(allocBB);

    int i = 0;
    for (auto param : node->getParams()) {
        param->accept(this);
        // the decl of formal param is the only one
        Variable *var = param->getVariables()[0];
        auto arg = func->getArg(i++);
        _theIRBuilder->CreateStore(arg, _var2addr[var]);
    }
    checkAndCreateBr(entryBB);

    _theIRBuilder->SetInsertPoint(entryBB);
    node->getBlock()->accept(this);

    // if the function didn't execute return before, than return the default value
    if (_hasBrOrRetBlk.find(_theIRBuilder->GetInsertBlock()) == _hasBrOrRetBlk.end()) {
        if (node->getRetType()->getBasicType() == BasicType::INT) {
            _theIRBuilder->CreateRet(_int32Zero);
        } else if (node->getRetType()->getBasicType() == BasicType::FLOAT) {
            _theIRBuilder->CreateRet(_floatZero);
        } else {
            _theIRBuilder->CreateRetVoid();
        }
    }
}

void IRBuilder::visit(Variable *node) {
    llvm::Type *basicLLVMType = convertToLLVMType(node->getBasicType());
    if (node->isGlobal()) {
        // create global variable
        if (auto initValue = node->getInitValue()) {
            initValue->accept(this);
        }
        if (node->getDataType()->getClassId() == ID_ARRAY_TYPE) {
            _value = convertNestedValuesToConstant(static_cast<ArrayType *>(node->getDataType())->getDimensions(), 0, 0,
                                                   basicLLVMType);
        } else {
            if (node->getInitValue() == nullptr) {
                _value = basicLLVMType == _int32Ty ? _int32Zero : _floatZero;
            }
            _value = castToDestTyIfNeed(_value, basicLLVMType);
        }
        assert(llvm::isa<llvm::Constant>(_value));
        _module->getOrInsertGlobal(node->getName(), _value->getType());
        auto globalVar = _module->getNamedGlobal(node->getName());
        globalVar->setInitializer((llvm::Constant *)_value);
        _var2addr.insert({node, globalVar});
    } else {
        llvm::Value *varAddr;
        if (_currentFunction->getEntryBlock().getInstList().empty()) {
            varAddr = _theIRBuilder->CreateAlloca(convertToLLVMType(node->getDataType()), nullptr, node->getName());
        } else {
            auto currentBB = _theIRBuilder->GetInsertBlock();
            _theIRBuilder->SetInsertPoint(&_currentFunction->getEntryBlock().getInstList().front());
            varAddr = _theIRBuilder->CreateAlloca(convertToLLVMType(node->getDataType()), nullptr, node->getName());
            _theIRBuilder->SetInsertPoint(currentBB);
        }
        _var2addr.insert({node, varAddr});

        if (auto initValue = node->getInitValue()) {
            initValue->accept(this);
            if (initValue->getClassId() == ID_NESTED_EXPRESSION) {
                auto memsetFunc = _module->getOrInsertFunction(
                    "llvm.memset.p0i8.i64", _voidTy, _theIRBuilder->getInt8PtrTy(), _theIRBuilder->getInt8Ty(),
                    _theIRBuilder->getInt64Ty(), _theIRBuilder->getInt1Ty());
                auto size = _module->getDataLayout().getTypeAllocSize(varAddr->getType()->getPointerElementType());
                _theIRBuilder->CreateCall(
                    memsetFunc,
                    {_theIRBuilder->CreateBitCast(varAddr, _theIRBuilder->getInt8PtrTy()), _theIRBuilder->getInt8(0),
                     _theIRBuilder->getInt64(size), _theIRBuilder->getInt1(true)});

                auto dimension = static_cast<ArrayType *>(node->getDataType())->getDimensions();
                auto elementSize = static_cast<ArrayType *>(node->getDataType())->getElementSize();
                for (auto item : _nestedExpressionValues) {
                    // get address of the array element from the index
                    int index = item.first;
                    auto tmpAddr = varAddr;
                    for (int i = 0; i < elementSize.size(); i++) {
                        tmpAddr = _theIRBuilder->CreateInBoundsGEP(
                            tmpAddr->getType()->getPointerElementType(), tmpAddr,
                            {_int32Zero, llvm::ConstantInt::get(_int32Ty, index / elementSize[i])});
                        index -= index / elementSize[i] * elementSize[i];
                    }
                    _theIRBuilder->CreateStore(castToDestTyIfNeed(item.second, basicLLVMType), tmpAddr);
                }
                _nestedExpressionValues.clear();

            } else {
                _value = castToDestTyIfNeed(_value, basicLLVMType);
                _theIRBuilder->CreateStore(_value, varAddr);
            }
        }
    }
}

void IRBuilder::visit(ConstVal *node) {
    if (node->getBasicType() == BasicType::INT) {
        _value = llvm::ConstantInt::get(_int32Ty, node->getIntValue());
    } else {
        _value = llvm::ConstantFP::get(_floatTy, node->getFloatValue());
    }
}

void IRBuilder::visit(VarRef *node) {
    if (node->isConst()) {
        if (node->getVariable()->getBasicType() == BasicType::INT) {
            _value = llvm::ConstantInt::get(_int32Ty, ExpressionHandle::evaluateConstExpr(node));
        } else {
            _value = llvm::ConstantFP::get(_floatTy, ExpressionHandle::evaluateConstExpr(node));
        }
        return;
    }
    auto addr = _var2addr[node->getVariable()];
    if (node->getVariable()->getDataType()->getClassId() == ID_ARRAY_TYPE) {
        // cast the array to pointer,
        // int a[10]; 'a' is treated as a pointer when used as a function argument
        _value = addr;
    } else {
        _value = _theIRBuilder->CreateLoad(addr->getType()->getPointerElementType(), addr);
    }
}

void IRBuilder::visit(IndexedRef *node) {
    auto addr = getIndexedRefAddress(node);
    if (addr->getType()->getPointerElementType()->isArrayTy()) {
        // cast the array to pointer
        _value = addr;
    } else {
        _value = _theIRBuilder->CreateLoad(addr->getType()->getPointerElementType(), addr);
    }
}

void IRBuilder::visit(NestedExpression *node) {
    static int deep = 0;
    static int index;
    // dimensions of definded variable
    static std::vector<int> dimensions;

    if (deep == 0) {
        index = 0;
        dimensions.clear();
        _nestedExpressionValues.clear();
        assert(node->getParent()->getClassId() == ID_VARIABLE);
        auto var = (Variable *)node->getParent();
        assert(var->getDataType()->getClassId() == ID_ARRAY_TYPE);
        dimensions = ((ArrayType *)var->getDataType())->getDimensions();
    }

    // The maximum number of elements in a nested expression
    int maxSize = 1;
    for (int i = deep; i < dimensions.size(); i++) {
        maxSize *= dimensions[i];
    }

    deep++;
    const auto &elements = node->getElements();
    // get one value when there are excess elements in a scalar initializer or
    // when there are too many braces around a scalar initializer.
    if (index % maxSize || deep > dimensions.size()) {
        if (!elements.empty()) {
            if (elements[0]->getClassId() == ID_NESTED_EXPRESSION) {
                elements[0]->accept(this);
            } else {
                elements[0]->accept(this);
                _nestedExpressionValues.insert({index++, _value});
            }
        }
    } else {
        int targetIndex = index + maxSize;
        for (size_t i = 0; i < elements.size(); i++) {
            if (elements[i]->getClassId() == ID_NESTED_EXPRESSION) {
                elements[i]->accept(this);
            } else {
                elements[i]->accept(this);
                _nestedExpressionValues.insert({index++, _value});
            }
            // ignore the remaining elements
            if (index == targetIndex) {
                break;
            }
        }
        index = targetIndex;
    }
    deep--;
}

void IRBuilder::visit(UnaryExpression *node) {
    node->getOperand()->accept(this);
    if (node->getOperator() == MINUS) {
        if (_value->getType() == _floatTy) {
            _value = _theIRBuilder->CreateFSub(_floatZero, _value);
        } else if (_value->getType() == _int32Ty) {
            _value = _theIRBuilder->CreateSub(_int32Zero, _value);
        } else {
            _value = _theIRBuilder->CreateSub(_int32Zero, castToDestTyIfNeed(_value, _int32Ty));
        }
    } else if (node->getOperator() == NOT) {
        _value = _theIRBuilder->CreateNot(castToDestTyIfNeed(_value, _int1Ty));
    }
}

void IRBuilder::visit(BinaryExpression *node) {
    if (node->getOperator() == AND || node->getOperator() == OR) {
        // calculate the value of short circuit expr, not for condition
        bool forValue = ExpressionHandle::isForValue(node);
        llvm::Value *valueAddr = nullptr;
        llvm::BasicBlock *saveTrueBB = nullptr;
        llvm::BasicBlock *saveFalseBB = nullptr;
        if (forValue) {
            saveTrueBB = _trueBB;
            saveFalseBB = _falseBB;
            _trueBB = llvm::BasicBlock::Create(_module->getContext(), "valueOneBB");
            _falseBB = llvm::BasicBlock::Create(_module->getContext(), "valueZeroBB");
            _trueBB->insertInto(_currentFunction);
            _falseBB->insertInto(_currentFunction);

            auto currentBB = _theIRBuilder->GetInsertBlock();
            _theIRBuilder->SetInsertPoint(&_currentFunction->getEntryBlock().getInstList().front());
            valueAddr = _theIRBuilder->CreateAlloca(_int32Ty);
            _theIRBuilder->SetInsertPoint(currentBB);
        }

        llvm::BasicBlock *rhsCondBB = llvm::BasicBlock::Create(_module->getContext(), "rhsCondBB");
        rhsCondBB->insertInto(_currentFunction);

        if (node->getOperator() == AND) {
            auto tmpTrueBB = _trueBB;
            _trueBB = rhsCondBB;
            node->getLeft()->accept(this);
            checkAndCreateCondBr(castToDestTyIfNeed(_value, _int1Ty), rhsCondBB, _falseBB);
            _trueBB = tmpTrueBB;
        } else {
            auto tmpFalseBB = _falseBB;
            _falseBB = rhsCondBB;
            node->getLeft()->accept(this);
            checkAndCreateCondBr(castToDestTyIfNeed(_value, _int1Ty), _trueBB, rhsCondBB);
            _falseBB = tmpFalseBB;
        }

        _theIRBuilder->SetInsertPoint(rhsCondBB);
        node->getRight()->accept(this);

        if (forValue) {
            checkAndCreateCondBr(castToDestTyIfNeed(_value, _int1Ty), _trueBB, _falseBB);
            llvm::BasicBlock *afterCalcShortCircuitBB =
                llvm::BasicBlock::Create(_module->getContext(), "afterCalcShortCircuitBB");
            afterCalcShortCircuitBB->insertInto(_currentFunction);

            _theIRBuilder->SetInsertPoint(_trueBB);
            _theIRBuilder->CreateStore(_int32One, valueAddr);
            _theIRBuilder->CreateBr(afterCalcShortCircuitBB);

            _theIRBuilder->SetInsertPoint(_falseBB);
            _theIRBuilder->CreateStore(_int32Zero, valueAddr);
            _theIRBuilder->CreateBr(afterCalcShortCircuitBB);

            _theIRBuilder->SetInsertPoint(afterCalcShortCircuitBB);
            _value = _theIRBuilder->CreateLoad(_int32Ty, valueAddr);

            _trueBB = saveTrueBB;
            _falseBB = saveFalseBB;
        }
        return;
    }

    llvm::Value *left;
    llvm::Value *right;

    // When the left expression is neither a binary operation nor a function call,
    // the right expression is evaluated first.
    // This is particularly useful when the right expression involves a binary operation or a function call.
    if (node->getLeft()->getClassId() != ID_BINARY_EXPRESSION && node->getLeft()->getClassId() != ID_FUNCTION_CALL) {
        node->getRight()->accept(this);
        right = _value;
        node->getLeft()->accept(this);
        left = _value;
    } else {
        node->getLeft()->accept(this);
        left = _value;
        node->getRight()->accept(this);
        right = _value;
    }

    // make the left expr and right expr has the same type
    if (left->getType() == _floatTy || right->getType() == _floatTy) {
        left = castToDestTyIfNeed(left, _floatTy);
        right = castToDestTyIfNeed(right, _floatTy);
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
        left = castToDestTyIfNeed(left, _int32Ty);
        right = castToDestTyIfNeed(right, _int32Ty);
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
    llvm::FunctionType *funcTy = _funcName2funcType[node->getName()];
    int i = 0;
    for (auto rParam : node->getParams()) {
        rParam->accept(this);
        _value = castToDestTyIfNeed(_value, funcTy->getParamType(i++));
        params.push_back(_value);
    }
    _value = _theIRBuilder->CreateCall(_module->getOrInsertFunction(node->getName(), funcTy), params);
}

void IRBuilder::visit(Block *node) {
    for (auto element : node->getElements()) {
        element->accept(this);
        // skip the statements following the jump statement
        if (_hasBrOrRetBlk.find(_theIRBuilder->GetInsertBlock()) != _hasBrOrRetBlk.end()) {
            break;
        }
    }
}

void IRBuilder::visit(AssignStatement *node) {
    node->getRval()->accept(this);
    auto rVal = _value;
    llvm::Value *addr = nullptr;
    if (node->getLval()->getClassId() == ID_VAR_REF) {
        addr = _var2addr[static_cast<VarRef *>(node->getLval())->getVariable()];
    } else {
        addr = getIndexedRefAddress((IndexedRef *)node->getLval());
    }
    rVal = castToDestTyIfNeed(rVal, addr->getType()->getPointerElementType());
    _theIRBuilder->CreateStore(rVal, addr);
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
        checkAndCreateCondBr(castToDestTyIfNeed(_value, _int1Ty), _trueBB, _falseBB);

        _theIRBuilder->SetInsertPoint(elseBB);
        node->getElseStmt()->accept(this);
        checkAndCreateBr(afterIfBB);
    } else {
        _falseBB = afterIfBB;
        node->getCond()->accept(this);
        checkAndCreateCondBr(castToDestTyIfNeed(_value, _int1Ty), _trueBB, _falseBB);
    }

    _theIRBuilder->SetInsertPoint(ifBB);
    node->getStmt()->accept(this);
    checkAndCreateBr(afterIfBB);

    _theIRBuilder->SetInsertPoint(afterIfBB);

    _trueBB = nullptr;
    _falseBB = nullptr;
}

void IRBuilder::visit(WhileStatement *node) {
    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(_module->getContext(), "condBB");
    llvm::BasicBlock *whileBB = llvm::BasicBlock::Create(_module->getContext(), "whileBB");
    llvm::BasicBlock *afterWhileBB = llvm::BasicBlock::Create(_module->getContext(), "afterWhileBB");
    condBB->insertInto(_currentFunction);
    whileBB->insertInto(_currentFunction);
    afterWhileBB->insertInto(_currentFunction);

    checkAndCreateBr(condBB);
    _theIRBuilder->SetInsertPoint(condBB);
    _trueBB = whileBB;
    _falseBB = afterWhileBB;
    node->getCond()->accept(this);
    checkAndCreateCondBr(castToDestTyIfNeed(_value, _int1Ty), _trueBB, _falseBB);

    auto tmpCondBB = _condBB;
    auto tmpAfterBB = _afterBB;
    _condBB = condBB;
    _afterBB = afterWhileBB;
    _theIRBuilder->SetInsertPoint(whileBB);
    node->getStmt()->accept(this);
    checkAndCreateBr(condBB);
    _condBB = tmpCondBB;
    _afterBB = tmpAfterBB;

    _theIRBuilder->SetInsertPoint(afterWhileBB);

    _trueBB = nullptr;
    _falseBB = nullptr;
}

void IRBuilder::visit(BreakStatement *node) { checkAndCreateBr(_afterBB); }

void IRBuilder::visit(ContinueStatement *node) { checkAndCreateBr(_condBB); }

void IRBuilder::visit(ReturnStatement *node) {
    _hasBrOrRetBlk.insert(_theIRBuilder->GetInsertBlock());
    if (node->getExpr()) {
        node->getExpr()->accept(this);
        _theIRBuilder->CreateRet(castToDestTyIfNeed(_value, _currentFunction->getReturnType()));
    } else {
        _theIRBuilder->CreateRetVoid();
    }
}

llvm::Type *IRBuilder::convertToLLVMType(int basicType) {
    switch (basicType) {
        case BasicType::BOOL:
            return _int1Ty;
        case BasicType::INT:
            return _int32Ty;
        case BasicType::FLOAT:
            return _floatTy;
        case BasicType::VOID:
            return _voidTy;
        default:
            assert(false && "should not reach here");
    }
}

llvm::Type *IRBuilder::convertToLLVMType(DataType *dataType) {
    if (dataType->getClassId() == ID_POINTER_TYPE) {
        return llvm::PointerType::get(convertToLLVMType(dataType->getBaseDataType()), 0);
    } else if (dataType->getClassId() == ID_ARRAY_TYPE) {
        auto arrayType = (ArrayType *)dataType;
        const auto &dimensions = arrayType->getDimensions();
        llvm::Type *tmpType = convertToLLVMType(arrayType->getBaseDataType());
        for (auto rbegin = dimensions.rbegin(); rbegin != dimensions.rend(); rbegin++) {
            tmpType = llvm::ArrayType::get(tmpType, *rbegin);
        }
        return tmpType;
    } else {
        return convertToLLVMType(dataType->getBasicType());
    }
}

llvm::Value *IRBuilder::castToDestTyIfNeed(llvm::Value *value, llvm::Type *destTy) {
    if (destTy->isPointerTy()) {
        assert(value->getType()->isPointerTy());
        return _theIRBuilder->CreateBitCast(value, destTy);
    }
    if (destTy == _floatTy) {
        if (value->getType() == _int32Ty) {
            return _theIRBuilder->CreateSIToFP(value, _floatTy);
        } else if (value->getType() == _int1Ty) {
            return _theIRBuilder->CreateUIToFP(value, _floatTy);
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

void IRBuilder::checkAndCreateBr(llvm::BasicBlock *destBlk) {
    if (_hasBrOrRetBlk.find(_theIRBuilder->GetInsertBlock()) == _hasBrOrRetBlk.end()) {
        _theIRBuilder->CreateBr(destBlk);
        _hasBrOrRetBlk.insert(_theIRBuilder->GetInsertBlock());
    }
}

void IRBuilder::checkAndCreateCondBr(llvm::Value *value, llvm::BasicBlock *trueBlk, llvm::BasicBlock *falseBlck) {
    if (_hasBrOrRetBlk.find(_theIRBuilder->GetInsertBlock()) == _hasBrOrRetBlk.end()) {
        _theIRBuilder->CreateCondBr(value, _trueBB, _falseBB);
        _hasBrOrRetBlk.insert(_theIRBuilder->GetInsertBlock());
    }
}

llvm::Value *IRBuilder::getIndexedRefAddress(IndexedRef *indexedRef) {
    Variable *var = indexedRef->getVariable();
    llvm::Value *addr = _var2addr[var];
    if (var->isGlobal() && var->getDataType()->getClassId() == ID_ARRAY_TYPE) {
        addr = _theIRBuilder->CreateBitCast(addr, llvm::PointerType::get(convertToLLVMType(var->getDataType()), 0));
    }
    assert(addr->getType()->isPointerTy());
    const auto &dimension = indexedRef->getDimensions();
    auto begin = dimension.begin();
    if (var->getDataType()->getClassId() == ID_POINTER_TYPE) {
        addr = _theIRBuilder->CreateLoad(addr->getType()->getPointerElementType(), addr);
        (*begin++)->accept(this);
        addr = _theIRBuilder->CreateInBoundsGEP(addr->getType()->getPointerElementType(), addr, _value);
    }
    for (; begin != dimension.end(); begin++) {
        (*begin)->accept(this);
        addr = _theIRBuilder->CreateInBoundsGEP(addr->getType()->getPointerElementType(), addr, {_int32Zero, _value});
    }
    return addr;
}

llvm::Value *IRBuilder::convertNestedValuesToConstant(const std::vector<int> &dimensions, int deep, int begin,
                                                      llvm::Type *basicType) {
    if (deep == dimensions.size()) {
        return castToDestTyIfNeed(_nestedExpressionValues[begin], basicType);
    }

    llvm::Type *partType = basicType;
    for (int i = dimensions.size() - 1; i > deep; i--) {
        partType = llvm::ArrayType::get(partType, dimensions[i]);
    }
    llvm::Constant *zeroInitializer = llvm::ConstantAggregateZero::get(partType);

    std::vector<llvm::Constant *> ret;
    std::vector<llvm::Type *> elementTypes;
    int partSize = 1;
    for (int i = deep + 1; i < dimensions.size(); i++) {
        partSize *= dimensions[i];
    }

    int right = begin + dimensions[deep] * partSize;
    int left = right - partSize;

    // Traverse from the rear to reduce the 'push zeroInitializer' operation
    bool valid = false;
    for (int i = dimensions[deep] - 1; i >= 0; i--) {
        bool pushed = false;
        for (auto item : _nestedExpressionValues) {
            if (item.first >= left && item.first < right) {
                auto partValue = convertNestedValuesToConstant(dimensions, deep + 1, left, basicType);
                ret.push_back((llvm::Constant *)partValue);
                elementTypes.push_back(partValue->getType());
                valid = true;
                pushed = true;
                break;
            }
        }
        if (valid && !pushed) {
            ret.push_back(zeroInitializer);
            elementTypes.push_back(partType);
        }
        left -= partSize;
        right -= partSize;
    }

    std::reverse(ret.begin(), ret.end());
    std::reverse(elementTypes.begin(), elementTypes.end());

    if (ret.size() != dimensions[deep]) {
        // type of remain elements
        llvm::ArrayType *remainType = llvm::ArrayType::get(partType, dimensions[deep] - ret.size());
        ret.push_back(llvm::ConstantAggregateZero::get(remainType));
        elementTypes.push_back(remainType);
    }
    llvm::StructType *retType = llvm::StructType::create(elementTypes);
    return llvm::ConstantStruct::get(retType, ret);
}
}  // namespace LLVMIR
}  // namespace ATC