/*	
        WebBaseNetscapePluginView.m
	Copyright 2002, Apple, Inc. All rights reserved.
*/

#define USE_CARBON 1

#import <WebKit/WebBaseNetscapePluginView.h>
#import <WebKit/WebController.h>
#import <WebKit/WebControllerPrivate.h>
#import <WebKit/WebDataSource.h>
#import <WebKit/WebFrame.h>
#import <WebKit/WebFramePrivate.h>
#import <WebKit/WebKitLogging.h>
#import <WebKit/WebNullPluginView.h>
#import <WebKit/WebNSViewExtras.h>

#import <WebKit/WebPluginStream.h>
#import <WebKit/WebPluginNullEventSender.h>
#import <WebKit/WebPlugin.h>
#import <WebKit/WebView.h>
#import <WebKit/WebWindowOperationsDelegate.h>

//#import <WebFoundation/WebAssertions.h>
#import <WebFoundation/WebFoundation.h>
#import <WebFoundation/WebNSStringExtras.h>
#import <WebFoundation/WebNSURLExtras.h>

#import <AppKit/NSEvent_Private.h>
#import <AppKit/NSWindow_Private.h>
#import <Carbon/Carbon.h>

@implementation WebBaseNetscapePluginView

#pragma mark EVENTS

+ (void)getCarbonEvent:(EventRecord *)carbonEvent
{
    carbonEvent->what = nullEvent;
    carbonEvent->message = 0;
    carbonEvent->when = TickCount();
    GetGlobalMouse(&carbonEvent->where);
    carbonEvent->modifiers = GetCurrentKeyModifiers();
    if (!Button())
        carbonEvent->modifiers |= btnState;
}

- (void)getCarbonEvent:(EventRecord *)carbonEvent
{
    [[self class] getCarbonEvent:carbonEvent];
}

- (EventModifiers)modifiersForEvent:(NSEvent *)event
{
    EventModifiers modifiers;
    unsigned int modifierFlags = [event modifierFlags];
    NSEventType eventType = [event type];
    
    modifiers = 0;
    
    if (eventType != NSLeftMouseDown && eventType != NSRightMouseDown)
        modifiers |= btnState;
    
    if (modifierFlags & NSCommandKeyMask)
        modifiers |= cmdKey;
    
    if (modifierFlags & NSShiftKeyMask)
        modifiers |= shiftKey;

    if (modifierFlags & NSAlphaShiftKeyMask)
        modifiers |= alphaLock;

    if (modifierFlags & NSAlternateKeyMask)
        modifiers |= optionKey;

    if (modifierFlags & NSControlKeyMask || eventType == NSRightMouseDown)
        modifiers |= controlKey;
    
    return modifiers;
}

- (void)getCarbonEvent:(EventRecord *)carbonEvent withEvent:(NSEvent *)cocoaEvent
{
    if([cocoaEvent _eventRef] && ConvertEventRefToEventRecord([cocoaEvent _eventRef], carbonEvent)){
        return;
    } else {
        NSPoint where;
        
        where = [[cocoaEvent window] convertBaseToScreen:[cocoaEvent locationInWindow]];
        
        carbonEvent->what = nullEvent;
        carbonEvent->message = 0;
        carbonEvent->when = (UInt32)([cocoaEvent timestamp] * 60); // seconds to ticks
        carbonEvent->where.h = (short)where.x;
        carbonEvent->where.v = (short)(NSMaxY([[[NSScreen screens] objectAtIndex:0] frame]) - where.y);
        carbonEvent->modifiers = [self modifiersForEvent:cocoaEvent];
    }
}

- (UInt32)keyMessageForEvent:(NSEvent *)theEvent
{
    NSData *data;
    UInt8 characterCode;
    UInt16 keyCode;
    UInt32 message=0;

    data = [[theEvent characters] dataUsingEncoding:CFStringConvertEncodingToNSStringEncoding(CFStringGetSystemEncoding())];
    if(data){
        [data getBytes:&characterCode length:1];
        keyCode = [theEvent keyCode];
        message = keyCode << 8 | characterCode;
    }
    return message;
}

