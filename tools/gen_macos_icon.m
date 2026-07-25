// gen_macos_icon.m - 生成输入法图标
//   macos/icon.icns    切换列表/Finder 用（蓝底白「手」）
//   macos/menuicon.pdf 菜单栏模板图标（黑色「手」，系统自动着色）
//
// 用法：clang -fobjc-arc tools/gen_macos_icon.m -framework Cocoa -o /tmp/gen_icon && /tmp/gen_icon
#import <Cocoa/Cocoa.h>

static NSString* const kGlyph = @"手";

static NSDictionary* TextAttrs(CGFloat size, NSColor* color) {
  NSFont* font = [NSFont fontWithName:@"PingFangSC-Semibold" size:size];
  if (!font) font = [NSFont boldSystemFontOfSize:size];
  return @{
    NSFontAttributeName : font,
    NSForegroundColorAttributeName : color,
  };
}

static void DrawGlyph(CGFloat size, NSColor* color, BOOL withBackground) {
  if (withBackground) {
    // 类系统输入法图标的蓝底圆角方块
    [[NSColor colorWithRed:0.04 green:0.48 blue:0.95 alpha:1.0] setFill];
    CGFloat r = size * 0.225;
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(0, 0, size, size)
                                     xRadius:r
                                     yRadius:r] fill];
  }
  NSDictionary* attrs = TextAttrs(size * 0.62, color);
  NSRect br = [kGlyph boundingRectWithSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)
                                   options:0
                                attributes:attrs];
  [kGlyph drawAtPoint:NSMakePoint((size - br.size.width) / 2 - br.origin.x,
                                  (size - br.size.height) / 2 - br.origin.y)
       withAttributes:attrs];
}

static void WritePNG(CGFloat px, NSString* path) {
  NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(px, px)];
  [img lockFocus];
  DrawGlyph(px, [NSColor whiteColor], YES);
  [img unlockFocus];
  NSBitmapImageRep* rep =
      [[NSBitmapImageRep alloc] initWithData:[img TIFFRepresentation]];
  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                  properties:@{}];
  [png writeToFile:path atomically:YES];
}

static void WriteMenuPDF(NSString* path) {
  const CGFloat size = 18;
  CGRect box = CGRectMake(0, 0, size, size);
  NSURL* url = [NSURL fileURLWithPath:path];
  CGContextRef pdf = CGPDFContextCreateWithURL((__bridge CFURLRef)url, &box, NULL);
  CGPDFContextBeginPage(pdf, NULL);
  // PDF 原点在左下，翻成左上方便 AppKit 绘制
  CGContextTranslateCTM(pdf, 0, size);
  CGContextScaleCTM(pdf, 1, -1);
  NSGraphicsContext* gc = [NSGraphicsContext graphicsContextWithCGContext:pdf
                                                                  flipped:YES];
  [NSGraphicsContext setCurrentContext:gc];
  DrawGlyph(size, [NSColor blackColor], NO);  // 模板图：纯黑，系统着色
  [NSGraphicsContext setCurrentContext:nil];
  CGPDFContextEndPage(pdf);
  CGContextRelease(pdf);
}

int main() {
  @autoreleasepool {
  NSString* dir = @"macos";
  NSString* iconset = [dir stringByAppendingPathComponent:@"icon.iconset"];
  [[NSFileManager defaultManager] createDirectoryAtPath:iconset
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:nil];
  struct { NSString* name; CGFloat px; } imgs[] = {
      {@"icon_16x16.png", 16},      {@"icon_16x16@2x.png", 32},
      {@"icon_32x32.png", 32},      {@"icon_32x32@2x.png", 64},
      {@"icon_128x128.png", 128},   {@"icon_128x128@2x.png", 256},
      {@"icon_256x256.png", 256},   {@"icon_256x256@2x.png", 512},
      {@"icon_512x512.png", 512},   {@"icon_512x512@2x.png", 1024},
  };
  for (unsigned i = 0; i < sizeof(imgs) / sizeof(imgs[0]); i++) {
    WritePNG(imgs[i].px, [iconset stringByAppendingPathComponent:imgs[i].name]);
  }
  WriteMenuPDF([dir stringByAppendingPathComponent:@"menuicon.pdf"]);
  NSLog(@"wrote %@ and menuicon.pdf", iconset);
  }
  return 0;
}
