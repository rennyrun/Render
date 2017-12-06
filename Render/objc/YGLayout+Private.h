/** Copyright (c) 2014-present, Facebook, Inc. */

#import "YGLayout.h"
#import "Yoga.h"

@interface RYGLayout ()

@property (nonatomic, assign, readonly) RYGNodeRef node;

- (instancetype)initWithView:(UIView *)view;

@end
