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
#import <QuartzCore/QuartzCore.h>
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
//  The touch-to-mouse gesture layer, shared with Android - long press for
//  the right button, drag for left on a control and middle on the world,
//  pinch cancelling a drag, and a press outside an edit box closing the
//  keyboard. It lived inside android_main.cpp, so iOS had none of it.
void RanGesture_Down ( int x, int y );
void RanGesture_Move ( int x, int y );
void RanGesture_Up   ( int x, int y );
void RanGesture_Tick ( void );
void RanUIPan_Update ( void );
void RanGesture_SetImeActive ( int active );
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
@property (nonatomic, assign) BOOL           bootFailed;
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

    //  Once. A failed boot used to be retried on the next frame, which meant
    //  the first run on a phone made 1,461 attempts and a 31,000-line log
    //  before anyone read it - and every attempt re-ran the splash, so the
    //  screen looked like it was still loading rather than broken. Boot is not
    //  something that succeeds on the second try: whatever it could not find
    //  it will not find a frame later.
    if (self.bootFailed) return;

    if (!self.booted) {
        const char *root = RanIOS_DataRoot();
        //  Something on screen before the client boots: it loads for seconds
        //  with no device of its own, and the window is otherwise black.
        RanSplash_Begin ( root );
        const int ok = RanApp_Boot ( root, RanGL_LogicalWidth(), RanGL_LogicalHeight() );
        RanSplash_End ();
        if (!ok) {
            RanPlat_Log ( RANLOG_ERROR, "RanIOS",
                          "boot failed - giving up (logical %dx%d)",
                          RanGL_LogicalWidth(), RanGL_LogicalHeight() );
            self.bootFailed = YES;
            [self.link invalidate];
            self.link = nil;
            return;
        }
        self.booted = YES;
    }

    RanTouch_Frame ( dt );
    //  A finger resting still produces no events, so the hold that becomes
    //  the right button can only be noticed here.
    //  Keyboard pan, before draw and before touches - see ui_pan.cpp.
    RanUIPan_Update ();
    RanGesture_Tick ();
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

//  Where a touch is, in the space everything downstream lays out in.
//
//  UIKit gives points; the layer scale turns those into panel pixels; and the
//  frame is drawn at panel/RanGL_InputScale, which is what RanTouch_Init was
//  handed and what the client's own hit test uses. Android divides once, up
//  front, and passes the result to BOTH the overlay and the client:
//
//      const int scale = RanGL_InputScale();
//      px = AMotionEvent_getX(event, i) / scale;
//      if (RanTouch_PointerDown(pid, px, py)) return 1;
//      RanInput_PointerMove(px, py);
//
//  Here the division was applied only on the client's side, so the overlay was
//  hit-tested in panel pixels against a layout built in logical ones - on this
//  phone a factor of two out, with every pad and skill button reading as a
//  press somewhere else. RanUI_PointInControl, which RanTouch consults first,
//  was asked the same doubled question.
- (CGPoint)inputPointOf:(UITouch *)t
{
    const CGPoint p = [self pixelsOf:t];
    const CGFloat is = RanGL_InputScale() > 0 ? (CGFloat)RanGL_InputScale() : 1.0f;
    return CGPointMake ( p.x / is, p.y / is );
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches) {
        const int slot = [self slotFor:t assign:YES];
        if (slot < 0) continue;
        const CGPoint p = [self inputPointOf:t];
        //  Offered to the overlay first, which says whether it took it; the
        //  client only hears about what is left. Exactly as on Android.
        if (RanTouch_PointerDown ( slot, (float)p.x, (float)p.y )) continue;
        RanGesture_Down ( (int)p.x, (int)p.y );
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches) {
        const int slot = [self slotFor:t assign:NO];
        if (slot < 0) continue;
        const CGPoint p = [self inputPointOf:t];
        if (RanTouch_PointerMove ( slot, (float)p.x, (float)p.y )) continue;
        RanGesture_Move ( (int)p.x, (int)p.y );
    }
}

