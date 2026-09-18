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
#include <mach/mach.h>
#include <os/proc.h>
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
float RanGL_InputScale ( void );
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

//  Live texture and buffer memory by owner, for the MEM line.
void RanD3D_LiveMemLine ( char *out, int cap );
void RanD3D_HeldMemLine ( char *out, int cap );
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

//  The keyboard responder - see the keyboard section further down for why the
//  client types into a UITextField and not into the view controller itself.
@interface RanIMEField : UITextField <UITextFieldDelegate>
@end

@interface RanViewController : UIViewController
@property (nonatomic, strong) RanIMEField   *imeField;
@property (nonatomic, strong) CADisplayLink *link;
@property (nonatomic, assign) BOOL           glReady;
@property (nonatomic, assign) BOOL           booted;
@property (nonatomic, assign) BOOL           bootFailed;
@property (nonatomic, assign) CFTimeInterval lastTick;
@property (nonatomic, assign) BOOL           pacedForHeat;
- (void)ensureImeField;
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

    //  Measurement switch "pace30": run the frame loop at an even 30 Hz. Frames
    //  on the iPhone take 17-19 ms against a 16.7 ms vsync, so at 60 Hz they
    //  land on alternating 16.7/33.3 ms gaps; an even 30 may look smoother than
    //  an uneven 50. Off by default - A/B it with the PACE line and by eye.
    //  RanTouch_Frame and the client take their step from real elapsed time,
    //  so nothing runs slower, only less often.
    if (RanPlat_DiagExists ( "pace30" )) {
        if (@available(iOS 15.0, *)) {
            self.link.preferredFrameRateRange = CAFrameRateRangeMake ( 30, 30, 30 );
        } else {
            self.link.preferredFramesPerSecond = 30;
        }
        RanPlat_Log ( RANLOG_INFO, "RanPace", "frame loop paced at 30 Hz (pace30 diagnostic)" );
    } else {
        RanPlat_Log ( RANLOG_INFO, "RanPace", "frame loop at the display rate" );
    }

    [self.link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSDefaultRunLoopMode];
}

//  Half the frame rate once the phone is hot, back to full when it cools.
//
//  Measured in a crowd: at nominal a frame costs ~6 ms of CPU and holds 60 fps.
//  Once iOS rates the phone "serious" it halves the clocks, the same frame
//  costs ~17 ms, and at 60 Hz there is no idle left in a 16.7 ms frame - so the
//  CPU runs flat out, which is what keeps it hot. Asking for 30 gives the frame
//  33.3 ms for the same work: the chip idles half the time and can cool. The
//  game does not slow down, it is drawn less often (elapsed time drives it).
//
//  Only at serious or critical. Nominal and fair stay at the display rate, so a
//  phone that is merely warm plays at 60 as before. "pace30" still forces 30.
//  "noheatpace": hold the display rate whatever the thermal state says.
//
//  Not a setting anyone should ship - it is there so the GPU can be measured.
//  Attributing a frame means dropping one section at a time and watching GPU
//  utilisation, and that only compares if the frame rate is the same for every
//  reading. On 2026-09-18 a sweep crossed the serious threshold half way
//  through: the clamp halved the frame rate, utilisation fell with it, and
//  every section after that point read as though it cost 33 points - a child
//  of world-eff apparently costing three times its parent, which is how the
//  measurement was caught. At 30 Hz the GPU sits at 28% and every section is
//  inside the noise, so the attribution cannot be done there either.
- (void)updateThermalPacing
{
    if (!self.link) return;
    if (RanPlat_DiagExists ( "pace30" )) return;
    if (RanPlat_DiagExists ( "noheatpace" )) return;

    const NSInteger heat = NSProcessInfo.processInfo.thermalState;
    const BOOL bHot = ( heat >= NSProcessInfoThermalStateSerious );
    if (bHot == self.pacedForHeat) return;
    self.pacedForHeat = bHot;

    if (@available(iOS 15.0, *)) {
        self.link.preferredFrameRateRange = bHot ? CAFrameRateRangeMake ( 30, 30, 30 )
                                                 : CAFrameRateRangeMake ( 30, 60, 60 );
    } else {
        self.link.preferredFramesPerSecond = bHot ? 30 : 0;
    }
    RanPlat_Log ( RANLOG_INFO, "RanPace", "thermal state %d: frame loop at %s",
                  (int)heat, bHot ? "30 Hz" : "the display rate" );
}