- (BOOL)sendEvent:(EventRecord *)event
{
    BOOL defers = [[self controller] _defersCallbacks];
    if (!defers) {
        [[self controller] _setDefersCallbacks:YES];
    }

    BOOL acceptedEvent = NPP_HandleEvent(instance, event);

    if (!defers) {
        [[self controller] _setDefersCallbacks:NO];
    }
    
    return acceptedEvent;
}

- (void)sendActivateEvent:(BOOL)activate
{
    EventRecord event;
    
    [self getCarbonEvent:&event];
    event.what = activateEvt;
    WindowRef windowRef = [[self window] _windowRef];
    event.message = (UInt32)windowRef;
    if (activate)
        event.modifiers |= activeFlag;
    
    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(activateEvent): %d  isActive: %d", acceptedEvent, (event.modifiers & activeFlag));
}

- (BOOL)sendUpdateEvent
{
    EventRecord event;
    
    [self getCarbonEvent:&event];
    event.what = updateEvt;
    WindowRef windowRef = [[self window] _windowRef];
    event.message = (UInt32)windowRef;

    BOOL acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(updateEvt): %d", acceptedEvent);
    
    return acceptedEvent;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)becomeFirstResponder
{
    EventRecord event;
    
    [self getCarbonEvent:&event];
    event.what = getFocusEvent;
    
    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(getFocusEvent): %d", acceptedEvent);
    return YES;
}

- (BOOL)resignFirstResponder
{
    EventRecord event;
    
    [self getCarbonEvent:&event];
    event.what = loseFocusEvent;
    
    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(loseFocusEvent): %d", acceptedEvent);
    return YES;
}

- (void)mouseDown:(NSEvent *)theEvent
{
    EventRecord event;

    [self getCarbonEvent:&event withEvent:theEvent];
    event.what = mouseDown;

    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(mouseDown): %d pt.v=%d, pt.h=%d", acceptedEvent, event.where.v, event.where.h);
}

- (void)mouseUp:(NSEvent *)theEvent
{
    EventRecord event;
    
    [self getCarbonEvent:&event withEvent:theEvent];
    event.what = mouseUp;

    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(mouseUp): %d pt.v=%d, pt.h=%d", acceptedEvent, event.where.v, event.where.h);
}

- (void)mouseEntered:(NSEvent *)theEvent
{
    EventRecord event;
    
    [self getCarbonEvent:&event withEvent:theEvent];
    event.what = adjustCursorEvent;

    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(mouseEntered): %d", acceptedEvent);
}

- (void)mouseExited:(NSEvent *)theEvent
{
    EventRecord event;
        
    [self getCarbonEvent:&event withEvent:theEvent];
    event.what = adjustCursorEvent;

    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event]; 
    
    LOG(Plugins, "NPP_HandleEvent(mouseExited): %d", acceptedEvent);
    
    // Set cursor back to arrow cursor.
    [[NSCursor arrowCursor] set];
}

- (void)keyUp:(NSEvent *)theEvent
{
    EventRecord event;

    [self getCarbonEvent:&event withEvent:theEvent];
    event.what = keyUp;

    if(event.message == 0){
        event.message = [self keyMessageForEvent:theEvent];
    }
    
    BOOL acceptedEvent = [self sendEvent:&event];

    LOG(Plugins, "NPP_HandleEvent(keyUp): %d charCode:%c keyCode:%lu",
                     acceptedEvent, (char) (event.message & charCodeMask), (event.message & keyCodeMask));
    
    // If the plug-in didn't accept this event,
    // pass it along so that keyboard scrolling, for example, will work.
    if (!acceptedEvent){
        [super keyUp:theEvent];
    }
}

