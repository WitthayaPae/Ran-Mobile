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
#include "ran_ios_patch.h"

//  The same surface android_main.cpp uses. None of it is Android-specific.
extern "C" {
int  RanApp_Boot ( const char *dataRoot, int width, int height );
int  RanApp_Frame ( void );

int  RanGL_Init ( void *nativeWindow );
int  RanGL_SurfaceChanged ( void );
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

//  The audio sink, so the app delegate can stop the sound when it goes away -
//  the mixer runs on the audio callback thread and would otherwise play on
//  over whatever the player switched to.
void RanAudioSink_Pause ( int paused );

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
@property (nonatomic, strong) CADisplayLink *link;
@property (nonatomic, assign) BOOL           glReady;
@property (nonatomic, assign) BOOL           booted;
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

    //  Keyboard height, pushed rather than polled - see RanPlat_ImeInsetPerMille.
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(keyboardFrame:)
                                               name:UIKeyboardWillChangeFrameNotification
                                             object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(keyboardGone:)
                                               name:UIKeyboardWillHideNotification
                                             object:nil];

}

//  GL starts HERE, not in viewDidLoad, and the difference is the whole screen.
//
//  viewDidLoad runs before the view has been laid out in its window, so the
//  layer still carries the frame it was constructed with - UIScreen.bounds,
//  which reports the CURRENT interface orientation and at launch can still be
//  portrait. Initialising the drawable there would size the whole client to a
//  portrait panel and leave it there: RanApp_Boot takes the logical size once,
//  and every rect the GUI is laid out with is measured against it.
//
//  viewDidLayoutSubviews is the first point at which the size is the real one,
//  and it runs again on every rotation - which is what SurfaceChanged is for.
- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];

    if (self.glReady) { RanGL_SurfaceChanged (); return; }

    //  The layer is all the platform owes the shim. RanGL_Init makes the EAGL
    //  context, the framebuffer and the renderbuffers, exactly as it makes the
    //  EGL surface on Android - so there is one place that knows how a frame is
    //  presented, and it is not this file.
    //
    //  contentsScale is set only on this first pass: RanGL_Init divides it when
    //  the renderscale flag asks for a smaller drawable, and setting it again on
    //  a later layout would silently undo that.
    CAEAGLLayer *layer = (CAEAGLLayer *)self.view.layer;
    layer.contentsScale = UIScreen.mainScreen.nativeScale;

    if (!RanGL_Init ( (__bridge void *)layer )) {
        RanPlat_Log ( RANLOG_ERROR, "RanIOS", "RanGL_Init failed" );
        return;
    }
    RanTouch_Init ( RanGL_LogicalWidth(), RanGL_LogicalHeight() );
    if (!RanGLR_Init()) {
        RanPlat_Log ( RANLOG_ERROR, "RanIOS", "GL renderer init failed" );
        return;
    }
    self.glReady = YES;

    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self.link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSDefaultRunLoopMode];
}

//  The frame loop. android_main owns a while(); here CADisplayLink owns it and
//  calls in. That is the one structural difference between the two files.
- (void)tick:(CADisplayLink *)link
{
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

    //  RanApp_Frame presents through RanGL_Present, as it does on Android.
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

//  ------------------------------------------------------------ patch screen
//
//  What RanLauncher's page is on Android: the only thing on screen until the
//  data is up to date, and then it hands straight over to the game. Text only —
//  the Android page's art is packed in Gui.rcc, which is itself part of what
//  the patcher is downloading, so it cannot be drawn before the patch runs.

@interface RanPatchViewController : UIViewController
@property (nonatomic, strong) UILabel *status, *detail;
@property (nonatomic, strong) UIProgressView *bar;
@end

@implementation RanPatchViewController

- (BOOL)prefersStatusBarHidden { return YES; }

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;

    self.status = [UILabel new];
    self.status.font = [UIFont systemFontOfSize:22 weight:UIFontWeightSemibold];
    self.status.textColor = UIColor.whiteColor;
    self.status.textAlignment = NSTextAlignmentCenter;

    self.detail = [UILabel new];
    self.detail.font = [UIFont systemFontOfSize:14];
    self.detail.textColor = [UIColor colorWithWhite:0.72 alpha:1.0];
    self.detail.textAlignment = NSTextAlignmentCenter;
    self.detail.numberOfLines = 0;

    self.bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];

    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:
                            @[self.status, self.detail, self.bar]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 12;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:60],
        [stack.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-60],
    ]];

    [self run];
}

- (void)run
{
    __weak RanPatchViewController *weakSelf = self;
    RanIOS_RunPatch (
        ^(NSString *status, NSString *detail, int permille) {
            dispatch_async ( dispatch_get_main_queue(), ^{
                RanPatchViewController *me = weakSelf;
                if (!me) return;
                //  nil leaves the line as it was: the file loop updates only the
                //  detail, hundreds of times.
                if (status) me.status.text = status;
                if (detail) me.detail.text = detail;
                me.bar.hidden = (permille < 0);
                if (permille >= 0) [me.bar setProgress:permille / 1000.0f animated:NO];
            });
        },
        ^(BOOL ok, NSString *error) {
            dispatch_async ( dispatch_get_main_queue(), ^{
                RanPatchViewController *me = weakSelf;
                if (!me) return;
                if (ok) { [me handOver]; return; }
                //  A failed patch is a dead end, not a warning: the client would
                //  read half-updated data. Same stance as the Java.
                me.status.text = @"Update failed";
                me.detail.text = error ?: @"unknown error";
                me.bar.hidden = YES;
                RanPlat_Log ( RANLOG_ERROR, "RanPatch", "%s",
                              error ? error.UTF8String : "unknown error" );
            });
        });
}

//  Straight swap, no animation: on Android the equivalent transition showing a
//  loading screen between the two was the thing the player disliked.
- (void)handOver
{
    RanViewController *game = [RanViewController new];
    g_vc = game;
    self.view.window.rootViewController = game;
}

@end

//  --------------------------------------------------------------- app entry

@interface RanAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation RanAppDelegate
//  Sound follows the app, exactly as APP_CMD_PAUSE / APP_CMD_RESUME do on
//  Android.
- (void)applicationDidEnterBackground:(UIApplication *)app { RanAudioSink_Pause ( 1 ); }
- (void)applicationWillEnterForeground:(UIApplication *)app { RanAudioSink_Pause ( 0 ); }

- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts
{
    //  Before anything in the shim runs, so it never goes looking for
    //  /sdcard/ran or /system/fonts.
    RanIOS_InstallPlatformPaths ();

    app.idleTimerDisabled = YES;                 //  FLAG_KEEP_SCREEN_ON

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    //  The patcher first, exactly as the launcher activity comes before the
    //  game activity on Android. It replaces itself with the game.
    self.window.rootViewController = [RanPatchViewController new];
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