//  The frame loop. android_main owns a while(); here CADisplayLink owns it and
//  calls in. That is the one structural difference between the two files.
- (void)tick:(CADisplayLink *)link
{
    const CFTimeInterval now = link.timestamp;
    const float dt = self.lastTick > 0 ? (float)(now - self.lastTick) : 0.0f;
    self.lastTick = now;

    //  Frame pacing, once a second: the FRAME line averages frames per second,
    //  and an average hides uneven spacing. On a 60 Hz panel a frame that runs
    //  past 16.7 ms waits for the next vsync, so the gaps alternate 16.7/33.3 ms
    //  - 50 "fps" that looks jerky, where an even 40 on the emulator looks
    //  smooth. This counts the gaps between display-link ticks the frame ran on.
    {
        static double s_gaps[240];
        static int    s_n = 0;
        static double s_since = 0.0;
        if (dt > 0.0f && s_n < 240) s_gaps[s_n++] = (double)dt;
        if (s_since == 0.0) s_since = now;
        if (now - s_since >= 1.0 && s_n > 0) {
            //  Insertion sort: at most a couple of hundred values a second.
            for (int i = 1; i < s_n; ++i) {
                const double v = s_gaps[i];
                int j = i - 1;
                while (j >= 0 && s_gaps[j] > v) { s_gaps[j + 1] = s_gaps[j]; --j; }
                s_gaps[j + 1] = v;
            }
            int over20 = 0, over34 = 0;
            for (int i = 0; i < s_n; ++i) {
                if (s_gaps[i] > 0.020) ++over20;
                if (s_gaps[i] > 0.034) ++over34;
            }
            RanPlat_Log ( RANLOG_INFO, "RanPace",
                          "PACE %d frames | gap ms median %.1f p95 %.1f max %.1f | >20ms %d  >34ms %d",
                          s_n, s_gaps[s_n / 2] * 1000.0,
                          s_gaps[(s_n * 95) / 100 < s_n ? (s_n * 95) / 100 : s_n - 1] * 1000.0,
                          s_gaps[s_n - 1] * 1000.0, over20, over34 );

            //  What iOS thinks this process is using, and how much it will
            //  still hand out. Android has no per-app ceiling worth speaking
            //  of, so a build that entered the world fine there could die here
            //  with nothing in the log and no crash report: a memory kill
            //  leaves neither. phys_footprint is the number the limit is
            //  applied to; os_proc_available_memory is what is left of it.
            {
                task_vm_info_data_t vm;
                mach_msg_type_number_t cnt = TASK_VM_INFO_COUNT;
                if ( task_info ( mach_task_self(), TASK_VM_INFO,
                                 (task_info_t)&vm, &cnt ) == KERN_SUCCESS )
                {
                    double left = 0.0;
                    if ( @available(iOS 13.0, *) )
                        left = (double)os_proc_available_memory() / 1048576.0;
                    //  The heat, as iOS itself rates it, so "the phone is hot"
                    //  is a number that a change can be measured against.
                    //  nominal -> fair -> serious (iOS starts throttling) ->
                    //  critical.
                    [self updateThermalPacing];
                    static const char *const kHeat[] = { "nominal", "fair", "serious", "critical" };
                    const NSInteger heat = NSProcessInfo.processInfo.thermalState;
                    RanPlat_Log ( RANLOG_INFO, "RanMem",
                                  "MEM footprint %.0f MB | headroom %.0f MB | heat %s",
                                  (double)vm.phys_footprint / 1048576.0, left,
                                  ( heat >= 0 && heat <= 3 ) ? kHeat[heat] : "?" );
                    //  Who owns it. A crowd test died at 3,050 MB and the
                    //  footprint alone could not say whether that was textures,
                    //  buffers or the engine's own models and animation data.
                    char own[200];
                    RanD3D_LiveMemLine ( own, sizeof(own) );
                    RanPlat_Log ( RANLOG_INFO, "RanMem", "MEM owners: %s", own );
                    char held[600];
                    RanD3D_HeldMemLine ( held, sizeof(held) );
                    RanPlat_Log ( RANLOG_INFO, "RanMem", "MEM RAM copies: %s", held );
                }
            }

            s_n = 0;
            s_since = now;
        }
    }

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
    //  Fractional on phones (RanGL_ChooseUIScale), so never truncate it.
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
//  What RanActivity's ImeView is, in UIKit: an off-screen UITextField that is
//  the real first responder. The typed text goes back through the same C entry
//  points the hardware-key path uses.
//
//  This was UIKeyInput on the view controller itself, which raises a keyboard
//  in three lines and is why it was written that way. But UIKeyInput is the
//  MINIMAL text surface - it is not UITextInput, so the system has no text
//  input to attach an input mode to, and the keyboard comes up with no globe
//  key: whatever language it opened in is the only one reachable, which is why
//  Thai players could not get to English. A UITextField is a full UITextInput
//  and gets language switching for free.
//
//  The field holds no meaning - the client's CUIEditBox owns the real buffer.
//  It exists to be typed into and to report what was typed.

- (void)ensureImeField
{
    if (self.imeField) return;
    RanIMEField *f = [[RanIMEField alloc] initWithFrame:CGRectZero];
    //  In the hierarchy, because a field outside a window cannot become first
    //  responder - but zero-sized, so no touch can ever land on it.
    [self.view addSubview:f];
    self.imeField = f;
}

@end

//  One character always sits in the field. iOS delivers no deletion to an
//  already-empty field, and this field is always empty of meaning, so without
//  the sentinel a backspace would never arrive at all.
static NSString * const kRanImeSentinel = @"​";

@implementation RanIMEField

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.delegate               = self;
    self.text                   = kRanImeSentinel;
    self.autocorrectionType     = UITextAutocorrectionTypeNo;
    self.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.spellCheckingType      = UITextSpellCheckingTypeNo;
    //  Smart punctuation would hand the client curly quotes and en dashes,
    //  which CP874 has no room for.
    self.smartQuotesType        = UITextSmartQuotesTypeNo;
    self.smartDashesType        = UITextSmartDashesTypeNo;
    self.smartInsertDeleteType  = UITextSmartInsertDeleteTypeNo;
    self.returnKeyType          = UIReturnKeySend;
    self.keyboardType           = g_imeNumeric ? UIKeyboardTypeNumberPad
                                               : UIKeyboardTypeDefault;
    return self;
}

