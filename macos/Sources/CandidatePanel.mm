// SZBCandidatePanel 实现
//
// Distributed under the BSD License.
#import "CandidatePanel.h"

static const NSInteger kCols = 5;       // 每行 5 个候选
static const CGFloat kRowHeight = 30;
static const CGFloat kPadH = 10;        // 面板左右内边距
static const CGFloat kPadV = 6;         // 面板上下内边距
static const CGFloat kColGap = 18;      // 列间距
static const CGFloat kKeyHintWidth = 20;  // 列选择键提示宽度
static const CGFloat kFontSize = 18;
static const CGFloat kCornerRadius = 8;

// 列选择键提示：第 1 列空格（1 也行），其后 2 3 4 5（与控制器一致）。
static NSString* const kKeyHints[kCols] = {@"␣", @"2", @"3", @"4", @"5"};

@interface SZBCandidateView : NSView
@property(nonatomic) NSArray<NSArray<NSString*>*>* rows;
@property(nonatomic) NSInteger highlightRow;  // 窗口内下标，-1 = 无
- (NSSize)idealSize;
@end

@implementation SZBCandidateView

- (BOOL)isFlipped {
  return YES;  // 第 0 行画在顶部
}

- (NSDictionary*)textAttrs {
  return @{
    NSFontAttributeName : [NSFont systemFontOfSize:kFontSize],
    NSForegroundColorAttributeName : [NSColor labelColor],
  };
}

- (NSDictionary*)hintAttrs {
  return @{
    NSFontAttributeName : [NSFont systemFontOfSize:kFontSize - 6],
    NSForegroundColorAttributeName : [NSColor secondaryLabelColor],
  };
}

- (NSDictionary*)highlightTextAttrs {
  return @{
    NSFontAttributeName : [NSFont systemFontOfSize:kFontSize],
    NSForegroundColorAttributeName : [NSColor whiteColor],
  };
}

- (NSDictionary*)highlightHintAttrs {
  return @{
    NSFontAttributeName : [NSFont systemFontOfSize:kFontSize - 6],
    NSForegroundColorAttributeName : [NSColor colorWithWhite:1 alpha:0.7],
  };
}

// 列宽取本窗口最宽候选（等宽网格，竖向对齐）。
- (CGFloat)columnWidth {
  CGFloat maxText = 0;
  for (NSArray<NSString*>* row in _rows) {
    for (NSString* text in row) {
      NSSize s = [text sizeWithAttributes:[self textAttrs]];
      maxText = MAX(maxText, s.width);
    }
  }
  return ceil(maxText) + kKeyHintWidth;
}

- (NSSize)idealSize {
  if (_rows.count == 0) return NSZeroSize;
  CGFloat colW = [self columnWidth];
  CGFloat w = kPadH * 2 + colW * kCols + kColGap * (kCols - 1);
  CGFloat h = kPadV * 2 + kRowHeight * _rows.count;
  return NSMakeSize(ceil(w), ceil(h));
}

- (void)drawRect:(NSRect)dirtyRect {
  // 圆角背景
  [[NSColor windowBackgroundColor] setFill];
  [[NSBezierPath bezierPathWithRoundedRect:self.bounds
                                   xRadius:kCornerRadius
                                   yRadius:kCornerRadius] fill];

  CGFloat colW = [self columnWidth];
  for (NSInteger r = 0; r < (NSInteger)_rows.count; r++) {
    NSRect rowRect = NSMakeRect(kPadH - 4, kPadV + r * kRowHeight,
                                self.bounds.size.width - (kPadH - 4) * 2,
                                kRowHeight);
    BOOL hl = (r == _highlightRow);
    if (hl) {
      [[NSColor controlAccentColor] setFill];
      [[NSBezierPath bezierPathWithRoundedRect:rowRect
                                       xRadius:5
                                       yRadius:5] fill];
    }
    NSArray<NSString*>* row = _rows[r];
    for (NSInteger c = 0; c < (NSInteger)row.count && c < kCols; c++) {
      CGFloat x = kPadH + c * (colW + kColGap);
      CGFloat textY = kPadV + r * kRowHeight +
                      (kRowHeight - kFontSize) / 2 - 2;  // 视觉垂直居中
      [kKeyHints[c] drawAtPoint:NSMakePoint(x, textY + 3)
                 withAttributes:hl ? [self highlightHintAttrs]
                                   : [self hintAttrs]];
      [row[c] drawAtPoint:NSMakePoint(x + kKeyHintWidth, textY)
           withAttributes:hl ? [self highlightTextAttrs] : [self textAttrs]];
    }
  }
}

@end

@implementation SZBCandidatePanel {
  SZBCandidateView* _view;
}

- (instancetype)init {
  self = [super initWithContentRect:NSMakeRect(0, 0, 100, 40)
                          styleMask:NSWindowStyleMaskBorderless |
                                    NSWindowStyleMaskNonactivatingPanel
                            backing:NSBackingStoreBuffered
                              defer:NO];
  if (self) {
    self.level = NSStatusWindowLevel;
    self.opaque = NO;
    self.backgroundColor = [NSColor clearColor];
    self.hasShadow = YES;
    _view = [[SZBCandidateView alloc] initWithFrame:NSZeroRect];
    self.contentView = _view;
  }
  return self;
}

- (void)updateWithRows:(NSArray<NSArray<NSString*>*>*)rows
          highlightRow:(NSInteger)highlightRow {
  _view.rows = rows;
  _view.highlightRow = highlightRow;
  NSSize size = [_view idealSize];
  [self setContentSize:size];
  _view.frame = NSMakeRect(0, 0, size.width, size.height);
  [_view setNeedsDisplay:YES];
}

- (void)showAtCaretRect:(NSRect)rect {
  NSSize size = self.frame.size;
  // 夹到光标所在屏幕的可见区域
  NSScreen* screen = [NSScreen mainScreen];
  for (NSScreen* s in [NSScreen screens]) {
    if (NSPointInRect(rect.origin, s.frame)) {
      screen = s;
      break;
    }
  }
  NSRect vf = screen.visibleFrame;
  NSPoint origin;
  origin.x = MIN(MAX(rect.origin.x, vf.origin.x + 4),
                 vf.origin.x + vf.size.width - size.width - 4);
  CGFloat belowY = rect.origin.y - size.height - 2;
  if (belowY >= vf.origin.y + 4) {
    origin.y = belowY;  // 下方空间够：贴光标行下方
  } else {
    // 不够：翻转到行上方（面板底边贴行顶，不遮挡光标与文本）
    origin.y = rect.origin.y + rect.size.height + 2;
  }
  origin.y = MIN(MAX(origin.y, vf.origin.y + 4),
                 vf.origin.y + vf.size.height - size.height - 4);
  [self setFrameOrigin:origin];
  [self orderFront:nil];
}

- (void)hide {
  [self orderOut:nil];
}

@end