- (void)releaseTouches:(NSSet<UITouch *> *)touches
{
    for (UITouch *t in touches) {
        const int slot = [self slotFor:t assign:NO];
        if (slot < 0) continue;
        const CGPoint p = [self inputPointOf:t];
        const int taken = RanTouch_PointerUp ( slot, (float)p.x, (float)p.y );
        _slots[slot] = nil;
        if (!taken) RanGesture_Up ( (int)p.x, (int)p.y );
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
{
    RanGesture_SetImeActive ( 1 );
    dispatch_async ( dispatch_get_main_queue(), ^{ [g_vc becomeFirstResponder]; } );
}

extern "C" void RanIME_Hide ( void )
{
    RanGesture_SetImeActive ( 0 );
    dispatch_async ( dispatch_get_main_queue(), ^{ [g_vc resignFirstResponder]; } );
}

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

//  Nothing to pump here.
//
//  UIKit runs the run loop on the main thread and the client renders from the
//  display link, so a blocking wait in the client never holds up event
//  delivery. The symbol exists because the shared shim's Sleep calls it.
extern "C" void RanPlat_PumpEvents ( void ) {}

//  ------------------------------------------------------------ patch screen
//
//  What RanLauncher's page is on Android: the only thing on screen until the
//  data is up to date, and then it hands straight over to the game.
//
//  Laid out band for band with RanLauncher.java, because the player sees this
//  page and then, a moment later, the client's own map loader draws the same
//  one. LoadingThread.cpp works in a 1024x768 virtual space - ld_top 1024x128
//  at (0,0), the art 1024x512 at (0,128), ld_under 1024x128 at (0,640) - so
//  each band is 128/768 of the height, whatever the panel is.
//
//  The art is bundled with the app, exactly as the Android launcher bundles it
//  in res/drawable-nodpi. An earlier note here said the page had to be text
//  only because the art lives in Gui.rcc and the patcher is what downloads it.
//  The first half is true and the second does not follow: Android carries the
//  same four PNGs in its own package and draws them before a byte is fetched.

@interface RanPatchViewController : UIViewController
{
    double _shownAt;
    double _holdSeconds;
}
@property (nonatomic, strong) UILabel *status, *detail;
@property (nonatomic, strong) UIProgressView *bar;
@property (nonatomic, strong) UIImageView *art, *topBand, *underBand, *mark;
@property (nonatomic, strong) UIStackView *band;
@property (nonatomic, strong) NSLayoutConstraint *bandCentre;
@end

@implementation RanPatchViewController

- (BOOL)prefersStatusBarHidden { return YES; }

- (void)viewDidLoad
{
    [super viewDidLoad];
    //  #0B0E10, the Java's root colour.
    self.view.backgroundColor = [UIColor colorWithRed:0x0B/255.0
                                                green:0x0E/255.0
                                                 blue:0x10/255.0 alpha:1.0];

    //  Fill the middle band and crop rather than letterbox: black bars around
    //  the art look like a broken asset.
    self.art = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"ran_loading"]];
    self.art.contentMode = UIViewContentModeScaleAspectFill;
    self.art.clipsToBounds = YES;
    [self.view addSubview:self.art];

    //  The bands are stretched to width, as the client does - they are a frame,
    //  not a picture, and their ends have to meet the edges of the screen.
    self.topBand = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"ld_top"]];
    self.topBand.contentMode = UIViewContentModeScaleToFill;
    [self.view addSubview:self.topBand];

    self.underBand = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"ld_under"]];
    self.underBand.contentMode = UIViewContentModeScaleToFill;
    [self.view addSubview:self.underBand];

    //  In the top band, which is empty by design: it is where the client puts
    //  the map name on its own loading screen.
    self.mark = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"ran_mark"]];
    self.mark.contentMode = UIViewContentModeScaleAspectFit;
    [self.view addSubview:self.mark];

    self.status = [UILabel new];
    self.status.font = [UIFont systemFontOfSize:15];
    //  #F0F4F6
    self.status.textColor = [UIColor colorWithRed:0xF0/255.0 green:0xF4/255.0
                                             blue:0xF6/255.0 alpha:1.0];
    self.status.textAlignment = NSTextAlignmentCenter;
    self.status.text = @"Starting";

    self.bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    //  Android's bar is the theme accent, #FFCB00, on a dark track, and about
    //  6dp tall - sampled off a screenshot of the launcher page rather than
    //  guessed. UIProgressView's default is a thin system-blue line on light
    //  grey, which is the one thing on this page that did not match.
    self.bar.progressTintColor = [UIColor colorWithRed:1.0 green:203/255.0 blue:0.0 alpha:1.0];
    self.bar.trackTintColor    = [UIColor colorWithWhite:0.22 alpha:1.0];
    //  A UIProgressView is a fixed ~4.5pt tall whatever frame it is given, so
    //  the only way to thicken it is to scale it.
    self.bar.transform = CGAffineTransformMakeScale ( 1.0f, 1.4f );

    self.detail = [UILabel new];
    self.detail.font = [UIFont systemFontOfSize:12];
    //  #AEB8BE
    self.detail.textColor = [UIColor colorWithRed:0xAE/255.0 green:0xB8/255.0
                                             blue:0xBE/255.0 alpha:1.0];
    self.detail.textAlignment = NSTextAlignmentCenter;
    self.detail.numberOfLines = 0;

    //  Say whether the art actually resolved. [UIImage imageNamed:] answers nil
    //  for a file that is not in the bundle, and the page then lays itself out
    //  correctly around nothing - which looks like a styling problem and is
    //  not one. The four PNGs were ignored by a tree-wide *.png rule once
    //  already, so this is worth a line in the log.
    {
        NSArray<NSString *> *names = @[@"ran_loading", @"ld_top", @"ld_under", @"ran_mark"];
        for (NSString *n in names) {
            UIImage *im = [UIImage imageNamed:n];
            if (im)
                RanPlat_Log ( RANLOG_INFO, "RanPatch", "page art %s: %.0fx%.0f",
                              n.UTF8String, im.size.width, im.size.height );
            else
                RanPlat_Log ( RANLOG_ERROR, "RanPatch", "page art %s: NOT IN THE BUNDLE",
                              n.UTF8String );
        }
    }

    //  The text and the bar sit in the bottom band. That band is already dark,
    //  so it needs no scrim of its own.
    self.band = [[UIStackView alloc] initWithArrangedSubviews:
                    @[self.status, self.bar, self.detail]];
    self.band.axis = UILayoutConstraintAxisVertical;
    self.band.alignment = UIStackViewAlignmentFill;
    self.band.spacing = 8;
    //  Constraints, not a frame computed in viewDidLayoutSubviews.
    //  The bar is hidden while the patcher has no percentage to show and
    //  shown again when it has, and hiding an arranged subview changes
    //  the stack's height. A frame measured once was measured with the
    //  bar hidden, and the bar had nowhere to appear when it came back.
    self.band.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:self.band];
    self.bandCentre = [self.band.centerYAnchor
                          constraintEqualToAnchor:self.view.bottomAnchor];
    [NSLayoutConstraint activateConstraints:@[
        [self.band.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:24],
        [self.band.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-24],
        self.bandCentre,
    ]];

    [self run];
}

