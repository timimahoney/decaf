/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ArrayValue_h
#define ArrayValue_h

#include "ScriptType.h"
#include <interpreter/CallFrame.h>
#include <Ruby/ruby.h>

// FIXME: Make this more generic. 
// Right now, it knows what language it's for by
// checking whether m_value or m_valueRB are filled.
// That's pretty dumb.

namespace WebCore {

class Dictionary;

class ArrayValue {
public:
    ArrayValue();
    ArrayValue(JSC::ExecState*, JSC::JSValue);
    ArrayValue(VALUE arrayRB);

    ArrayValue& operator=(const ArrayValue&);

    bool isUndefinedOrNull() const;

    bool length(size_t&) const;
    bool get(size_t index, Dictionary&) const;

private:
    JSC::ExecState* m_exec;
    JSC::JSValue m_value;
    VALUE m_valueRB;
    ScriptType m_scriptType;
};

}

#endif // ArrayValue_h