- (void)keyDown:(NSEvent *)theEvent
{
    EventRecord event;

    // Some command keys are sent with both performKeyEquivalent and keyDown.
    // We should send only 1 keyDown to the plug-in, so we'll ignore this one.
    if([theEvent modifierFlags] & NSCommandKeyMask){
        return;
    }
    
    [self getCarbonEvent:&event withEvent:theEvent];
    event.what = keyDown;

    if(event.message == 0){
        event.message = [self keyMessageForEvent:theEvent];
    }
    
    BOOL acceptedEvent = [self sendEvent:&event];

    LOG(Plugins, "NPP_HandleEvent(keyDown): %d charCode:%c keyCode:%lu",
                     acceptedEvent, (char) (event.message & charCodeMask), (event.message & keyCodeMask));
    
    // If the plug-in didn't accept this event,
    // pass it along so that keyboard scrolling, for example, will work.
    if (!acceptedEvent){
        [super keyDown:theEvent];
    }
}

- (BOOL)isInResponderChain
{
    NSResponder *responder = [[self window] firstResponder];

    while(responder != nil){
        if(responder == self){
            return YES;
        }
        responder = [responder nextResponder];
    }
    return NO;
}

// Must subclass performKeyEquivalent: for command-modified keys to work.
- (BOOL)performKeyEquivalent:(NSEvent *)theEvent
{
    EventRecord event;

    if(![self isInResponderChain]){
        return NO;
    }
    
    [self getCarbonEvent:&event withEvent:theEvent];
    event.what = keyDown;

    if(event.message == 0){
        event.message = [self keyMessageForEvent:theEvent];
    }

    BOOL acceptedEvent = [self sendEvent:&event];

    LOG(Plugins, "NPP_HandleEvent(performKeyEquivalent): %d charCode:%c keyCode:%lu",
                     acceptedEvent, (char) (event.message & charCodeMask), (event.message & keyCodeMask));
    
    return acceptedEvent;
}

// Must subclass menuForEvent: for right-click to work.
- (NSMenu *)menuForEvent:(NSEvent *)theEvent
{
    EventRecord event;
    
    [self getCarbonEvent:&event withEvent:theEvent];

    BOOL acceptedEvent;
    acceptedEvent = [self sendEvent:&event];
    
    LOG(Plugins, "NPP_HandleEvent(menuForEvent): %d pt.v=%d, pt.h=%d", acceptedEvent, event.where.v, event.where.h);

    return nil;
}

#pragma mark WEB_NETSCAPE_PLUGIN

- (void)setUpWindowAndPort
{
    CGrafPtr port = GetWindowPort([[self window] _windowRef]);
    NSRect contentViewFrame = [[[self window] contentView] frame];
    NSRect boundsInWindow = [self convertRect:[self bounds] toView:nil];
    NSRect visibleRectInWindow = [self convertRect:[self visibleRect] toView:nil];
    
    // flip Y coordinates
    boundsInWindow.origin.y = contentViewFrame.size.height - boundsInWindow.origin.y - boundsInWindow.size.height; 
    visibleRectInWindow.origin.y = contentViewFrame.size.height - visibleRectInWindow.origin.y - visibleRectInWindow.size.height;
    
    nPort.port = port;
    
    // FIXME: Are these values correct? Without them, Flash freaks.
    nPort.portx = -(int32)boundsInWindow.origin.x;
    nPort.porty = -(int32)boundsInWindow.origin.y;
    
    window.window = &nPort;
    
    window.x = (int32)boundsInWindow.origin.x; 
    window.y = (int32)boundsInWindow.origin.y;

    window.width = (uint32)boundsInWindow.size.width;
    window.height = (uint32)boundsInWindow.size.height;

    window.clipRect.top = (uint16)visibleRectInWindow.origin.y;
    window.clipRect.left = (uint16)visibleRectInWindow.origin.x;
    window.clipRect.bottom = (uint16)(visibleRectInWindow.origin.y + visibleRectInWindow.size.height);
    window.clipRect.right = (uint16)(visibleRectInWindow.origin.x + visibleRectInWindow.size.width);
    
    window.type = NPWindowTypeWindow;
}

- (void)setWindow
{
    [self setUpWindowAndPort];

    NPError npErr;
    npErr = NPP_SetWindow(instance, &window);
    LOG(Plugins, "NPP_SetWindow: %d, port=0x%08x, window.x:%d window.y:%d",
                     npErr, (int)nPort.port, (int)window.x, (int)window.y);

#if 0
    // Draw test    
    Rect portRect;
    GetPortBounds(port, &portRect);
    SetPort(port);
    MoveTo(0,0);
    LineTo(portRect.right, portRect.bottom);
#endif
}

