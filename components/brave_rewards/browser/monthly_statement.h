/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_BROWSER_MONTHLY_STATEMENT_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_BROWSER_MONTHLY_STATEMENT_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/macros.h"
#include "brave/components/brave_rewards/browser/balance_report.h"

namespace brave_rewards {

struct MonthlyStatementPublisher {
  MonthlyStatementPublisher();
  explicit MonthlyStatementPublisher(const std::string& site_id);
  MonthlyStatementPublisher(const MonthlyStatementPublisher& properties);
  ~MonthlyStatementPublisher();

  std::string id;
  bool verified;
  bool excluded;
  std::string name;
  std::string favicon_url;
  std::string url;
  std::string provider;
  double probi;
  int32_t category;
  uint64_t date;
  uint32_t percentage;
  uint64_t reconcile_stamp;
};

struct MonthlyTransaction {
  MonthlyTransaction();
  MonthlyTransaction(const std::string& probi, uint64_t date, int32_t category);
  MonthlyTransaction(const MonthlyTransaction& properties);
  ~MonthlyTransaction();

  std::string probi;
  uint64_t date;
  int32_t category;
};

struct MonthlyStatement {
  MonthlyStatement();
  MonthlyStatement(const MonthlyStatement& properties);
  ~MonthlyStatement();

  std::vector<MonthlyStatementPublisher> publishers;
  std::vector<MonthlyTransaction> transactions;
  BalanceReport report;
  std::vector<std::string> months_available;
  uint64_t reconcile_stamp;
};

}  // namespace brave_rewards

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_BROWSER_MONTHLY_STATEMENT_H_