//  Nothing is ever committed to the field: every edit becomes a client call and
//  is then refused, which leaves the sentinel in place for the next backspace.
- (BOOL)              textField:(UITextField *)field
  shouldChangeCharactersInRange:(NSRange)range
              replacementString:(NSString *)string
{
    //  The same call the Android IME's deleteSurroundingText makes, not a
    //  DIK_BACK key: the client's edit buffer holds UTF-8 and a backspace has
    //  to remove one whole character, which only RanIME_Backspace knows how.
    if (string.length == 0) { RanIME_Backspace (); return NO; }
    if ([string isEqualToString:@"\n"]) { RanInput_KeyTap ( 0x1C ); return NO; }
    RanIME_InsertUtf8 ( string.UTF8String );
    return NO;
}

//  Return sends the line: the client wants the key down on a poll plus the
//  latch RanInput_TakeEnter reports, and BasicChatRightBody needs both before
//  it will send. Same reason nativeEnter exists on Android.
- (BOOL)textFieldShouldReturn:(UITextField *)field
{
    RanInput_KeyTap ( 0x1C );
    return NO;
}

@end

//  The mirror of RanIME_Show / RanIME_Hide, which CUIEditBox calls when an edit
//  box takes and loses focus.
static __weak RanViewController *g_vc = nil;

extern "C" void RanIME_Show ( void )
{
    RanGesture_SetImeActive ( 1 );
    dispatch_async ( dispatch_get_main_queue(), ^{
        [g_vc ensureImeField];
        [g_vc.imeField becomeFirstResponder];
    } );
}

extern "C" void RanIME_Hide ( void )
{
    RanGesture_SetImeActive ( 0 );
    dispatch_async ( dispatch_get_main_queue(), ^{ [g_vc.imeField resignFirstResponder]; } );
}

extern "C" void RanIME_SetNumeric ( int numeric )
{
    if (g_imeNumeric == (numeric != 0)) return;
    g_imeNumeric = (numeric != 0);
    //  The type is read when the keyboard is built, so an already-raised one
    //  has to be told to rebuild.
    dispatch_async ( dispatch_get_main_queue(), ^{
        g_vc.imeField.keyboardType = g_imeNumeric ? UIKeyboardTypeNumberPad
                                                  : UIKeyboardTypeDefault;
        [g_vc.imeField reloadInputViews];
    } );
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
