/** Copyright (c) 2014-present, Facebook, Inc. */

#import "YGLayout.h"
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^RYGLayoutConfigurationBlock)(RYGLayout *);

@interface UIView (Yoga)

/**
 The RYGLayout that is attached to this view. It is lazily created.
 */
@property (nonatomic, readonly, strong) RYGLayout *yoga;

/**
 In ObjC land, every time you access `view.yoga.*` you are adding another `objc_msgSend`
 to your code. If you plan on making multiple changes to RYGLayout, it's more performant
 to use this method, which uses a single objc_msgSend call.
 */
- (void)configureLayoutWithBlock:(RYGLayoutConfigurationBlock)block
NS_SWIFT_NAME(configureLayout(block:));

- (void)resetYoga;

@end

NS_ASSUME_NONNULL_END
