/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import "Records.h"
#import "Records+Private.h"
#import "CppTransformations.h"

#import <vector>
#import <map>
#import <string>

@implementation BATReconcileInfo
- (instancetype)initWithReconcileInfo:(const ledger::ReconcileInfo&)obj {
  if ((self = [super init])) {
    self.viewingid = [NSString stringWithUTF8String:obj.viewingId_.c_str()];
    self.amount = [NSString stringWithUTF8String:obj.amount_.c_str()];
    self.retryStep = (BATContributionRetry)obj.retry_step_;
    self.retryLevel = obj.retry_level_;
  }
  return self;
}
@end

@implementation BATRewardsInternalsInfo
- (instancetype)initWithRewardsInternalsInfo:(const ledger::RewardsInternalsInfo&)obj {
  if ((self = [super init])) {
    self.paymentId = [NSString stringWithUTF8String:obj.payment_id.c_str()];
    self.isKeyInfoSeedValid = obj.is_key_info_seed_valid;
    self.personaId = [NSString stringWithUTF8String:obj.persona_id.c_str()];
    self.userId = [NSString stringWithUTF8String:obj.user_id.c_str()];
    self.bootStamp = obj.boot_stamp;
    self.currentReconciles = NSDictionaryFromMap(obj.current_reconciles, ^BATReconcileInfo *(ledger::ReconcileInfo o){ return [[BATReconcileInfo alloc] initWithReconcileInfo:o]; });
  }
  return self;
}
@end

@implementation BATTransactionInfo
- (instancetype)initWithTransactionInfo:(const ledger::TransactionInfo&)obj {
  if ((self = [super init])) {
    self.timestampInSeconds = obj.timestamp_in_seconds;
    self.estimatedRedemptionValue = obj.estimated_redemption_value;
    self.confirmationType = [NSString stringWithUTF8String:obj.confirmation_type.c_str()];
  }
  return self;
}
@end

@implementation BATTransactionsInfo
- (instancetype)initWithTransactionsInfo:(const ledger::TransactionsInfo&)obj {
  if ((self = [super init])) {
    self.transactions = NSArrayFromVector(obj.transactions, ^BATTransactionInfo *(const ledger::TransactionInfo& o){ return [[BATTransactionInfo alloc] initWithTransactionInfo: o]; });
  }
  return self;
}
@end

@implementation BATMediaEventInfo
- (instancetype)initWithMediaEventInfo:(const ledger::MediaEventInfo&)obj {
  if ((self = [super init])) {
    self.event = [NSString stringWithUTF8String:obj.event_.c_str()];
    self.time = [NSString stringWithUTF8String:obj.time_.c_str()];
    self.status = [NSString stringWithUTF8String:obj.status_.c_str()];
  }
  return self;
}
@end
