// SZBCandidatePanel - 自绘候选窗
//
// IMKCandidates 在 macOS 26 上候选文字完全不渲染（实测：改字体/前景色/
// 背景色均无效，面板只剩框），弃用，学 Squirrel 自绘。
//
// 布局：5 列 × 最多 4 行；行级高亮（整行蓝底）。
// 列选择键提示：第 1 列 ␣（空格），其后 2 3 8 9。
//
// Distributed under the BSD License.
#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface SZBCandidatePanel : NSPanel

// rows: 本窗口要显示的行，每行是至多 5 个候选文本（不足留空）。
// highlightRow: 窗口内的高亮行下标（0-based，-1 = 无高亮）。
- (void)updateWithRows:(NSArray<NSArray<NSString*>*>*)rows
          highlightRow:(NSInteger)highlightRow;

// 显示在光标行矩形（Cocoa 屏幕坐标）下方；下方空间不足时翻转到
// 行上方（不遮挡光标与文本）。自动夹到屏幕内。
- (void)showAtCaretRect:(NSRect)rect;

- (void)hide;

@end

NS_ASSUME_NONNULL_END
