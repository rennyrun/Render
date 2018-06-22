/** Copyright (c) 2014-present, Facebook, Inc. */

#import "UIView+Yoga.h"
#import "YGLayout+Private.h"
#import <objc/runtime.h>

static const void *kRYGYogaAssociatedKey = &kRYGYogaAssociatedKey;

@implementation UIView (YogaKit)

- (RYGLayout *)yoga
{
  RYGLayout *yoga = objc_getAssociatedObject(self, kRYGYogaAssociatedKey);
  if (!yoga) {
    yoga = [[RYGLayout alloc] initWithView:self];
    objc_setAssociatedObject(self, kRYGYogaAssociatedKey, yoga, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }

  return yoga;
}

- (void)resetYoga
{
  RYGLayout *yoga = [[RYGLayout alloc] initWithView:self];
  objc_setAssociatedObject(self, kRYGYogaAssociatedKey, yoga, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)configureLayoutWithBlock:(RYGLayoutConfigurationBlock)block
{
  if (block != nil) {
    block(self.yoga);
  }
}

@end
