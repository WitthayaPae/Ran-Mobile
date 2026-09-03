//  The iOS entry point: what android_main.cpp is, in UIKit.
//
//  NOT YET COMPILED - there is no Mac on this machine. Written against the
//  documented APIs and mirroring android_main.cpp where it can. Treat every
//  line as unverified until clang has seen it once.
//
//  Everything below the shim is unchanged. This file does what
//  android_native_app_glue did: make a GL context, own the frame loop, turn
//  touches into RanTouch_* calls, and raise a keyboard when the client asks.
#ifdef __APPLE__

#import <UIKit/UIKit.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>

#include "../../shim/platform/ran_plat.h"

//  The same surface android_main.cpp uses. None of it is Android-specific.
extern "C" {
int  RanApp_Boot ( const char *dataRoot, int width, int height );
int  RanApp_Frame ( void );

int  RanGL_Init ( void *nativeWindow );
int  RanGL_LogicalWidth ( void );
int  RanGL_LogicalHeight ( void );
int  RanGL_InputScale ( void );
int  RanGLR_Init ( void );
void RanSplash_Begin ( const char *dataRoot );
void RanSplash_End ( void );

void RanTouch_Init ( int surfaceWidth, int surfaceHeight );
int  RanTouch_PointerDown ( int id, float x, float y );
int  RanTouch_PointerMove ( int id, float x, float y );
int  RanTouch_PointerUp ( int id, float x, float y );
void RanTouch_Frame ( float elapsedSeconds );

void RanInput_PointerMove ( int x, int y );
void RanInput_PointerButton ( int button, int down );
void RanInput_PumpButtons ( void );
void RanInput_KeyTap ( int scanCode );

//  The client's edit-box buffer. Android reaches it through JNI; here it is
//  simply called.
void RanIME_InsertUtf8 ( const char *utf8 );
void RanIME_Backspace ( void );

const char *RanIOS_DataRoot ( void );
void        RanIOS_InstallPlatformPaths ( void );
}

//  Set by RanIME_SetNumeric, read when UIKit builds the keyboard.
static bool g_imeNumeric      = false;
//  Set from UIKeyboardWillChangeFrame, read by RanPlat_ImeInsetPerMille.
static int  g_imeInsetPerMille = 0;

@interface RanView : UIView
@end
@implementation RanView
+ (Class)layerClass { return [CAEAGLLayer class]; }
@end

@interface RanViewController : UIViewController <UIKeyInput>
@property (nonatomic, strong) EAGLContext   *gl;
@property (nonatomic, strong) CADisplayLink *link;
@property (nonatomic, assign) BOOL           booted;
@property (nonatomic, assign) GLuint         fbo, colorRB, depthRB;
@property (nonatomic, assign) CFTimeInterval lastTick;
@end

@implementation RanViewController
{
    //  UIKit hands back a UITouch pointer; the shim wants a small integer that
    //  stays put for the life of a finger. Ten slots, as the shim has.
    __unsafe_unretained UITouch *_slots[10];
}

- (void)loadView { self.view = [[RanView alloc] initWithFrame:UIScreen.mainScreen.bounds]; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.multipleTouchEnabled = YES;

    CAEAGLLayer *layer = (CAEAGLLayer *)self.view.layer;
    layer.opaque = YES;
    //  Nothing retained between frames: the client redraws the world every
    //  frame, so a retained backing is memory for no gain.
    layer.drawableProperties = @{ kEAGLDrawablePropertyRetainedBacking : @NO,
                                  kEAGLDrawablePropertyColorFormat     : kEAGLColorFormatRGBA8 };
    layer.contentsScale = UIScreen.mainScreen.nativeScale;

    //  ES 3.0: iOS has no 3.1. That is why the renderer treats the separate
    //  attribute format as optional and falls back - see g_haveAttribFormat,
    //  already exercised on Android drivers that lack it.
    self.gl = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!self.gl || ![EAGLContext setCurrentContext:self.gl]) {
        RanPlat_Log ( RANLOG_ERROR, "RanIOS", "no ES3 context" );
        return;
    }

    [self makeFramebuffer:layer];

    if (!RanGL_Init ( (__bridge void *)layer )) {
        RanPlat_Log ( RANLOG_ERROR, "RanIOS", "RanGL_Init failed" );
        return;
    }
    RanTouch_Init ( RanGL_LogicalWidth(), RanGL_LogicalHeight() );
    if (!RanGLR_Init()) {
        RanPlat_Log ( RANLOG_ERROR, "RanIOS", "GL renderer init failed" );
        return;
    }

    //  Keyboard height, pushed rather than polled - see RanPlat_ImeInsetPerMille.
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(keyboardFrame:)
                                               name:UIKeyboardWillChangeFrameNotification
                                             object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(keyboardGone:)
                                               name:UIKeyboardWillHideNotification
                                             object:nil];

    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self.link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSDefaultRunLoopMode];
}

