/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010-2011 Google Inc. All rights reserved.
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
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#include "config.h"

#if ENABLE(JAVASCRIPT_DEBUGGER) && ENABLE(INSPECTOR)
#include "InspectorDebuggerAgent.h"

#include "CachedResource.h"
#include "ContentSearchUtils.h"
#include "InjectedScript.h"
#include "InjectedScriptManager.h"
#include "InspectorFrontend.h"
#include "InspectorPageAgent.h"
#include "InspectorState.h"
#include "InspectorValues.h"
#include "InstrumentingAgents.h"
#include "RegularExpression.h"
#include "ScriptDebugServer.h"
#include "ScriptObject.h"
#include <wtf/MemoryInstrumentationHashMap.h>
#include <wtf/MemoryInstrumentationVector.h>
#include <wtf/text/WTFString.h>

using WebCore::TypeBuilder::Array;
using WebCore::TypeBuilder::Debugger::FunctionDetails;
using WebCore::TypeBuilder::Debugger::ScriptId;
using WebCore::TypeBuilder::Runtime::RemoteObject;

namespace WebCore {

namespace DebuggerAgentState {
static const char debuggerEnabled[] = "debuggerEnabled";
static const char javaScriptBreakpoints[] = "javaScriptBreakopints";
static const char pauseOnExceptionsState[] = "pauseOnExceptionsState";
};

const char* InspectorDebuggerAgent::backtraceObjectGroup = "backtrace";

InspectorDebuggerAgent::InspectorDebuggerAgent(InstrumentingAgents* instrumentingAgents, InspectorCompositeState* inspectorState, InjectedScriptManager* injectedScriptManager)
    : InspectorBaseAgent<InspectorDebuggerAgent>("Debugger", instrumentingAgents, inspectorState)
    , m_injectedScriptManager(injectedScriptManager)
    , m_frontend(0)
    , m_pausedScriptState(0)
    , m_javaScriptPauseScheduled(false)
    , m_listener(0)
{
    // FIXME: make breakReason optional so that there was no need to init it with "other".
    clearBreakDetails();
    m_state->setLong(DebuggerAgentState::pauseOnExceptionsState, ScriptDebugServer::DontPauseOnExceptions);
}

InspectorDebuggerAgent::~InspectorDebuggerAgent()
{
    ASSERT(!m_instrumentingAgents->inspectorDebuggerAgent());
}

void InspectorDebuggerAgent::enable()
{
    m_instrumentingAgents->setInspectorDebuggerAgent(this);

    // FIXME(WK44513): breakpoints activated flag should be synchronized between all front-ends
    ALL_DEBUG_SERVERS_CALL(setBreakpointsActivated(true))
    startListeningScriptDebugServer();

    if (m_listener)
        m_listener->debuggerWasEnabled();
}

void InspectorDebuggerAgent::disable()
{
    m_state->setObject(DebuggerAgentState::javaScriptBreakpoints, InspectorObject::create());
    m_state->setLong(DebuggerAgentState::pauseOnExceptionsState, ScriptDebugServer::DontPauseOnExceptions);
    m_instrumentingAgents->setInspectorDebuggerAgent(0);

    stopListeningScriptDebugServer();
    ALL_DEBUG_SERVERS_CALL(clearBreakpoints())
    ALL_DEBUG_SERVERS_CALL(clearCompiledScripts())
    clear();

    if (m_listener)
        m_listener->debuggerWasDisabled();
}

bool InspectorDebuggerAgent::enabled()
{
    return m_state->getBoolean(DebuggerAgentState::debuggerEnabled);
}

void InspectorDebuggerAgent::causesRecompilation(ErrorString*, bool* result)
{
    // FIXME: Call this on the correct server.
    *result = scriptDebugServer(JSScriptType).causesRecompilation();
}

void InspectorDebuggerAgent::canSetScriptSource(ErrorString*, bool* result)
{
    // FIXME: Call this on the correct server.
    *result = scriptDebugServer(JSScriptType).canSetScriptSource();
}

void InspectorDebuggerAgent::supportsSeparateScriptCompilationAndExecution(ErrorString*, bool* result)
{
    // FIXME: Call this on the correct server.
    *result = scriptDebugServer(JSScriptType).supportsSeparateScriptCompilationAndExecution();
}

void InspectorDebuggerAgent::enable(ErrorString*)
{
    if (enabled())
        return;

    enable();
    m_state->setBoolean(DebuggerAgentState::debuggerEnabled, true);

    ASSERT(m_frontend);
}

void InspectorDebuggerAgent::disable(ErrorString*)
{
    if (!enabled())
        return;

    disable();
    m_state->setBoolean(DebuggerAgentState::debuggerEnabled, false);
}

void InspectorDebuggerAgent::restore()
{
    if (enabled()) {
        m_frontend->globalObjectCleared();
        enable();
        long pauseState = m_state->getLong(DebuggerAgentState::pauseOnExceptionsState);
        String error;
        setPauseOnExceptionsImpl(&error, pauseState);
    }
}

void InspectorDebuggerAgent::setFrontend(InspectorFrontend* frontend)
{
    m_frontend = frontend->debugger();
}

void InspectorDebuggerAgent::clearFrontend()
{
    m_frontend = 0;

    if (!enabled())
        return;

    disable();

    // FIXME: due to m_state->mute() hack in InspectorController, debuggerEnabled is actually set to false only
    // in InspectorState, but not in cookie. That's why after navigation debuggerEnabled will be true,
    // but after front-end re-open it will still be false.
    m_state->setBoolean(DebuggerAgentState::debuggerEnabled, false);
}

void InspectorDebuggerAgent::setBreakpointsActive(ErrorString*, bool active)
{
    if (active) {
        ALL_DEBUG_SERVERS_CALL(activateBreakpoints())
    } else {
        ALL_DEBUG_SERVERS_CALL(deactivateBreakpoints())
    }
}

bool InspectorDebuggerAgent::isPaused()
{
    // FIXME: Make this more generic.
    return scriptDebugServer(JSScriptType).isPaused() || scriptDebugServer(RBScriptType).isPaused();
}

bool InspectorDebuggerAgent::runningNestedMessageLoop()
{
    // FIXME: Make this more generic.
    return scriptDebugServer(JSScriptType).runningNestedMessageLoop() || scriptDebugServer(RBScriptType).runningNestedMessageLoop();
}

void InspectorDebuggerAgent::addMessageToConsole(MessageSource source, MessageType type)
{
    // FIXME: Make this generic for ScriptType.
    if (scriptDebugServer(JSScriptType).pauseOnExceptionsState() != ScriptDebugServer::DontPauseOnExceptions && source == ConsoleAPIMessageSource && type == AssertMessageType)
        breakProgram(InspectorFrontend::Debugger::Reason::Assert, 0);
}

static PassRefPtr<InspectorObject> buildObjectForBreakpointCookie(const String& url, int lineNumber, int columnNumber, const String& condition, bool isRegex)
{
    RefPtr<InspectorObject> breakpointObject = InspectorObject::create();
    breakpointObject->setString("url", url);
    breakpointObject->setNumber("lineNumber", lineNumber);
    breakpointObject->setNumber("columnNumber", columnNumber);
    breakpointObject->setString("condition", condition);
    breakpointObject->setBoolean("isRegex", isRegex);
    return breakpointObject;
}

static bool matches(const String& url, const String& pattern, bool isRegex)
{
    if (isRegex) {
        RegularExpression regex(pattern, TextCaseSensitive);
        return regex.match(url) != -1;
    }
    return url == pattern;
}

void InspectorDebuggerAgent::setBreakpointByUrl(ErrorString* errorString, int lineNumber, const String* const optionalURL, const String* const optionalURLRegex, const int* const optionalColumnNumber, const String* const optionalCondition, TypeBuilder::Debugger::BreakpointId* outBreakpointId, RefPtr<TypeBuilder::Array<TypeBuilder::Debugger::Location> >& locations)
{
    locations = Array<TypeBuilder::Debugger::Location>::create();
    if (!optionalURL == !optionalURLRegex) {
        *errorString = "Either url or urlRegex must be specified.";
        return;
    }

    String url = optionalURL ? *optionalURL : *optionalURLRegex;
    int columnNumber = optionalColumnNumber ? *optionalColumnNumber : 0;
    String condition = optionalCondition ? *optionalCondition : "";
    bool isRegex = optionalURLRegex;

    String breakpointId = (isRegex ? "/" + url + "/" : url) + ':' + String::number(lineNumber) + ':' + String::number(columnNumber);
    RefPtr<InspectorObject> breakpointsCookie = m_state->getObject(DebuggerAgentState::javaScriptBreakpoints);
    if (breakpointsCookie->find(breakpointId) != breakpointsCookie->end()) {
        *errorString = "Breakpoint at specified location already exists.";
        return;
    }

    breakpointsCookie->setObject(breakpointId, buildObjectForBreakpointCookie(url, lineNumber, columnNumber, condition, isRegex));
    m_state->setObject(DebuggerAgentState::javaScriptBreakpoints, breakpointsCookie);

    ScriptBreakpoint breakpoint(lineNumber, columnNumber, condition);
    for (ScriptsMap::iterator it = m_scripts.begin(); it != m_scripts.end(); ++it) {
        if (!matches(it->value.url, url, isRegex))
            continue;
        RefPtr<TypeBuilder::Debugger::Location> location = resolveBreakpoint(breakpointId, it->key, breakpoint);
        if (location)
            locations->addItem(location);
    }
    *outBreakpointId = breakpointId;
}

static bool parseLocation(ErrorString* errorString, RefPtr<InspectorObject> location, String* scriptId, int* lineNumber, int* columnNumber)
{
    if (!location->getString("scriptId", scriptId) || !location->getNumber("lineNumber", lineNumber)) {
        // FIXME: replace with input validation.
        *errorString = "scriptId and lineNumber are required.";
        return false;
    }
    *columnNumber = 0;
    location->getNumber("columnNumber", columnNumber);
    return true;
}

void InspectorDebuggerAgent::setBreakpoint(ErrorString* errorString, const RefPtr<InspectorObject>& location, const String* const optionalCondition, TypeBuilder::Debugger::BreakpointId* outBreakpointId, RefPtr<TypeBuilder::Debugger::Location>& actualLocation)
{
    String scriptId;
    int lineNumber;
    int columnNumber;

    if (!parseLocation(errorString, location, &scriptId, &lineNumber, &columnNumber))
        return;

    String condition = optionalCondition ? *optionalCondition : emptyString();

    String breakpointId = scriptId + ':' + String::number(lineNumber) + ':' + String::number(columnNumber);
    if (m_breakpointIdToDebugServerBreakpointIds.find(breakpointId) != m_breakpointIdToDebugServerBreakpointIds.end()) {
        *errorString = "Breakpoint at specified location already exists.";
        return;
    }
    ScriptBreakpoint breakpoint(lineNumber, columnNumber, condition);
    actualLocation = resolveBreakpoint(breakpointId, scriptId, breakpoint);
    if (actualLocation)
        *outBreakpointId = breakpointId;
    else
        *errorString = "Could not resolve breakpoint";
}

void InspectorDebuggerAgent::removeBreakpoint(ErrorString*, const String& breakpointId)
{
    RefPtr<InspectorObject> breakpointsCookie = m_state->getObject(DebuggerAgentState::javaScriptBreakpoints);
    breakpointsCookie->remove(breakpointId);
    m_state->setObject(DebuggerAgentState::javaScriptBreakpoints, breakpointsCookie);

    BreakpointIdToDebugServerBreakpointIdsMap::iterator debugServerBreakpointIdsIterator = m_breakpointIdToDebugServerBreakpointIds.find(breakpointId);
    if (debugServerBreakpointIdsIterator == m_breakpointIdToDebugServerBreakpointIds.end())
        return;
    for (size_t i = 0; i < debugServerBreakpointIdsIterator->value.size(); ++i) {
        ALL_DEBUG_SERVERS_CALL(removeBreakpoint(debugServerBreakpointIdsIterator->value[i]))
    }
    m_breakpointIdToDebugServerBreakpointIds.remove(debugServerBreakpointIdsIterator);
}

void InspectorDebuggerAgent::continueToLocation(ErrorString* errorString, const RefPtr<InspectorObject>& location)
{
    if (!m_continueToLocationBreakpointId.isEmpty()) {
        ALL_DEBUG_SERVERS_CALL(removeBreakpoint(m_continueToLocationBreakpointId))
        m_continueToLocationBreakpointId = "";
    }

    String scriptId;
    int lineNumber;
    int columnNumber;

    if (!parseLocation(errorString, location, &scriptId, &lineNumber, &columnNumber))
        return;

    ScriptBreakpoint breakpoint(lineNumber, columnNumber, "");
    m_continueToLocationBreakpointId = ALL_DEBUG_SERVERS_CALL(setBreakpoint(scriptId, breakpoint, &lineNumber, &columnNumber))
    resume(errorString);
}

PassRefPtr<TypeBuilder::Debugger::Location> InspectorDebuggerAgent::resolveBreakpoint(const String& breakpointId, const String& scriptId, const ScriptBreakpoint& breakpoint)
{
    ScriptsMap::iterator scriptIterator = m_scripts.find(scriptId);
    if (scriptIterator == m_scripts.end())
        return 0;
    Script& script = scriptIterator->value;
    if (breakpoint.lineNumber < script.startLine || script.endLine < breakpoint.lineNumber)
        return 0;

    int actualLineNumber;
    int actualColumnNumber;
    String debugServerBreakpointId = ALL_DEBUG_SERVERS_CALL(setBreakpoint(scriptId, breakpoint, &actualLineNumber, &actualColumnNumber))
    if (debugServerBreakpointId.isEmpty())
        return 0;

    BreakpointIdToDebugServerBreakpointIdsMap::iterator debugServerBreakpointIdsIterator = m_breakpointIdToDebugServerBreakpointIds.find(breakpointId);
    if (debugServerBreakpointIdsIterator == m_breakpointIdToDebugServerBreakpointIds.end())
        debugServerBreakpointIdsIterator = m_breakpointIdToDebugServerBreakpointIds.set(breakpointId, Vector<String>()).iterator;
    debugServerBreakpointIdsIterator->value.append(debugServerBreakpointId);

    RefPtr<TypeBuilder::Debugger::Location> location = TypeBuilder::Debugger::Location::create()
        .setScriptId(scriptId)
        .setLineNumber(actualLineNumber);
    location->setColumnNumber(actualColumnNumber);
    return location;
}

static PassRefPtr<InspectorObject> scriptToInspectorObject(ScriptObject scriptObject)
{
    if (scriptObject.hasNoValue())
        return 0;
    RefPtr<InspectorValue> value = scriptObject.toInspectorValue(scriptObject.scriptState());
    if (!value)
        return 0;
    return value->asObject();
}

void InspectorDebuggerAgent::searchInContent(ErrorString* error, const String& scriptId, const String& query, const bool* const optionalCaseSensitive, const bool* const optionalIsRegex, RefPtr<Array<WebCore::TypeBuilder::Page::SearchMatch> >& results)
{
    bool isRegex = optionalIsRegex ? *optionalIsRegex : false;
    bool caseSensitive = optionalCaseSensitive ? *optionalCaseSensitive : false;

    ScriptsMap::iterator it = m_scripts.find(scriptId);
    if (it != m_scripts.end())
        results = ContentSearchUtils::searchInTextByLines(it->value.source, query, caseSensitive, isRegex);
    else
        *error = "No script for id: " + scriptId;
}

void InspectorDebuggerAgent::setScriptSource(ErrorString* error, const String& scriptId, const String& newContent, const bool* const preview, RefPtr<Array<TypeBuilder::Debugger::CallFrame> >& newCallFrames, RefPtr<InspectorObject>& result)
{
    bool previewOnly = preview && *preview;
    ScriptObject resultObject;
    // FIXME: Make this work for all ScriptTypes.
    if (!scriptDebugServer(JSScriptType).setScriptSource(scriptId, newContent, previewOnly, error, &m_currentCallStack, &resultObject))
        return;
    newCallFrames = currentCallFrames();
    RefPtr<InspectorObject> object = scriptToInspectorObject(resultObject);
    if (object)
        result = object;
}
void InspectorDebuggerAgent::restartFrame(ErrorString* errorString, const String& callFrameId, RefPtr<Array<TypeBuilder::Debugger::CallFrame> >& newCallFrames, RefPtr<InspectorObject>& result)
{
    InjectedScript injectedScript = m_injectedScriptManager->injectedScriptForObjectId(callFrameId);
    if (injectedScript.hasNoValue()) {
        *errorString = "Inspected frame has gone";
        return;
    }

    injectedScript.restartFrame(errorString, m_currentCallStack, callFrameId, &result);
    // FIXME: Make this work for all ScriptTypes.
    scriptDebugServer(JSScriptType).updateCallStack(&m_currentCallStack);
    newCallFrames = currentCallFrames();
}

void InspectorDebuggerAgent::getScriptSource(ErrorString* error, const String& scriptId, String* scriptSource)
{
    ScriptsMap::iterator it = m_scripts.find(scriptId);
    if (it != m_scripts.end())
        *scriptSource = it->value.source;
    else
        *error = "No script for id: " + scriptId;
}

void InspectorDebuggerAgent::getFunctionDetails(ErrorString* errorString, const String& functionId, RefPtr<TypeBuilder::Debugger::FunctionDetails>& details)
{
    InjectedScript injectedScript = m_injectedScriptManager->injectedScriptForObjectId(functionId);
    if (injectedScript.hasNoValue()) {
        *errorString = "Function object id is obsolete";
        return;
    }
    injectedScript.getFunctionDetails(errorString, functionId, &details);
}

void InspectorDebuggerAgent::schedulePauseOnNextStatement(InspectorFrontend::Debugger::Reason::Enum breakReason, PassRefPtr<InspectorObject> data)
{
    if (m_javaScriptPauseScheduled)
        return;
    m_breakReason = breakReason;
    m_breakAuxData = data;
    ALL_DEBUG_SERVERS_CALL(setPauseOnNextStatement(true))
}

void InspectorDebuggerAgent::cancelPauseOnNextStatement()
{
    if (m_javaScriptPauseScheduled)
        return;
    clearBreakDetails();
    ALL_DEBUG_SERVERS_CALL(setPauseOnNextStatement(false))
}

void InspectorDebuggerAgent::pause(ErrorString*)
{
    if (m_javaScriptPauseScheduled)
        return;
    clearBreakDetails();
    ALL_DEBUG_SERVERS_CALL(setPauseOnNextStatement(true))
    m_javaScriptPauseScheduled = true;
}

void InspectorDebuggerAgent::resume(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    m_injectedScriptManager->releaseObjectGroup(InspectorDebuggerAgent::backtraceObjectGroup);
    ALL_DEBUG_SERVERS_CALL(continueProgram())
}

void InspectorDebuggerAgent::stepOver(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    m_injectedScriptManager->releaseObjectGroup(InspectorDebuggerAgent::backtraceObjectGroup);
    ALL_DEBUG_SERVERS_CALL(stepOverStatement())
}

void InspectorDebuggerAgent::stepInto(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    m_injectedScriptManager->releaseObjectGroup(InspectorDebuggerAgent::backtraceObjectGroup);
    ALL_DEBUG_SERVERS_CALL(stepIntoStatement())
    m_listener->stepInto();
}

void InspectorDebuggerAgent::stepOut(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    m_injectedScriptManager->releaseObjectGroup(InspectorDebuggerAgent::backtraceObjectGroup);
    ALL_DEBUG_SERVERS_CALL(stepOutOfFunction())
}

void InspectorDebuggerAgent::setPauseOnExceptions(ErrorString* errorString, const String& stringPauseState)
{
    ScriptDebugServer::PauseOnExceptionsState pauseState;
    if (stringPauseState == "none")
        pauseState = ScriptDebugServer::DontPauseOnExceptions;
    else if (stringPauseState == "all")
        pauseState = ScriptDebugServer::PauseOnAllExceptions;
    else if (stringPauseState == "uncaught")
        pauseState = ScriptDebugServer::PauseOnUncaughtExceptions;
    else {
        *errorString = "Unknown pause on exceptions mode: " + stringPauseState;
        return;
    }
    setPauseOnExceptionsImpl(errorString, pauseState);
}

void InspectorDebuggerAgent::setPauseOnExceptionsImpl(ErrorString* errorString, int pauseState)
{
    ALL_DEBUG_SERVERS_CALL(setPauseOnExceptionsState(static_cast<ScriptDebugServer::PauseOnExceptionsState>(pauseState)))
    // FIXME: Do this more generically.
    if (scriptDebugServer(JSScriptType).pauseOnExceptionsState() != pauseState || scriptDebugServer(RBScriptType).pauseOnExceptionsState() != pauseState)
        *errorString = "Internal error. Could not change pause on exceptions state";
    else
        m_state->setLong(DebuggerAgentState::pauseOnExceptionsState, pauseState);
}

void InspectorDebuggerAgent::evaluateOnCallFrame(ErrorString* errorString, const String& callFrameId, const String& expression, const String* const objectGroup, const bool* const includeCommandLineAPI, const bool* const doNotPauseOnExceptionsAndMuteConsole, const bool* const returnByValue, const bool* generatePreview, RefPtr<TypeBuilder::Runtime::RemoteObject>& result, TypeBuilder::OptOutput<bool>* wasThrown)
{
    InjectedScript injectedScript = m_injectedScriptManager->injectedScriptForObjectId(callFrameId);
    if (injectedScript.hasNoValue()) {
        *errorString = "Inspected frame has gone";
        return;
    }

    ScriptType scriptType = injectedScript.scriptType();
    ScriptDebugServer::PauseOnExceptionsState previousPauseOnExceptionsState = scriptDebugServer(scriptType).pauseOnExceptionsState();
    if (doNotPauseOnExceptionsAndMuteConsole ? *doNotPauseOnExceptionsAndMuteConsole : false) {
        if (previousPauseOnExceptionsState != ScriptDebugServer::DontPauseOnExceptions)
            scriptDebugServer(scriptType).setPauseOnExceptionsState(ScriptDebugServer::DontPauseOnExceptions);
        muteConsole();
    }

    injectedScript.evaluateOnCallFrame(errorString, m_currentCallStack, callFrameId, expression, objectGroup ? *objectGroup : "", includeCommandLineAPI ? *includeCommandLineAPI : false, returnByValue ? *returnByValue : false, generatePreview ? *generatePreview : false, &result, wasThrown);

    if (doNotPauseOnExceptionsAndMuteConsole ? *doNotPauseOnExceptionsAndMuteConsole : false) {
        unmuteConsole();
        if (scriptDebugServer(scriptType).pauseOnExceptionsState() != previousPauseOnExceptionsState)
            scriptDebugServer(scriptType).setPauseOnExceptionsState(previousPauseOnExceptionsState);
    }
}

void InspectorDebuggerAgent::compileScript(ErrorString* errorString, const String& expression, const String& sourceURL, TypeBuilder::OptOutput<ScriptId>* scriptId, TypeBuilder::OptOutput<String>* syntaxErrorMessage)
{
    // FIXME: Get the correct injected script for the correct ScriptType.
    InjectedScript injectedScript = injectedScriptForEval(errorString, 0);
    if (injectedScript.hasNoValue()) {
        *errorString = "Inspected frame has gone";
        return;
    }

    String scriptIdValue;
    String exceptionMessage;
    scriptDebugServer(injectedScript.scriptType()).compileScript(injectedScript.scriptState(), expression, sourceURL, &scriptIdValue, &exceptionMessage);
    if (!scriptIdValue && !exceptionMessage) {
        *errorString = "Script compilation failed";
        return;
    }
    *syntaxErrorMessage = exceptionMessage;
    *scriptId = scriptIdValue;
}

void InspectorDebuggerAgent::runScript(ErrorString* errorString, const ScriptId& scriptId, const int* executionContextId, const String* const objectGroup, const bool* const doNotPauseOnExceptionsAndMuteConsole, RefPtr<TypeBuilder::Runtime::RemoteObject>& result, TypeBuilder::OptOutput<bool>* wasThrown)
{
    InjectedScript injectedScript = injectedScriptForEval(errorString, executionContextId);
    if (injectedScript.hasNoValue()) {
        *errorString = "Inspected frame has gone";
        return;
    }

    ScriptType scriptType = injectedScript.scriptType();
    ScriptDebugServer::PauseOnExceptionsState previousPauseOnExceptionsState = scriptDebugServer(scriptType).pauseOnExceptionsState();
    if (doNotPauseOnExceptionsAndMuteConsole && *doNotPauseOnExceptionsAndMuteConsole) {
        if (previousPauseOnExceptionsState != ScriptDebugServer::DontPauseOnExceptions)
            scriptDebugServer(scriptType).setPauseOnExceptionsState(ScriptDebugServer::DontPauseOnExceptions);
        muteConsole();
    }

    ScriptValue value;
    bool wasThrownValue;
    String exceptionMessage;
    scriptDebugServer(scriptType).runScript(injectedScript.scriptState(), scriptId, &value, &wasThrownValue, &exceptionMessage);
    *wasThrown = wasThrownValue;
    if (value.hasNoValue()) {
        *errorString = "Script execution failed";
        return;
    }
    result = injectedScript.wrapObject(value, objectGroup ? *objectGroup : "");
    if (wasThrownValue)
        result->setDescription(exceptionMessage);

    if (doNotPauseOnExceptionsAndMuteConsole && *doNotPauseOnExceptionsAndMuteConsole) {
        unmuteConsole();
        if (scriptDebugServer(scriptType).pauseOnExceptionsState() != previousPauseOnExceptionsState)
            scriptDebugServer(scriptType).setPauseOnExceptionsState(previousPauseOnExceptionsState);
    }
}

void InspectorDebuggerAgent::setOverlayMessage(ErrorString*, const String*)
{
}

void InspectorDebuggerAgent::setVariableValue(ErrorString* errorString, int scopeNumber, const String& variableName, const RefPtr<InspectorObject>& newValue, const String* callFrameId, const String* functionObjectId)
{
    InjectedScript injectedScript;
    if (callFrameId) {
        injectedScript = m_injectedScriptManager->injectedScriptForObjectId(*callFrameId);
        if (injectedScript.hasNoValue()) {
            *errorString = "Inspected frame has gone";
            return;
        }
    } else if (functionObjectId) {
        injectedScript = m_injectedScriptManager->injectedScriptForObjectId(*functionObjectId);
        if (injectedScript.hasNoValue()) {
            *errorString = "Function object id cannot be resolved";
            return;
        }
    } else {
        *errorString = "Either call frame or function object must be specified";
        return;
    }
    String newValueString = newValue->toJSONString();

    injectedScript.setVariableValue(errorString, m_currentCallStack, callFrameId, functionObjectId, scopeNumber, variableName, newValueString);
}

void InspectorDebuggerAgent::scriptExecutionBlockedByCSP(const String& directiveText)
{
    // FIXME: Make this generic for ScriptTypes.
    if (scriptDebugServer(JSScriptType).pauseOnExceptionsState() != ScriptDebugServer::DontPauseOnExceptions) {
        RefPtr<InspectorObject> directive = InspectorObject::create();
        directive->setString("directiveText", directiveText);
        breakProgram(InspectorFrontend::Debugger::Reason::CSPViolation, directive.release());
    }
}

PassRefPtr<Array<TypeBuilder::Debugger::CallFrame> > InspectorDebuggerAgent::currentCallFrames()
{
    if (!m_pausedScriptState)
        return Array<TypeBuilder::Debugger::CallFrame>::create();
    InjectedScript injectedScript = m_injectedScriptManager->injectedScriptFor(m_pausedScriptState);
    if (injectedScript.hasNoValue()) {
        ASSERT_NOT_REACHED();
        return Array<TypeBuilder::Debugger::CallFrame>::create();
    }
    return injectedScript.wrapCallFrames(m_currentCallStack);
}

String InspectorDebuggerAgent::sourceMapURLForScript(const Script& script)
{
    DEFINE_STATIC_LOCAL(String, sourceMapHttpHeader, (ASCIILiteral("X-SourceMap")));

    String sourceMapURL = ContentSearchUtils::findSourceMapURL(script.source);
    if (!sourceMapURL.isEmpty())
        return sourceMapURL;

    if (script.url.isEmpty())
        return String();

    InspectorPageAgent* pageAgent = m_instrumentingAgents->inspectorPageAgent();
    if (!pageAgent)
        return String();

    CachedResource* resource = pageAgent->cachedResource(pageAgent->mainFrame(), KURL(ParsedURLString, script.url));
    if (resource)
        return resource->response().httpHeaderField(sourceMapHttpHeader);
    return String();
}

// JavaScriptDebugListener functions

void InspectorDebuggerAgent::didParseSource(const String& scriptId, const Script& script)
{
    // Don't send script content to the front end until it's really needed.
    const bool* isContentScript = script.isContentScript ? &script.isContentScript : 0;
    String sourceMapURL = sourceMapURLForScript(script);
    String* sourceMapURLParam = sourceMapURL.isNull() ? 0 : &sourceMapURL;
    String sourceURL;
    if (!script.startLine && !script.startColumn)
        sourceURL = ContentSearchUtils::findSourceURL(script.source);
    bool hasSourceURL = !sourceURL.isEmpty();
    String scriptURL = hasSourceURL ? sourceURL : script.url;
    bool* hasSourceURLParam = hasSourceURL ? &hasSourceURL : 0;
    m_frontend->scriptParsed(scriptId, scriptURL, script.startLine, script.startColumn, script.endLine, script.endColumn, isContentScript, sourceMapURLParam, hasSourceURLParam);

    m_scripts.set(scriptId, script);

    if (scriptURL.isEmpty())
        return;

    RefPtr<InspectorObject> breakpointsCookie = m_state->getObject(DebuggerAgentState::javaScriptBreakpoints);
    for (InspectorObject::iterator it = breakpointsCookie->begin(); it != breakpointsCookie->end(); ++it) {
        RefPtr<InspectorObject> breakpointObject = it->value->asObject();
        bool isRegex;
        breakpointObject->getBoolean("isRegex", &isRegex);
        String url;
        breakpointObject->getString("url", &url);
        if (!matches(scriptURL, url, isRegex))
            continue;
        ScriptBreakpoint breakpoint;
        breakpointObject->getNumber("lineNumber", &breakpoint.lineNumber);
        breakpointObject->getNumber("columnNumber", &breakpoint.columnNumber);
        breakpointObject->getString("condition", &breakpoint.condition);
        RefPtr<TypeBuilder::Debugger::Location> location = resolveBreakpoint(it->key, scriptId, breakpoint);
        if (location)
            m_frontend->breakpointResolved(it->key, location);
    }
}

void InspectorDebuggerAgent::failedToParseSource(const String& url, const String& data, int firstLine, int errorLine, const String& errorMessage)
{
    m_frontend->scriptFailedToParse(url, data, firstLine, errorLine, errorMessage);
}

void InspectorDebuggerAgent::didPause(ScriptState* scriptState, const ScriptValue& callFrames, const ScriptValue& exception)
{
    ASSERT(scriptState && !m_pausedScriptState);
    m_pausedScriptState = scriptState;
    m_currentCallStack = callFrames;

    if (!exception.hasNoValue()) {
        InjectedScript injectedScript = m_injectedScriptManager->injectedScriptFor(scriptState);
        if (!injectedScript.hasNoValue()) {
            m_breakReason = InspectorFrontend::Debugger::Reason::Exception;
            m_breakAuxData = injectedScript.wrapObject(exception, "backtrace")->openAccessors();
            // m_breakAuxData might be null after this.
        }
    }

    m_frontend->paused(currentCallFrames(), m_breakReason, m_breakAuxData);
    m_javaScriptPauseScheduled = false;

    if (!m_continueToLocationBreakpointId.isEmpty()) {
        scriptDebugServer(scriptState->scriptType()).removeBreakpoint(m_continueToLocationBreakpointId);
        m_continueToLocationBreakpointId = "";
    }
    if (m_listener)
        m_listener->didPause();
}

void InspectorDebuggerAgent::didContinue()
{
    m_pausedScriptState = 0;
    m_currentCallStack = ScriptValue();
    clearBreakDetails();
    m_frontend->resumed();
}

void InspectorDebuggerAgent::breakProgram(InspectorFrontend::Debugger::Reason::Enum breakReason, PassRefPtr<InspectorObject> data)
{
    m_breakReason = breakReason;
    m_breakAuxData = data;
    ALL_DEBUG_SERVERS_CALL(breakProgram())
}

void InspectorDebuggerAgent::clear()
{
    m_pausedScriptState = 0;
    m_currentCallStack = ScriptValue();
    m_scripts.clear();
    m_breakpointIdToDebugServerBreakpointIds.clear();
    m_continueToLocationBreakpointId = String();
    clearBreakDetails();
    m_javaScriptPauseScheduled = false;
    ErrorString error;
    setOverlayMessage(&error, 0);
}

bool InspectorDebuggerAgent::assertPaused(ErrorString* errorString)
{
    if (!m_pausedScriptState) {
        *errorString = "Can only perform operation while paused.";
        return false;
    }
    return true;
}

void InspectorDebuggerAgent::clearBreakDetails()
{
    m_breakReason = InspectorFrontend::Debugger::Reason::Other;
    m_breakAuxData = 0;
}

void InspectorDebuggerAgent::reportMemoryUsage(MemoryObjectInfo* memoryObjectInfo) const
{
    MemoryClassInfo info(memoryObjectInfo, this, WebCoreMemoryTypes::InspectorDebuggerAgent);
    InspectorBaseAgent<InspectorDebuggerAgent>::reportMemoryUsage(memoryObjectInfo);
    info.addMember(m_injectedScriptManager, "injectedScriptManager");
    info.addWeakPointer(m_frontend);
    info.addMember(m_pausedScriptState, "pausedScriptState");
    info.addMember(m_currentCallStack, "currentCallStack");
    info.addMember(m_scripts, "scripts");
    info.addMember(m_breakpointIdToDebugServerBreakpointIds, "breakpointIdToDebugServerBreakpointIds");
    info.addMember(m_continueToLocationBreakpointId, "continueToLocationBreakpointId");
    info.addMember(m_breakAuxData, "breakAuxData");
    info.addWeakPointer(m_listener);
}

void ScriptDebugListener::Script::reportMemoryUsage(MemoryObjectInfo* memoryObjectInfo) const
{
    MemoryClassInfo info(memoryObjectInfo, this, WebCoreMemoryTypes::InspectorDebuggerAgent);
    info.addMember(url, "url");
    info.addMember(source, "source");
    info.addMember(sourceMappingURL, "sourceMappingURL");
}

void InspectorDebuggerAgent::reset()
{
    m_scripts.clear();
    m_breakpointIdToDebugServerBreakpointIds.clear();
    if (m_frontend)
        m_frontend->globalObjectCleared();
}

} // namespace WebCore

#endif // ENABLE(JAVASCRIPT_DEBUGGER) && ENABLE(INSPECTOR)
