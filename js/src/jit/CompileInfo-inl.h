/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_CompileInfo_inl_h
#define jit_CompileInfo_inl_h

#include "jit/CompileInfo.h"
#include "jit/IonAllocPolicy.h"

#include "jsscriptinlines.h"

namespace js {
namespace jit {

inline RegExpObject *
CompileInfo::getRegExp(jsbytecode *pc) const
{
    return script_->getRegExp(pc);
}

inline JSFunction *
CompileInfo::getFunction(jsbytecode *pc) const
{
    return script_->getFunction(GET_UINT32_INDEX(pc));
}

InlineScriptTree *
InlineScriptTree::New(TempAllocator *allocator, InlineScriptTree *callerTree,
                      jsbytecode *callerPc, JSScript *script)
{
    JS_ASSERT_IF(!callerTree, !callerPc);
    JS_ASSERT_IF(callerTree, callerTree->script()->containsPC(callerPc));

    // Allocate a new InlineScriptTree
    void *treeMem = allocator->allocate(sizeof(InlineScriptTree));
    if (!treeMem)
        return nullptr;

    // Initialize it.
    return new (treeMem) InlineScriptTree(callerTree, callerPc, script);
}

InlineScriptTree *
InlineScriptTree::addCallee(TempAllocator *allocator, jsbytecode *callerPc,
                            JSScript *calleeScript)
{
    JS_ASSERT(script_ && script_->containsPC(callerPc));
    InlineScriptTree *calleeTree = New(allocator, this, callerPc, calleeScript);
    if (!calleeTree)
        return nullptr;

    calleeTree->nextCallee_ = children_;
    children_ = calleeTree;
    return calleeTree;
}

} // namespace jit
} // namespace js

#endif /* jit_CompileInfo_inl_h */