- (void)makeFramebuffer:(CAEAGLLayer *)layer
{
    glGenFramebuffers ( 1, &_fbo );
    glBindFramebuffer ( GL_FRAMEBUFFER, _fbo );

    glGenRenderbuffers ( 1, &_colorRB );
    glBindRenderbuffer ( GL_RENDERBUFFER, _colorRB );
    [self.gl renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
    glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, _colorRB );

    GLint w = 0, h = 0;
    glGetRenderbufferParameteriv ( GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH,  &w );
    glGetRenderbufferParameteriv ( GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h );

    //  24-bit depth with 8-bit stencil, because the shadow and water passes use
    //  stencil - the same thing the EGL config asks for on Android.
    glGenRenderbuffers ( 1, &_depthRB );
    glBindRenderbuffer ( GL_RENDERBUFFER, _depthRB );
    glRenderbufferStorage ( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h );
    glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, _depthRB );
    glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                GL_RENDERBUFFER, _depthRB );

    RanPlat_Log ( RANLOG_INFO, "RanIOS", "drawable %dx%d", (int)w, (int)h );
}

//  The frame loop. android_main owns a while(); here CADisplayLink owns it and
//  calls in. That is the one structural difference between the two files.
- (void)tick:(CADisplayLink *)link
{
    [EAGLContext setCurrentContext:self.gl];
    glBindFramebuffer ( GL_FRAMEBUFFER, _fbo );

    const CFTimeInterval now = link.timestamp;
    const float dt = self.lastTick > 0 ? (float)(now - self.lastTick) : 0.0f;
    self.lastTick = now;

    if (!self.booted) {
        const char *root = RanIOS_DataRoot();
        //  Something on screen before the client boots: it loads for seconds
        //  with no device of its own, and the window is otherwise black.
        RanSplash_Begin ( root );
        const int ok = RanApp_Boot ( root, RanGL_LogicalWidth(), RanGL_LogicalHeight() );
        RanSplash_End ();
        if (!ok) { RanPlat_Log ( RANLOG_ERROR, "RanIOS", "boot failed" ); return; }
        self.booted = YES;
    }

    RanTouch_Frame ( dt );
    RanInput_PumpButtons ();
    if (!RanApp_Frame ()) return;

    glBindRenderbuffer ( GL_RENDERBUFFER, _colorRB );
    [self.gl presentRenderbuffer:GL_RENDERBUFFER];
}

- (void)keyboardFrame:(NSNotification *)n
{
    const CGRect kb = [n.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    const CGRect win = self.view.bounds;
    //  Only the part that actually overlaps the window counts: an undocked or
    //  off-screen keyboard covers nothing, and its frame still has a height.
    const CGFloat covered = MAX ( 0.0, CGRectGetMaxY(win) - kb.origin.y );
    g_imeInsetPerMille = win.size.height > 0
                       ? (int)( covered * 1000.0 / win.size.height + 0.5 ) : 0;
    if (g_imeInsetPerMille > 1000) g_imeInsetPerMille = 1000;
}

- (void)keyboardGone:(NSNotification *)n { g_imeInsetPerMille = 0; }

//  ------------------------------------------------------------------ touch
//
//  The shim wants stable small ids and surface pixels; UIKit gives points, so
//  each is scaled before it goes in - the same numbers an Android motion event
//  already carries.

- (int)slotFor:(UITouch *)t assign:(BOOL)assign
{
    for (int i = 0; i < 10; ++i) if (_slots[i] == t) return i;
    if (!assign) return -1;
    for (int i = 0; i < 10; ++i) if (!_slots[i]) { _slots[i] = t; return i; }
    return -1;
}

- (CGPoint)pixelsOf:(UITouch *)t
{
    const CGFloat s = self.view.layer.contentsScale;
    CGPoint p = [t locationInView:self.view];
    return CGPointMake ( p.x * s, p.y * s );
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches) {
        const int slot = [self slotFor:t assign:YES];
        if (slot < 0) continue;
        const CGPoint p = [self pixelsOf:t];
        //  Offered to the overlay first, which says whether it took it; the
        //  client only hears about what is left. Exactly as on Android.
        if (RanTouch_PointerDown ( slot, (float)p.x, (float)p.y )) continue;
        const int is = RanGL_InputScale() > 0 ? RanGL_InputScale() : 1;
        RanInput_PointerMove ( (int)p.x / is, (int)p.y / is );
        RanInput_PointerButton ( 0, 1 );
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches) {
        const int slot = [self slotFor:t assign:NO];
        if (slot < 0) continue;
        const CGPoint p = [self pixelsOf:t];
        if (RanTouch_PointerMove ( slot, (float)p.x, (float)p.y )) continue;
        const int is = RanGL_InputScale() > 0 ? RanGL_InputScale() : 1;
        RanInput_PointerMove ( (int)p.x / is, (int)p.y / is );
    }
}