- (void)removeTrackingRect
{
    if (trackingTag) {
        [self removeTrackingRect:trackingTag];
        // Must release the window to balance the retain in resetTrackingRect.
        [[self window] release];
        trackingTag = 0;
    }
}

- (void)resetTrackingRect
{
    [self removeTrackingRect];
    if (isStarted) {
        // Must retain the window so that removeTrackingRect can work after the window is closed.
        [[self window] retain];
        trackingTag = [self addTrackingRect:[self bounds] owner:self userData:nil assumeInside:NO];
    }
}

-(void)start
{
    if (isStarted || !canRestart || NPP_New == 0){
        return;
    }
    
    isStarted = YES;
    
    NPError npErr;
    npErr = NPP_New((char *)[MIMEType cString], instance, mode, argsCount, cAttributes, cValues, NULL);
    LOG(Plugins, "NPP_New: %d", npErr);
    
    // Create a WindowRef is one doesn't already exist
    [[self window] _windowRef];
        
    [self setWindow];
    
    NSWindow *theWindow = [self window];
    NSNotificationCenter *notificationCenter = [NSNotificationCenter defaultCenter];
    NSView *view;
    for (view = self; view; view = [view superview]) {
        [notificationCenter addObserver:self selector:@selector(viewHasMoved:) 
            name:NSViewFrameDidChangeNotification object:view];
        [notificationCenter addObserver:self selector:@selector(viewHasMoved:) 
            name:NSViewBoundsDidChangeNotification object:view];
    }
    [notificationCenter addObserver:self selector:@selector(windowWillClose:)
            name:NSWindowWillCloseNotification object:theWindow];
    [notificationCenter addObserver:self selector:@selector(windowBecameKey:) 
        name:NSWindowDidBecomeKeyNotification object:theWindow];
    [notificationCenter addObserver:self selector:@selector(windowResignedKey:) 
        name:NSWindowDidResignKeyNotification object:theWindow];
    [notificationCenter addObserver:self selector:@selector(defaultsHaveChanged:) 
        name:NSUserDefaultsDidChangeNotification object:nil];

    if ([theWindow isKeyWindow]){
        [self sendActivateEvent:YES];
    }
    
    eventSender = [[WebNetscapePluginNullEventSender alloc] initWithPluginView:self];
    [eventSender sendNullEvents];
    [self resetTrackingRect];
}

- (void)stop
{
    [self removeTrackingRect];

    if (!isStarted){
        return;
    }
    
    isStarted = NO;

    // Stop any active streams
    [streams makeObjectsPerformSelector:@selector(stop)];
    
    // Stop the null events
    [eventSender stop];
    [eventSender release];

    // Set cursor back to arrow cursor
    [[NSCursor arrowCursor] set];
    
    // Stop notifications
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    
    NPError npErr;
    npErr = NPP_Destroy(instance, NULL);
    LOG(Plugins, "NPP_Destroy: %d", npErr);
}

- (WebDataSource *)dataSource
{
    // Do nothing. Overridden by subclasses.
    return nil;
}

- (WebFrame *)webFrame
{
    return [[self dataSource] webFrame];
}

- (WebController *)controller
{
    return [[self webFrame] controller];
}

- (WebNetscapePlugin *)plugin
{
    return plugin;
}

- (void)setPlugin:(WebNetscapePlugin *)thePlugin;
{
    [plugin release];
    plugin = [thePlugin retain];

    NPP_New = 		[plugin NPP_New];
    NPP_Destroy = 	[plugin NPP_Destroy];
    NPP_SetWindow = 	[plugin NPP_SetWindow];
    NPP_NewStream = 	[plugin NPP_NewStream];
    NPP_WriteReady = 	[plugin NPP_WriteReady];
    NPP_Write = 	[plugin NPP_Write];
    NPP_StreamAsFile = 	[plugin NPP_StreamAsFile];
    NPP_DestroyStream = [plugin NPP_DestroyStream];
    NPP_HandleEvent = 	[plugin NPP_HandleEvent];
    NPP_URLNotify = 	[plugin NPP_URLNotify];
    NPP_GetValue = 	[plugin NPP_GetValue];
    NPP_SetValue = 	[plugin NPP_SetValue];
    NPP_Print = 	[plugin NPP_Print];
}