//  Laid out here rather than with constraints: the bands are a fraction of the
//  panel height, which is not known until the view has been sized, and it has
//  to follow a rotation.
- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];

    const CGSize  size  = self.view.bounds.size;
    const CGFloat bandH = roundf ( size.height * 128.0f / 768.0f );

    self.art.frame       = CGRectMake ( 0, bandH, size.width, size.height - 2*bandH );
    self.topBand.frame   = CGRectMake ( 0, 0, size.width, bandH );
    self.underBand.frame = CGRectMake ( 0, size.height - bandH, size.width, bandH );

    //  Sized off the band rather than in points, so it keeps its margin on any
    //  panel. 0.82 is the Java's figure.
    const CGFloat markH = roundf ( bandH * 0.82f );
    self.mark.frame = CGRectMake ( roundf ( ( size.width - markH ) / 2.0f ),
                                   roundf ( ( bandH - markH ) / 2.0f ), markH, markH );

    //  The band sits in the middle of the bottom strip; its height comes
    //  from its own content, so only the centre has to follow the panel.
    self.bandCentre.constant = -bandH / 2.0f;
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    //  How long the player actually sees this page. With the data already
    //  current the patcher has nothing to do and the page is gone in a blink,
    //  which is a different complaint from the page looking wrong.
    self->_shownAt = CACurrentMediaTime ();
    RanPlat_Log ( RANLOG_INFO, "RanPatch", "page on screen" );
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    RanPlat_Log ( RANLOG_INFO, "RanPatch", "page leaving after %.2f s",
                  CACurrentMediaTime () - self->_shownAt );
}

