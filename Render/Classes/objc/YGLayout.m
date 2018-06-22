/** Copyright (c) 2014-present, Facebook, Inc. */

#import "YGLayout+Private.h"
#import "UIView+Yoga.h"


#define RYG_PROPERTY(type, lowercased_name, capitalized_name)    \
- (type)lowercased_name                                         \
{                                                               \
return RYGNodeStyleGet##capitalized_name(self.node);           \
}                                                               \
\
- (void)set##capitalized_name:(type)lowercased_name             \
{                                                               \
RYGNodeStyleSet##capitalized_name(self.node, lowercased_name); \
}

#define RYG_VALUE_PROPERTY(lowercased_name, capitalized_name)    \
- (CGFloat)lowercased_name                                      \
{                                                               \
RYGValue value = RYGNodeStyleGet##capitalized_name(self.node);  \
if (value.unit == RYGUnitPoint) {                              \
return value.value;                                         \
} else {                                                      \
return RYGUndefined;                                         \
}                                                             \
}                                                               \
\
- (void)set##capitalized_name:(CGFloat)lowercased_name          \
{                                                               \
RYGNodeStyleSet##capitalized_name(self.node, lowercased_name); \
}

#define RYG_EDGE_PROPERTY_GETTER(lowercased_name, capitalized_name, property, edge) \
- (CGFloat)lowercased_name                                                         \
{                                                                                  \
return RYGNodeStyleGet##property(self.node, edge);                                \
}

#define RYG_EDGE_PROPERTY_SETTER(lowercased_name, capitalized_name, property, edge) \
- (void)set##capitalized_name:(CGFloat)lowercased_name                             \
{                                                                                  \
RYGNodeStyleSet##property(self.node, edge, lowercased_name);                      \
}

#define RYG_EDGE_PROPERTY(lowercased_name, capitalized_name, property, edge) \
RYG_EDGE_PROPERTY_GETTER(lowercased_name, capitalized_name, property, edge)  \
RYG_EDGE_PROPERTY_SETTER(lowercased_name, capitalized_name, property, edge)

#define RYG_VALUE_EDGE_PROPERTY_GETTER(objc_lowercased_name, objc_capitalized_name, c_name, edge) \
- (CGFloat)objc_lowercased_name                                                                  \
{                                                                                                \
RYGValue value = RYGNodeStyleGet##c_name(self.node, edge);                                       \
if (value.unit == RYGUnitPoint) {                                                               \
return value.value;                                                                          \
} else {                                                                                       \
return RYGUndefined;                                                                          \
}                                                                                              \
}

#define RYG_VALUE_EDGE_PROPERTY_SETTER(objc_lowercased_name, objc_capitalized_name, c_name, edge) \
- (void)set##objc_capitalized_name:(CGFloat)objc_lowercased_name                                 \
{                                                                                                \
RYGNodeStyleSet##c_name(self.node, edge, objc_lowercased_name);                                 \
}

#define RYG_VALUE_EDGE_PROPERTY(lowercased_name, capitalized_name, property, edge) \
RYG_VALUE_EDGE_PROPERTY_GETTER(lowercased_name, capitalized_name, property, edge)  \
RYG_VALUE_EDGE_PROPERTY_SETTER(lowercased_name, capitalized_name, property, edge)

