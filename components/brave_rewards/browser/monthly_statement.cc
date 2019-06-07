/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/browser/monthly_statement.h"

namespace brave_rewards {

MonthlyStatement::MonthlyStatement() {}

MonthlyStatement::MonthlyStatement(const MonthlyStatement& properties) {
  publishers = properties.publishers;
  months_available = properties.months_available;
  report = properties.report;
  reconcile_stamp = properties.reconcile_stamp;
}

MonthlyStatement::~MonthlyStatement() {}

MonthlyStatementPublisher::MonthlyStatementPublisher() {}

MonthlyStatementPublisher::MonthlyStatementPublisher(
    const std::string& site_id) :
    id(site_id),
    verified(false),
    excluded(false),
    probi(0),
    category(0),
    date(0),
    percentage(0),
    reconcile_stamp(0) {
}

MonthlyStatementPublisher::~MonthlyStatementPublisher() {}

MonthlyStatementPublisher::MonthlyStatementPublisher(
    const MonthlyStatementPublisher& properties) {
  id = properties.id;
  verified = properties.verified;
  excluded = properties.excluded;
  name = properties.name;
  favicon_url = properties.favicon_url;
  url = properties.url;
  provider = properties.provider;
  probi = properties.probi;
  date = properties.date;
  category = properties.category;
  percentage = properties.percentage;
  reconcile_stamp = properties.reconcile_stamp;
}

MonthlyTransaction::MonthlyTransaction() {}

MonthlyTransaction::MonthlyTransaction(
    const std::string& probi_, uint64_t date_, int32_t category_) {
  probi = probi_;
  date = date_;
  category = category_;
}


MonthlyTransaction::MonthlyTransaction(const MonthlyTransaction& properties) {
  probi = properties.probi;
  date = properties.date;
  category = properties.category;
}

MonthlyTransaction::~MonthlyTransaction() {}

}  // namespace brave_rewards