- (void)run
{
    //  /Documents/ran/patchhold keeps this page up after the patcher is
    //  done, so it can be photographed and compared against Android's.
    //  With the data current it is otherwise on screen for about a third
    //  of a second, which no screen capture over USB can catch.
    self->_holdSeconds = RanPlat_DiagExists ( "patchhold" ) ? 12.0 : 0.0;

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
                const BOOL wantHidden = (permille < 0);
                if (me.bar.hidden != wantHidden) {
                    me.bar.hidden = wantHidden;
                    [me.view setNeedsLayout];
                }
                if (permille >= 0) [me.bar setProgress:permille / 1000.0f animated:NO];
            });
        },
        ^(BOOL ok, NSString *error) {
            dispatch_async ( dispatch_get_main_queue(), ^{
                RanPatchViewController *me = weakSelf;
                if (!me) return;
                if (ok) {
                    //  Before the swap, while the page is still on screen.
                    [me writeBootCover];
                    if (me->_holdSeconds > 0.0) {
                        RanPlat_Log ( RANLOG_INFO, "RanPatch",
                                      "holding the page for %.0f s (patchhold)",
                                      me->_holdSeconds );
                        dispatch_after ( dispatch_time ( DISPATCH_TIME_NOW,
                                            (int64_t)(me->_holdSeconds * NSEC_PER_SEC) ),
                                         dispatch_get_main_queue(), ^{ [me handOver]; } );
                    } else {
                        [me handOver];
                    }
                    return;
                }
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

//  Hand the page itself to the client's boot screen.
//
//  RanSplash draws whatever is in <root>/cache/bootcover.bin, and falls back to
//  loading_002.dds - a zone loading screen - when there is none. Android's
//  launcher writes the file, so the player sees one continuous page from the
//  patcher through the seconds of client boot. iOS never wrote it, so the page
//  was replaced by a loading screen that has nothing to do with logging in.
//
//  Same format as RanLauncher.handOverPage: "RANC", width and height as
//  little-endian 32-bit, then width*height*4 bytes of RGBA. Half resolution,
//  because it is a photograph behind a caption, it is stretched back by a
//  linear filter, and this keeps the write under a frame.
- (void)writeBootCover
{
    const CGSize full = self.view.bounds.size;
    const CGFloat scale = self.view.window.screen.scale ?: 2.0;
    const int w = (int)( full.width  * scale / 2.0 );
    const int h = (int)( full.height * scale / 2.0 );
    if (w <= 0 || h <= 0) return;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB ();
    CGContextRef ctx = CGBitmapContextCreate ( NULL, (size_t)w, (size_t)h, 8, (size_t)w * 4, cs,
                                               kCGImageAlphaPremultipliedLast |
                                               kCGBitmapByteOrder32Big );
    CGColorSpaceRelease ( cs );
    if (!ctx) return;

    //  UIKit's origin is top left and Core Graphics' is bottom left.
    CGContextTranslateCTM ( ctx, 0, h );
    CGContextScaleCTM ( ctx, 1.0, -1.0 );
    CGContextScaleCTM ( ctx, scale / 2.0, scale / 2.0 );
    [self.view.layer renderInContext:ctx];

    const unsigned char *px = (const unsigned char *)CGBitmapContextGetData ( ctx );
    if (px) {
        NSString *dir = [NSString stringWithFormat:@"%s/cache", RanIOS_DataRoot()];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil error:nil];
        NSMutableData *out = [NSMutableData dataWithCapacity:16 + (NSUInteger)w * h * 4];
        [out appendBytes:"RANC" length:4];
        const uint32_t lw = CFSwapInt32HostToLittle ( (uint32_t)w );
        const uint32_t lh = CFSwapInt32HostToLittle ( (uint32_t)h );
        [out appendBytes:&lw length:4];
        [out appendBytes:&lh length:4];
        [out appendBytes:px length:(NSUInteger)w * h * 4];

        //  Written beside and renamed, so a half-written file is never picked up.
        NSString *tmp = [dir stringByAppendingPathComponent:@"bootcover.tmp"];
        NSString *dst = [dir stringByAppendingPathComponent:@"bootcover.bin"];
        if ([out writeToFile:tmp atomically:NO]) {
            [[NSFileManager defaultManager] removeItemAtPath:dst error:nil];
            if ([[NSFileManager defaultManager] moveItemAtPath:tmp toPath:dst error:nil])
                RanPlat_Log ( RANLOG_INFO, "RanPatch", "boot cover %dx%d written", w, h );
            else
                [[NSFileManager defaultManager] removeItemAtPath:tmp error:nil];
        }
    }
    CGContextRelease ( ctx );
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