#define RYG_VALUE_EDGES_PROPERTIES(lowercased_name, capitalized_name)                                                            \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##Left, capitalized_name##Left, capitalized_name, RYGEdgeLeft)                             \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##Top, capitalized_name##Top, capitalized_name, RYGEdgeTop)                                \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##Right, capitalized_name##Right, capitalized_name, RYGEdgeRight)                          \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##Bottom, capitalized_name##Bottom, capitalized_name, RYGEdgeBottom)                       \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##Start, capitalized_name##Start, capitalized_name, RYGEdgeStart)                          \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##End, capitalized_name##End, capitalized_name, RYGEdgeEnd)                                \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##Horizontal, capitalized_name##Horizontal, capitalized_name, RYGEdgeHorizontal) \
RYG_VALUE_EDGE_PROPERTY(lowercased_name##Vertical, capitalized_name##Vertical, capitalized_name, RYGEdgeVertical)       \
RYG_VALUE_EDGE_PROPERTY(lowercased_name, capitalized_name, capitalized_name, RYGEdgeAll)

@interface RYGLayout ()

@property (nonatomic, weak, readonly) UIView *view;

@end

@implementation RYGLayout

@synthesize isEnabled=_isEnabled;
@synthesize isIncludedInLayout=_isIncludedInLayout;
@synthesize node=_node;

+ (void)initialize
{
  RYGSetExperimentalFeatureEnabled(RYGExperimentalFeatureWebFlexBasis, true);
}

- (instancetype)initWithView:(UIView*)view
{
  if (self = [super init]) {
    _view = view;
    _node = RYGNodeNew();
    RYGNodeSetContext(_node, (__bridge void *) view);
    _isEnabled = NO;
    _isIncludedInLayout = YES;
  }

  return self;
}

- (void)dealloc
{
  RYGNodeFree(self.node);
}

- (BOOL)isDirty
{
  return RYGNodeIsDirty(self.node);
}

- (void)markDirty
{
  if (self.isDirty || !self.isLeaf) {
    return;
  }

  // Yoga is not happy if we try to mark a node as "dirty" before we have set
  // the measure function. Since we already know that this is a leaf,
  // this *should* be fine. Forgive me Hack Gods.
  const RYGNodeRef node = self.node;
  if (RYGNodeGetMeasureFunc(node) == NULL) {
    RYGNodeSetMeasureFunc(node, RYGMeasureView);
  }

  RYGNodeMarkDirty(node);
}

- (NSUInteger)numberOfChildren
{
  return RYGNodeGetChildCount(self.node);
}

- (BOOL)isLeaf
{
  NSAssert([NSThread isMainThread], @"This method must be called on the main thread.");
  if (self.isEnabled) {
    for (UIView *subview in self.view.subviews) {
      RYGLayout *const yoga = subview.yoga;
      if (yoga.isEnabled && yoga.isIncludedInLayout) {
        return NO;
      }
    }
  }

  return YES;
}

#pragma mark - Style

- (RYGPositionType)position
{
  return RYGNodeStyleGetPositionType(self.node);
}

- (void)setPosition:(RYGPositionType)position
{
  RYGNodeStyleSetPositionType(self.node, position);
}

RYG_PROPERTY(RYGDirection, direction, Direction)
RYG_PROPERTY(RYGFlexDirection, flexDirection, FlexDirection)
RYG_PROPERTY(RYGJustify, justifyContent, JustifyContent)
RYG_PROPERTY(RYGAlign, alignContent, AlignContent)
RYG_PROPERTY(RYGAlign, alignItems, AlignItems)
RYG_PROPERTY(RYGAlign, alignSelf, AlignSelf)
RYG_PROPERTY(RYGWrap, flexWrap, FlexWrap)
RYG_PROPERTY(RYGOverflow, overflow, Overflow)
RYG_PROPERTY(RYGDisplay, display, Display)

RYG_PROPERTY(CGFloat, flexGrow, FlexGrow)
RYG_PROPERTY(CGFloat, flexShrink, FlexShrink)
RYG_VALUE_PROPERTY(flexBasis, FlexBasis)

RYG_VALUE_EDGE_PROPERTY(left, Left, Position, RYGEdgeLeft)
RYG_VALUE_EDGE_PROPERTY(top, Top, Position, RYGEdgeTop)
RYG_VALUE_EDGE_PROPERTY(right, Right, Position, RYGEdgeRight)
RYG_VALUE_EDGE_PROPERTY(bottom, Bottom, Position, RYGEdgeBottom)
RYG_VALUE_EDGE_PROPERTY(start, Start, Position, RYGEdgeStart)
RYG_VALUE_EDGE_PROPERTY(end, End, Position, RYGEdgeEnd)
RYG_VALUE_EDGES_PROPERTIES(margin, Margin)
RYG_VALUE_EDGES_PROPERTIES(padding, Padding)

RYG_EDGE_PROPERTY(borderLeftWidth, BorderLeftWidth, Border, RYGEdgeLeft)
RYG_EDGE_PROPERTY(borderTopWidth, BorderTopWidth, Border, RYGEdgeTop)
RYG_EDGE_PROPERTY(borderRightWidth, BorderRightWidth, Border, RYGEdgeRight)
RYG_EDGE_PROPERTY(borderBottomWidth, BorderBottomWidth, Border, RYGEdgeBottom)
RYG_EDGE_PROPERTY(borderStartWidth, BorderStartWidth, Border, RYGEdgeStart)
RYG_EDGE_PROPERTY(borderEndWidth, BorderEndWidth, Border, RYGEdgeEnd)
RYG_EDGE_PROPERTY(borderWidth, BorderWidth, Border, RYGEdgeAll)

RYG_VALUE_PROPERTY(width, Width)
RYG_VALUE_PROPERTY(height, Height)
RYG_VALUE_PROPERTY(minWidth, MinWidth)
RYG_VALUE_PROPERTY(minHeight, MinHeight)
RYG_VALUE_PROPERTY(maxWidth, MaxWidth)
RYG_VALUE_PROPERTY(maxHeight, MaxHeight)
RYG_PROPERTY(CGFloat, aspectRatio, AspectRatio)

#pragma mark - Layout and Sizing

- (RYGDirection)resolvedDirection
{
  return RYGNodeLayoutGetDirection(self.node);
}

- (void)applyLayout
{
  [self calculateLayoutWithSize:self.view.bounds.size];
  RYGApplyLayoutToViewHierarchy(self.view, NO);
}

- (void)applyLayoutPreservingOrigin:(BOOL)preserveOrigin
{
  [self calculateLayoutWithSize:self.view.bounds.size];
  RYGApplyLayoutToViewHierarchy(self.view, preserveOrigin);
}

- (void)applyLayoutPreservingOrigin:(BOOL)preserveOrigin withSize:(CGSize)size
{
  [self calculateLayoutWithSize:size];
  RYGApplyLayoutToViewHierarchy(self.view, preserveOrigin);
}


- (CGSize)intrinsicSize
{
  const CGSize constrainedSize = {
    .width = RYGUndefined,
    .height = RYGUndefined,
  };
  return [self calculateLayoutWithSize:constrainedSize];
}

#pragma mark - Private

- (CGSize)calculateLayoutWithSize:(CGSize)size
{
  NSAssert([NSThread isMainThread], @"Yoga calculation must be done on main.");
  NSAssert(self.isEnabled, @"Yoga is not enabled for this view.");

  RYGAttachNodesFromViewHierachy(self.view);

  const RYGNodeRef node = self.node;
  RYGNodeCalculateLayout(
                        node,
                        size.width,
                        size.height,
                        RYGNodeStyleGetDirection(node));

  return (CGSize) {
    .width = RYGNodeLayoutGetWidth(node),
    .height = RYGNodeLayoutGetHeight(node),
  };
}

static RYGSize RYGMeasureView(
                            RYGNodeRef node,
                            float width,
                            RYGMeasureMode widthMode,
                            float height,
                            RYGMeasureMode heightMode)
{
  const CGFloat constrainedWidth = (widthMode == RYGMeasureModeUndefined) ? CGFLOAT_MAX : width;
  const CGFloat constrainedHeight = (heightMode == RYGMeasureModeUndefined) ? CGFLOAT_MAX: height;

  UIView *view = (__bridge UIView*) RYGNodeGetContext(node);
  const CGSize sizeThatFits = [view sizeThatFits:(CGSize) {
    .width = constrainedWidth,
    .height = constrainedHeight,
  }];

  return (RYGSize) {
    .width = RYGSanitizeMeasurement(constrainedWidth, sizeThatFits.width, widthMode),
    .height = RYGSanitizeMeasurement(constrainedHeight, sizeThatFits.height, heightMode),
  };
}

static CGFloat RYGSanitizeMeasurement(
                                     CGFloat constrainedSize,
                                     CGFloat measuredSize,
                                     RYGMeasureMode measureMode)
{
  CGFloat result;
  if (measureMode == RYGMeasureModeExactly) {
    result = constrainedSize;
  } else if (measureMode == RYGMeasureModeAtMost) {
    result = MIN(constrainedSize, measuredSize);
  } else {
    result = measuredSize;
  }

  return result;
}

static BOOL RYGNodeHasExactSameChildren(const RYGNodeRef node, NSArray<UIView *> *subviews)
{
  if (RYGNodeGetChildCount(node) != subviews.count) {
    return NO;
  }

  for (int i=0; i<subviews.count; i++) {
    if (RYGNodeGetChild(node, i) != subviews[i].yoga.node) {
      return NO;
    }
  }

  return YES;
}

static void RYGAttachNodesFromViewHierachy(UIView *const view)
{
  RYGLayout *const yoga = view.yoga;
  const RYGNodeRef node = yoga.node;

  // Only leaf nodes should have a measure function
  if (yoga.isLeaf) {
    RYGRemoveAllChildren(node);
    RYGNodeSetMeasureFunc(node, RYGMeasureView);
  } else {
    RYGNodeSetMeasureFunc(node, NULL);

    NSMutableArray<UIView *> *subviewsToInclude = [[NSMutableArray alloc] initWithCapacity:view.subviews.count];
    for (UIView *subview in view.subviews) {
      if (subview.yoga.isIncludedInLayout) {
        [subviewsToInclude addObject:subview];
      }
    }

    if (!RYGNodeHasExactSameChildren(node, subviewsToInclude)) {
      RYGRemoveAllChildren(node);
      for (int i=0; i<subviewsToInclude.count; i++) {
        RYGNodeInsertChild(node, subviewsToInclude[i].yoga.node, i);
      }
    }

    for (UIView *const subview in subviewsToInclude) {
      RYGAttachNodesFromViewHierachy(subview);
    }
  }
}

static void RYGRemoveAllChildren(const RYGNodeRef node)
{
  if (node == NULL) {
    return;
  }

  while (RYGNodeGetChildCount(node) > 0) {
    RYGNodeRemoveChild(node, RYGNodeGetChild(node, RYGNodeGetChildCount(node) - 1));
  }
}

static CGFloat RYGRoundPixelValue(CGFloat value)
{
  static CGFloat scale;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^(){
    scale = [UIScreen mainScreen].scale;
  });

  return roundf(value * scale) / scale;
}

