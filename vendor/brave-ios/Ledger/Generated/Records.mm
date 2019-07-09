/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import "Records.h"
#import "Records+Private.h"
#import "CppTransformations.h"

#import <vector>
#import <map>
#import <string>

@implementation BATBalanceReportInfo
- (instancetype)initWithBalanceReportInfo:(const ledger::BalanceReportInfo&)obj {
  if ((self = [super init])) {
    self.openingBalance = [NSString stringWithUTF8String:obj.opening_balance_.c_str()];
    self.closingBalance = [NSString stringWithUTF8String:obj.closing_balance_.c_str()];
    self.deposits = [NSString stringWithUTF8String:obj.deposits_.c_str()];
    self.grants = [NSString stringWithUTF8String:obj.grants_.c_str()];
    self.earningFromAds = [NSString stringWithUTF8String:obj.earning_from_ads_.c_str()];
    self.autoContribute = [NSString stringWithUTF8String:obj.auto_contribute_.c_str()];
    self.recurringDonation = [NSString stringWithUTF8String:obj.recurring_donation_.c_str()];
    self.oneTimeDonation = [NSString stringWithUTF8String:obj.one_time_donation_.c_str()];
    self.total = [NSString stringWithUTF8String:obj.total_.c_str()];
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