- (void)setMIMEType:(NSString *)theMIMEType
{
    [MIMEType release];
    MIMEType = [theMIMEType retain];
}

- (void)setBaseURL:(NSURL *)theBaseURL
{
    [baseURL release];
    baseURL = [theBaseURL retain];
}

- (void)setArguments:(NSDictionary *)arguments
{
    LOG(Plugins, "%@", arguments);

    // Convert arguments dictionary to 2 string arrays.
    // These arrays are passed to NPP_New, but the strings need to be
    // modifiable and live the entire life of the plugin.

    // The Java plug-in requires the first argument to be the base URL
    if ([MIMEType isEqualToString:@"application/x-java-applet"]) {
        cAttributes = (char **)malloc(([arguments count] + 1) * sizeof(char *));
        cValues = (char **)malloc(([arguments count] + 1) * sizeof(char *));
        cAttributes[0] = strdup("DOCBASE");
        cValues[0] = strdup([[baseURL absoluteString] UTF8String]);
        argsCount++;
    } else {
        cAttributes = (char **)malloc([arguments count] * sizeof(char *));
        cValues = (char **)malloc([arguments count] * sizeof(char *));
    }

    NSEnumerator *e = [arguments keyEnumerator];
    NSString *key;
    while ((key = [e nextObject])) {
        cAttributes[argsCount] = strdup([key UTF8String]);
        cValues[argsCount] = strdup([[arguments objectForKey:key] UTF8String]);
        argsCount++;
    }
}

- (void)setMode:(int)theMode
{
    mode = theMode;
}

#pragma mark NSVIEW

- initWithFrame:(NSRect)frame
{
    [super initWithFrame:frame];

    instance = &instanceStruct;
    instance->ndata = self;

    canRestart = YES;

    streams = [[NSMutableArray alloc] init];
    notificationData = [[NSMutableDictionary alloc] init];

    return self;
}

-(void)dealloc
{
    unsigned i;

    [self stop];

    for (i = 0; i < argsCount; i++) {
        free(cAttributes[i]);
        free(cValues[i]);
    }
    [streams removeAllObjects];
    [streams release];
    [MIMEType release];
    [baseURL release];
    [notificationData release];
    free(cAttributes);
    free(cValues);
    [super dealloc];
}

- (void)drawRect:(NSRect)rect
{
#if 0
    [[NSColor redColor] set];
    NSRectFill([self bounds]);
#endif
    if(isStarted){
        [self sendUpdateEvent];
    }
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
    // We must remove the tracking rect before we move to the new window.
    // Once we move to the new window, it will be too late.
    [self removeTrackingRect];
    [super viewWillMoveToWindow:newWindow];
}

- (void)viewDidMoveToWindow
{
    if (![self window]){
        [self stop];
    }
    
    [self resetTrackingRect];
}

#pragma mark NOTIFICATIONS

-(void)viewHasMoved:(NSNotification *)notification
{
    [self setUpWindowAndPort];

    // reset the tracking rect
    [self resetTrackingRect];
}

-(void)windowWillClose:(NSNotification *)notification
{
    [self stop];
}

-(void)windowBecameKey:(NSNotification *)notification
{
    [self sendActivateEvent:YES];
    [self performSelector:@selector(sendUpdateEvent) withObject:nil afterDelay:.001];
}

-(void)windowResignedKey:(NSNotification *)notification
{
    [self sendActivateEvent:NO];
    [self performSelector:@selector(sendUpdateEvent) withObject:nil afterDelay:.001];
}

- (void)defaultsHaveChanged:(NSNotification *)notification
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    if ([defaults boolForKey:@"WebKitPluginsEnabled"]) {
        canRestart = YES;
        [self start];
    } else {
        canRestart = NO;
        [self stop];
        [self setNeedsDisplay:YES];
    }
}