static void RYGApplyLayoutToViewHierarchy(UIView *view, BOOL preserveOrigin)
{
  NSCAssert([NSThread isMainThread], @"Framesetting should only be done on the main thread.");

  const RYGLayout *yoga = view.yoga;

  if (!yoga.isIncludedInLayout) {
    return;
  }

  RYGNodeRef node = yoga.node;
  const CGPoint topLeft = {
    RYGNodeLayoutGetLeft(node),
    RYGNodeLayoutGetTop(node),
  };

  const CGPoint bottomRight = {
    topLeft.x + RYGNodeLayoutGetWidth(node),
    topLeft.y + RYGNodeLayoutGetHeight(node),
  };

  const CGPoint origin = preserveOrigin ? view.frame.origin : CGPointZero;
  view.frame = (CGRect) {
    .origin = {
      .x = RYGRoundPixelValue(topLeft.x + origin.x),
      .y = RYGRoundPixelValue(topLeft.y + origin.y),
    },
    .size = {
      .width = RYGRoundPixelValue(bottomRight.x) - RYGRoundPixelValue(topLeft.x),
      .height = RYGRoundPixelValue(bottomRight.y) - RYGRoundPixelValue(topLeft.y),
    },
  };
  
  if (!yoga.isLeaf) {
    for (NSUInteger i=0; i<view.subviews.count; i++) {
      RYGApplyLayoutToViewHierarchy(view.subviews[i], NO);
    }
  }
}

@end
