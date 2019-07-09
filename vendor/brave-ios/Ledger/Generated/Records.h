/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <Foundation/Foundation.h>
#import "Enums.h"

@class BATBalanceReportInfo, BATTransactionInfo, BATTransactionsInfo, BATMediaEventInfo;

NS_ASSUME_NONNULL_BEGIN

NS_SWIFT_NAME(BalanceReportInfo)
@interface BATBalanceReportInfo : NSObject
@property (nonatomic) NSString * openingBalance;
@property (nonatomic) NSString * closingBalance;
@property (nonatomic) NSString * deposits;
@property (nonatomic) NSString * grants;
@property (nonatomic) NSString * earningFromAds;
@property (nonatomic) NSString * autoContribute;
@property (nonatomic) NSString * recurringDonation;
@property (nonatomic) NSString * oneTimeDonation;
@property (nonatomic) NSString * total;
@end

NS_SWIFT_NAME(TransactionInfo)
@interface BATTransactionInfo : NSObject
@property (nonatomic) unsigned long long timestampInSeconds;
@property (nonatomic) double estimatedRedemptionValue;
@property (nonatomic) NSString * confirmationType;
@end

NS_SWIFT_NAME(TransactionsInfo)
@interface BATTransactionsInfo : NSObject
@property (nonatomic) NSArray<BATTransactionInfo *> * transactions;
@end

NS_SWIFT_NAME(MediaEventInfo)
@interface BATMediaEventInfo : NSObject
@property (nonatomic) NSString * event;
@property (nonatomic) NSString * time;
@property (nonatomic) NSString * status;
@end

NS_ASSUME_NONNULL_END