- (void)frameStateChanged:(NSNotification *)notification
{
    WebFrame *frame;
    WebFrameState frameState;
    NSValue *notifyDataValue;
    void *notifyData;
    NSURL *URL;
    
    frame = [notification object];
    URL = [[[frame dataSource] request] URL];
    notifyDataValue = [notificationData objectForKey:URL];

    if(!notifyDataValue){
        return;
    }
    
    notifyData = [notifyDataValue pointerValue];
    frameState = [[[notification userInfo] objectForKey:WebCurrentFrameState] intValue];
    if (frameState == WebFrameStateComplete) {
        NPP_URLNotify(instance, [[URL absoluteString] cString], NPRES_DONE, notifyData);
    }
    //FIXME: Need to send other NPReasons
}

- (NPP)pluginInstance
{
    return instance;
}


@end

@implementation WebBaseNetscapePluginView (WebNPPCallbacks)

- (NSURL *)pluginURLFromCString:(const char *)URLCString
{
    NSString *URLString;
    NSURL *URL;

    URLString = [NSString stringWithCString:URLCString];

    if ([URLString _web_looksLikeAbsoluteURL]) {
        URL = [NSURL _web_URLWithString:URLString];
    }
    else {
        URL = [NSURL _web_URLWithString:URLString relativeToURL:baseURL];
    }
    
    return URL;
}

- (NPError)loadRequest:(WebResourceRequest *)request inTarget:(NSString *)target withNotifyData:(void *)notifyData
{
    WebNetscapePluginStream *stream;
    WebDataSource *dataSource;
    WebFrame *frame;
    NSURL *URL;
    
    URL = [request URL];

    if(!URL){
        return NPERR_INVALID_URL;
    }
    
    if(!target){
        stream = [[WebNetscapePluginStream alloc] initWithURL:URL pluginPointer:instance notifyData:notifyData];
        if(stream){
            [streams addObject:stream];
            [stream startLoad];
            [stream release];
        }else{
            return NPERR_INVALID_URL;
        }
    }else{
        frame = [[self webFrame] frameNamed:target];
        if(!frame){
            // FIXME: Why is it OK to just discard all the attributes in this case?
            [[[self controller] windowOperationsDelegate] openNewWindowWithURL:URL referrer:nil];
            // FIXME: Need to send NPP_URLNotify at the right time.
            // FIXME: Need to name new frame
            if(notifyData){
                NPP_URLNotify(instance, [[URL absoluteString] cString], NPRES_DONE, notifyData);
            }
        }else{
            if(notifyData){
                if(![target isEqualToString:@"_self"] && ![target isEqualToString:@"_current"] && 
                    ![target isEqualToString:@"_parent"] && ![target isEqualToString:@"_top"]){
    
                    [notificationData setObject:[NSValue valueWithPointer:notifyData] forKey:URL];
                    [[NSNotificationCenter defaultCenter] addObserver:self 
                        selector:@selector(frameStateChanged:) name:WebFrameStateChangedNotification object:frame];
                }
                // Plug-in docs say to return NPERR_INVALID_PARAM here
                // but IE allows an NPP_*URLNotify when the target is _self, _current, _parent or _top
                // so we have to allow this as well. Needed for iTools.
            }
            dataSource = [[WebDataSource alloc] initWithRequest:request];
            if ([frame setProvisionalDataSource:dataSource]) {
                [frame startLoading];
            }
            [dataSource release];
        }
    }
    return NPERR_NO_ERROR;
}

-(NPError)getURLNotify:(const char *)URL target:(const char *)target notifyData:(void *)notifyData
{
    NSString *theTarget = nil;
    NSURL *pluginURL;
    WebResourceRequest *request;
        
    LOG(Plugins, "NPN_GetURLNotify: %s target: %s", URL, target);
        
    if(!URL)
        return NPERR_INVALID_URL;
        
    if(target)
        theTarget = [NSString stringWithCString:target];

    pluginURL = [self pluginURLFromCString:URL]; 

    if(!pluginURL)
        return NPERR_INVALID_URL;
        
    request = [[[WebResourceRequest alloc] initWithURL:pluginURL] autorelease];
    
    return [self loadRequest:request inTarget:theTarget withNotifyData:notifyData];
}