- (void)releaseTouches:(NSSet<UITouch *> *)touches
{
    for (UITouch *t in touches) {
        const int slot = [self slotFor:t assign:NO];
        if (slot < 0) continue;
        const CGPoint p = [self pixelsOf:t];
        const int taken = RanTouch_PointerUp ( slot, (float)p.x, (float)p.y );
        _slots[slot] = nil;
        if (!taken) RanInput_PointerButton ( 0, 0 );
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{ [self releaseTouches:touches]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{ [self releaseTouches:touches]; }

//  --------------------------------------------------------------- keyboard
//
//  What RanActivity's ImeView is, in UIKit. UIKeyInput on the first responder
//  is what raises a keyboard, and the text goes back through the same C entry
//  points the hardware-key path uses.

- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)hasText { return YES; }

- (void)insertText:(NSString *)text
{
    if (text.length == 0) return;
    //  Return sends the line: the client wants the key down on a poll plus the
    //  latch RanInput_TakeEnter reports, and BasicChatRightBody needs both
    //  before it will send. Same reason nativeEnter exists on Android.
    if ([text isEqualToString:@"\n"]) { RanInput_KeyTap ( 0x1C ); return; }
    RanIME_InsertUtf8 ( text.UTF8String );
}

//  The same call the Android IME's deleteSurroundingText makes, not a DIK_BACK
//  key: the client's edit buffer holds UTF-8 and a backspace has to remove one
//  whole character, which only RanIME_Backspace knows how to do.
- (void)deleteBackward { RanIME_Backspace (); }

//  RanIME_SetNumeric: the client asks for a digits-only pad on the port field.
- (UIKeyboardType)keyboardType { return g_imeNumeric ? UIKeyboardTypeNumberPad
                                                     : UIKeyboardTypeDefault; }
- (UIReturnKeyType)returnKeyType { return UIReturnKeySend; }
- (UITextAutocorrectionType)autocorrectionType { return UITextAutocorrectionTypeNo; }

@end

//  The mirror of RanIME_Show / RanIME_Hide, which CUIEditBox calls when an edit
//  box takes and loses focus.
static __weak RanViewController *g_vc = nil;

extern "C" void RanIME_Show ( void )
{ dispatch_async ( dispatch_get_main_queue(), ^{ [g_vc becomeFirstResponder]; } ); }

extern "C" void RanIME_Hide ( void )
{ dispatch_async ( dispatch_get_main_queue(), ^{ [g_vc resignFirstResponder]; } ); }

extern "C" void RanIME_SetNumeric ( int numeric )
{
    if (g_imeNumeric == (numeric != 0)) return;
    g_imeNumeric = (numeric != 0);
    //  The type is read when the keyboard is built, so an already-raised one
    //  has to be told to rebuild.
    dispatch_async ( dispatch_get_main_queue(), ^{ [g_vc reloadInputViews]; } );
}

//  How much of the bottom of the window the keyboard covers, in thousandths -
//  what the chat box moves itself by (DxGameStage.cpp).
//
//  Cheap here, unlike Android: UIKit posts the frame, so this is a read of a
//  variable rather than a JNI round trip, and needs no throttle.
extern "C" int RanPlat_ImeInsetPerMille ( void ) { return g_imeInsetPerMille; }

//  --------------------------------------------------------------- app entry

@interface RanAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation RanAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts
{
    //  Before anything in the shim runs, so it never goes looking for
    //  /sdcard/ran or /system/fonts.
    RanIOS_InstallPlatformPaths ();

    app.idleTimerDisabled = YES;                 //  FLAG_KEEP_SCREEN_ON

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    RanViewController *vc = [RanViewController new];
    g_vc = vc;
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main ( int argc, char *argv[] )
{
    @autoreleasepool {
        return UIApplicationMain ( argc, argv, nil,
                                   NSStringFromClass([RanAppDelegate class]) );
    }
}

#endif  //  __APPLE__