-(NPError)getURL:(const char *)URL target:(const char *)target
{
    NSString *theTarget = nil;
    NSURL *pluginURL;
    WebResourceRequest *request;
    
    LOG(Plugins, "NPN_GetURL: %s target: %s", URL, target);
    
    if(!URL)
        return NPERR_INVALID_URL;
        
    if(target)
        theTarget = [NSString stringWithCString:target];

    pluginURL = [self pluginURLFromCString:URL]; 

    if(!pluginURL)
        return NPERR_INVALID_URL;
        
    request = [[[WebResourceRequest alloc] initWithURL:pluginURL] autorelease];
    
    return [self loadRequest:request inTarget:theTarget withNotifyData:NULL];
}

-(NPError)postURLNotify:(const char *)URL target:(const char *)target len:(UInt32)len buf:(const char *)buf file:(NPBool)file notifyData:(void *)notifyData
{
    NSData *postData;
    NSURL *tempURL;
    NSString *path, *theTarget = nil;
    NSURL *pluginURL;
    WebResourceRequest *request;
    
    LOG(Plugins, "NPN_PostURLNotify: %s", URL);
 
    if(!URL)
        return NPERR_INVALID_URL;
        
    if(target)
        theTarget = [NSString stringWithCString:target];
 
    if(file){
        if([[NSString stringWithCString:buf] _web_looksLikeAbsoluteURL]){
            tempURL = [NSURL fileURLWithPath:[NSString stringWithCString:URL]];
            path = [tempURL path];
        }else{
            path = [NSString stringWithCString:buf];
        }
        postData = [NSData dataWithContentsOfFile:path];
    }else{
        postData = [NSData dataWithBytes:buf length:len];
    }

    pluginURL = [self pluginURLFromCString:URL]; 

    if(!pluginURL)
        return NPERR_INVALID_URL;
        
    request = [[[WebResourceRequest alloc] initWithURL:pluginURL] autorelease];
    [request setMethod:@"POST"];
    [request setData:postData];
    
    return [self loadRequest:request inTarget:theTarget withNotifyData:notifyData];
}

-(NPError)postURL:(const char *)URL target:(const char *)target len:(UInt32)len buf:(const char *)buf file:(NPBool)file
{
    NSString *theTarget = nil;
        
    LOG(Plugins, "NPN_PostURL: %s", URL);
    
    if(!URL)
        return NPERR_INVALID_URL;
        
    if(target)
        theTarget = [NSString stringWithCString:target];
        
    return [self postURLNotify:URL target:target len:len buf:buf file:file notifyData:NULL];
}

-(NPError)newStream:(NPMIMEType)type target:(const char *)target stream:(NPStream**)stream
{
    LOG(Plugins, "NPN_NewStream");
    return NPERR_GENERIC_ERROR;
}

-(NPError)write:(NPStream*)stream len:(SInt32)len buffer:(void *)buffer
{
    LOG(Plugins, "NPN_Write");
    return NPERR_GENERIC_ERROR;
}

-(NPError)destroyStream:(NPStream*)stream reason:(NPReason)reason
{
    LOG(Plugins, "NPN_DestroyStream");
    if(!stream->ndata)
        return NPERR_INVALID_INSTANCE_ERROR;
        
    [(WebNetscapePluginStream *)stream->ndata stop];
    return NPERR_NO_ERROR;
}

-(void)status:(const char *)message
{
    LOG(Plugins, "NPN_Status: %s", message);
    if([self controller]){
        [[[self controller] windowOperationsDelegate] setStatusText:[NSString stringWithCString:message]];
    }
}

-(void)invalidateRect:(NPRect *)invalidRect
{
    LOG(Plugins, "NPN_InvalidateRect");
    [self sendUpdateEvent];
}

-(void)invalidateRegion:(NPRegion)invalidateRegion
{
    LOG(Plugins, "NPN_InvalidateRegion");
    [self sendUpdateEvent];
}

-(void)forceRedraw
{
    LOG(Plugins, "forceRedraw");
    [self sendUpdateEvent];
}

@end
